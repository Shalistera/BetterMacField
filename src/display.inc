#import <QuartzCore/QuartzCore.h>

/* Live rendering controls with menu-sized, aspect-preserving host windows. */
static int display_original_width,display_original_height;
static int display_pending_width,display_pending_height;
static NSMutableSet<NSString *> *display_overrides;
static NSString *display_last_result=@"更改即时生效";
static NSString *const display_preset_key=@"BetterMacFieldUpscalePreset";
static NSString *const display_upscaler_key=@"BetterMacFieldUpscaler";
static bool display_upscaler_startup_pending=true;
static void game_settings_note_output(int width,int height);
static bool game_settings_set_scale(double scale);
static void display_reapply_saved_preset(void);

static void display_size_window(int width,int height) {
    // UIKit's scene restrictions also clamp programmatic AppKit frame changes.
    id app=[NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
    for(id uiWindow in [app valueForKey:@"windows"]){
        id scene=[uiWindow valueForKey:@"windowScene"];
        id restrictions=[scene valueForKey:@"sizeRestrictions"];
        for(NSString *name in @[@"setMinimumSize:",@"setMaximumSize:"]){
            SEL setter=NSSelectorFromString(name);
            if([restrictions respondsToSelector:setter]){
                NSSize size=[name isEqual:@"setMinimumSize:"] ? NSZeroSize : NSMakeSize(CGFLOAT_MAX,CGFLOAT_MAX);
                ((void (*)(id,SEL,NSSize))[restrictions methodForSelector:setter])(restrictions,setter,size);
            }
        }
    }
    for(NSWindow *window in NSApp.windows){
        if(!window.visible || ![NSStringFromClass(window.class) hasPrefix:@"UINS"])continue;
        if(window.styleMask & NSWindowStyleMaskFullScreen)continue;
        NSScreen *screen=window.screen ?: NSScreen.mainScreen;
        NSRect available=NSInsetRect(screen.visibleFrame,12,12);
        CGFloat backing=window.backingScaleFactor;
        NSSize size=NSMakeSize(width/backing,height/backing);
        NSRect content=[window contentRectForFrameRect:window.frame];
        CGFloat chrome=window.frame.size.height-content.size.height;
        CGFloat fit=MIN(1,MIN(available.size.width/size.width,(available.size.height-chrome)/size.height));
        size.width=floor(size.width*fit);size.height=floor(size.width*height/width);
        NSRect frame=[window frameRectForContentRect:NSMakeRect(0,0,size.width,size.height)];
        frame.origin=NSMakePoint(NSMidX(window.frame)-frame.size.width/2,NSMidY(window.frame)-frame.size.height/2);
        frame.origin.x=MAX(NSMinX(available),MIN(frame.origin.x,NSMaxX(available)-frame.size.width));
        frame.origin.y=MAX(NSMinY(available),MIN(frame.origin.y,NSMaxY(available)-frame.size.height));
        window.contentMinSize=NSZeroSize;window.contentMaxSize=NSMakeSize(CGFLOAT_MAX,CGFLOAT_MAX);
        window.contentAspectRatio=NSMakeSize(width,height);
        [window setFrame:frame display:YES animate:NO];
        window.contentMinSize=size;window.contentMaxSize=size;
        fprintf(output,"display_window=request:%gx%g actual:%gx%g output:%dx%d backing:%g fit:%g\n",size.width,size.height,
            window.contentView.bounds.size.width,window.contentView.bounds.size.height,width,height,backing,fit);
    }
}

/* These are input/output pixel ratios, independent of the parameter encoding. */
static NSArray *display_quality_presets(void) {
    return @[@[@"质量",@(2.0/3.0)],@[@"平衡",@(1.0/1.7)],@[@"性能",@0.5]];
}

static double display_preset_ratio(NSString *name) {
    for(NSArray *preset in display_quality_presets())
        if([name isEqual:preset[0]])return [preset[1] doubleValue];
    return 0;
}

static double display_parameter_for_ratio(double ratio,NSDictionary *state) {
    bool retina=fabs([state[@"uiScreen"][@"scale"] doubleValue]-2.0)<0.001;
    NSString *version=[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if([version isEqual:@"1.6.3"]){
        // Inverse of the original ResolutionSettings remap at RVA0xcf2815c.
        // It uses Display.systemHeight, not the output or logical window height.
        void *display=input_static(find_class("UnityEngine","Display"),"get_main",0,NULL);
        int height=object_int(display,"get_systemHeight"),width=object_int(display,"get_systemWidth");
        void *orientation=input_static(find_class("UnityEngine","Screen"),"get_orientation",0,NULL);
        int value=orientation ? *(int *)object_unbox(orientation) : 0;
        if((value==3 || value==4) && height>width)height=width;
        if(height>432 && height<=1080)
            return (432.0+648.0*(ratio*height-432.0)/(height-432.0))/1080.0;
        return ratio;
    }
    // Previously verified1.5.3 paths: direct at2x, (1 + 2 * value) / 3 at1x.
    return retina ? ratio : (3.0*ratio-1.0)/2.0;
}

static bool display_ratio_matches(NSDictionary *state,double ratio) {
    NSArray *scene=state[@"mainCamera"][@"sceneRTSize"];
    return ratio>0 && scene.count==2 &&
        fabs([scene[0] doubleValue]-[state[@"width"] doubleValue]*ratio)<=4 &&
        fabs([scene[1] doubleValue]-[state[@"height"] doubleValue]*ratio)<=4;
}

/* Screen.SetResolution may wait until the game becomes drawable again. Commit
   the original setting only after Unity reports the requested output size. */
static void display_finish_pending_resolution(void) {
    if(!display_pending_width)return;
    void *screen=find_class("UnityEngine","Screen");
    void *w=input_static(screen,"get_width",0,NULL),*h=input_static(screen,"get_height",0,NULL);
    if(w && h && *(int *)object_unbox(w)==display_pending_width && *(int *)object_unbox(h)==display_pending_height){
        game_settings_note_output(display_pending_width,display_pending_height);
        display_reapply_saved_preset();
        fprintf(output,"display_pending=verified output=%dx%d\n",display_pending_width,display_pending_height);
        display_pending_width=display_pending_height=0;display_last_result=@"已应用";
    }
}

static NSDictionary *display_feature_parameters(NSString *feature) {
    void *hub=get_hub(),*map=field_reference(field_reference(hub,"m_impl"),"m_registeredParameters");
    void *args[]={string_new(feature.UTF8String)};bool ok=false;
    void *list=invoke(map,"get_Item",1,args,&ok);
    NSMutableDictionary *values=[NSMutableDictionary dictionary];
    int count=ok && list ? object_int(list,"get_Count") : 0;
    for(int i=0;i<count && i<100;i++){
        void *index[]={&i},*parameter=invoke(list,"get_Item",1,index,&ok);
        void *name=invoke(parameter,"get_paramName",0,NULL,&ok);
        void *value=invoke(parameter,"get_paramValue",0,NULL,&ok);
        if(ok && name && value)values[managed_string(name)]=render_value(value,0);
    }
    return values;
}

#include "scene_lod.inc"

static void display_layers(id view,NSMutableArray *rows,int depth) {
    if(!view || depth>12)return;
    @try {
        id layer=[view valueForKey:@"layer"];
        if([layer isKindOfClass:CAMetalLayer.class]){
            CAMetalLayer *metal=layer;
            [rows addObject:@{@"view":NSStringFromClass([view class]),@"drawableWidth":@(metal.drawableSize.width),
                @"drawableHeight":@(metal.drawableSize.height),@"contentsScale":@(metal.contentsScale),
                @"viewContentScaleFactor":[view valueForKey:@"contentScaleFactor"] ?: NSNull.null,
                @"bounds":NSStringFromRect(metal.bounds)}];
        }
        for(id child in [view valueForKey:@"subviews"])display_layers(child,rows,depth+1);
    } @catch(NSException *exception) {}
}

static NSDictionary *display_state(void) {
    NSMutableDictionary *state=[NSMutableDictionary dictionary];
    state[@"savedUpscaler"]=[NSUserDefaults.standardUserDefaults stringForKey:display_upscaler_key] ?: @"default";
    state[@"nativeMetalFX"]=native_temporal_state();
    void *display=input_static(find_class("UnityEngine","Display"),"get_main",0,NULL);
    if(display)state[@"systemDisplaySize"]=@[@(object_int(display,"get_systemWidth")),@(object_int(display,"get_systemHeight"))];
    void *screen=find_class("UnityEngine","Screen");
    for(NSString *name in @[@"width",@"height",@"currentResolution",@"fullScreen"]){
        void *value=input_static(screen,[[@"get_" stringByAppendingString:name] UTF8String],0,NULL);
        if(value)state[name]=render_value(value,0);
    }
    state[@"parameters"]=display_feature_parameters(@"RenderTargetSettings");
    NSDictionary *taau=display_feature_parameters(@"TAAU"),*post=display_feature_parameters(@"PostProcess");
    NSMutableDictionary *sharpen=[NSMutableDictionary dictionary];
    for(NSString *key in @[@"1KSharpenStrength",@"2KSharpenStrength",@"4KSharpenStrength"])
        if(taau[key])sharpen[key]=taau[key];
    if(post[@"SharpenEnabled"])sharpen[@"postProcess"]=post[@"SharpenEnabled"];
    state[@"sharpen"]=sharpen;
    NSMutableArray *windows=[NSMutableArray array];
    for(NSWindow *window in NSApp.windows){
        if(!window.visible)continue;
        [windows addObject:@{@"frame":NSStringFromRect(window.frame),@"key":@(window.keyWindow),
            @"class":NSStringFromClass(window.class),@"styleMask":@(window.styleMask),
            @"resizable":@((window.styleMask & NSWindowStyleMaskResizable)!=0),
            @"contentSize":NSStringFromSize(window.contentView.bounds.size),
            @"contentMinSize":NSStringFromSize(window.contentMinSize),@"contentMaxSize":NSStringFromSize(window.contentMaxSize),
            @"macOSFullScreen":@((window.styleMask & NSWindowStyleMaskFullScreen)!=0)}];
    }
    state[@"hostWindows"]=windows;
    NSMutableDictionary *pacing=[NSMutableDictionary dictionary];
    const char *pacingClasses[]={"Application","QualitySettings","OnDemandRendering"};
    const char *pacingNamespaces[]={"UnityEngine","UnityEngine","UnityEngine.Rendering"};
    const char *pacingProperties[]={"targetFrameRate","vSyncCount","renderFrameInterval"};
    for(int i=0;i<3;i++){
        void *klass=find_class(pacingNamespaces[i],pacingClasses[i]);
        NSString *getter=[@"get_" stringByAppendingString:@(pacingProperties[i])];
        void *value=klass ? input_static(klass,getter.UTF8String,0,NULL) : NULL;
        if(value)pacing[@(pacingProperties[i])]=render_value(value,0);
    }
    state[@"framePacing"]=pacing;
    bool *cpp=graphics_engine_cpp_flag();
    if(cpp)state[@"cppRenderPath"]=@(*cpp);
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
    const void *tryget=typed_method(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,"UnityEngine.Camera");
    if(camera && tryget){
        int multipass=0;void *args[]={camera,&multipass},*exception=NULL;
        void *hg=runtime_invoke(tryget,NULL,args,&exception);bool ok=false;
        if(hg && !exception){
            NSMutableDictionary *main=[NSMutableDictionary dictionary];
            for(NSString *name in @[@"actualWidth",@"actualHeight",@"antialiasing",@"enableTAAU",@"enableMetalFXSpatialScaler",@"enableMetalFXTemporalScaler"]){
                void *value=invoke(hg,[[@"get_" stringByAppendingString:name] UTF8String],0,NULL,&ok);
                if(ok && value)main[name]=render_value(value,0);
            }
            for(NSString *name in @[@"sceneRTSize",@"taauRTSize"]){
                void *value=invoke(hg,[[@"get_" stringByAppendingString:name] UTF8String],0,NULL,&ok);
                if(ok && value){int *size=object_unbox(value);main[name]=@[@(size[0]),@(size[1])];}
            }
            void *path=invoke(hg,"get_renderPathInstance",0,NULL,&ok);
            if(ok && path)main[@"renderPath"]=@(class_get_name(object_get_class(path)));
            state[@"mainCamera"]=main;
        }
    }
    @try {
        id settings=[NSClassFromString(@"PlayTools.PlaySettings") valueForKey:@"shared"];
        NSMutableDictionary *play=[NSMutableDictionary dictionary];
        for(NSString *key in @[@"windowSizeWidth",@"windowSizeHeight",@"customScaler",@"adaptiveDisplay",@"inverseScreenValues",@"resizableWindow"])
            play[key]=[settings valueForKey:key] ?: NSNull.null;
        state[@"playTools"]=play;
        id app=[NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        id uiScreen=[NSClassFromString(@"UIScreen") valueForKey:@"mainScreen"];
        NSMutableDictionary *screenMetrics=[NSMutableDictionary dictionary];
        for(NSString *key in @[@"scale",@"nativeScale",@"bounds",@"nativeBounds"])
            screenMetrics[key]=[[uiScreen valueForKey:key] description] ?: @"<unavailable>";
        state[@"uiScreen"]=screenMetrics;
        NSMutableArray *layers=[NSMutableArray array];
        for(id window in [app valueForKey:@"windows"])display_layers(window,layers,0);
        state[@"metalLayers"]=layers;
    } @catch(NSException *exception) {state[@"hostInspectionError"]=exception.name;}
    return state;
}

static void display_reapply_saved_preset(void) {
    NSString *preset=[NSUserDefaults.standardUserDefaults stringForKey:display_preset_key];
    double ratio=display_preset_ratio(preset);
    if(!ratio)return;
    double scale=display_parameter_for_ratio(ratio,display_state());
    bool applied=game_settings_set_scale(scale);
    if(applied)[display_overrides removeObject:@"RenderingScale|RenderTargetSettings"];
    else {
        applied=override_parameter(get_hub(),@"RenderingScale|RenderTargetSettings",
            [NSString stringWithFormat:@"%.6f",scale],false);
        if(applied){
            if(!display_overrides)display_overrides=[NSMutableSet set];
            [display_overrides addObject:@"RenderingScale|RenderTargetSettings"];
        }
    }
    fprintf(output,"upscaler_preset=%s ratio=%.6f parameter=%.6f\n",applied ? "applied" : "failed",ratio,scale);
}

static bool display_set_unity_resolution(int width,int height) {
    void *screen=find_class("UnityEngine","Screen"),*iterator=NULL;const void *method;
    while((method=class_get_methods(screen,&iterator))){
        if(strcmp(method_get_name(method),"SetResolution") || method_get_param_count(method)!=3)continue;
        char *type=type_get_name(method_get_param(method,2));
        bool match=type && !strcmp(type,"System.Boolean");if(type)runtime_free(type);
        if(!match)continue;
        void *full=input_static(screen,"get_fullScreen",0,NULL);bool fullscreen=full && *(bool *)object_unbox(full);
        void *args[]={&width,&height,&fullscreen},*exception=NULL;
        runtime_invoke(method,NULL,args,&exception);return exception==NULL;
    }
    return false;
}

static bool display_set_resolution(int width,int height) {
    display_size_window(width,height);
    // Host layout updates the drawable. Apply the selected output after that update.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/4),dispatch_get_main_queue(),^{
        display_set_unity_resolution(width,height);
    });
    return true;
}

/* Experimental, process-local AA switch on the existing scene camera. */
static uint32_t display_aa_camera;
static int display_aa_original;
static bool display_aa_saved_cpp,display_aa_original_cpp;
static void display_upscaler(NSDictionary *command) {
    NSString *mode=command[@"mode"];
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
    int multipass=0;void *args[]={camera,&multipass};
    void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
    if(!hg){fprintf(output,"upscaler_result=no_camera\n");return;}
    bool restore=[mode isEqual:@"restore"],temporal=false;
    bool native=[mode isEqual:@"metalfx-native"],spatial=[mode isEqual:@"metalfx-spatial"];
    bool nativeUpscale=native || spatial;
    if(!restore && !temporal && !nativeUpscale && ![mode isEqual:@"taau"]){fprintf(output,"upscaler_result=invalid_mode\n");return;}
    if(temporal || nativeUpscale){
        void *supported=input_static(find_class("UnityEngine","SystemInfo"),
            spatial ? "get_supportsMetalFXSpatialScaler" : "get_supportsMetalFXTemporalScaler",0,NULL);
        if(!supported || !*(bool *)object_unbox(supported)){fprintf(output,"upscaler_result=unsupported\n");return;}
    }
    bool *cpp=graphics_engine_cpp_flag();
    if(!cpp){fprintf(output,"upscaler_result=render_path_unavailable\n");return;}
    int width=0,height=0;
    if(nativeUpscale){
        void *screen=find_class("UnityEngine","Screen");
        void *w=input_static(screen,"get_width",0,NULL),*h=input_static(screen,"get_height",0,NULL);
        if(w)width=*(int *)object_unbox(w);if(h)height=*(int *)object_unbox(h);
        if(width<1 || height<1 || width>7680 || height>4320){fprintf(output,"upscaler_result=invalid_output_size\n");return;}
        if(!native_temporal_install_size(hg)){fprintf(output,"upscaler_result=native_size_guard_failed\n");return;}
    }
    if(restore && (!display_aa_camera || gchandle_get_target(display_aa_camera)!=hg)){
        fprintf(output,"upscaler_result=no_saved_camera\n");return;
    }
    if(!restore && (!display_aa_camera || gchandle_get_target(display_aa_camera)!=hg)){
        if(display_aa_camera)gchandle_free(display_aa_camera);
        display_aa_camera=gchandle_new(hg,false);display_aa_original=object_int(hg,"get_antialiasing");
    }
    // MetalFX consumes the original TAAU inputs. Only screen-space lighting
    // requires split outputs; splitting alone breaks native snow materials.
    if((native) && !graphics_gbuffer_retention(true)){
        display_last_result=@"光影缓冲未就绪，超分模式保持原设置";
        fprintf(output,"upscaler_result=gbuffer_retention_failed\n");return;
    }
    if(native && (!override_parameter(get_hub(),@"TransientGBuffer|RenderTargetSettings",@"0",false) ||
       !override_parameter(get_hub(),@"shouldSplitOnePass|OnePassDeferred",
                           @"",true))){
        fprintf(output,"upscaler_result=split_outputs_failed\n");return;
    }
    int value=restore ? display_aa_original : (temporal ? 8 : 2);bool ok=false;
    void *setArgs[]={&value};invoke(hg,"set_antialiasing",1,setArgs,&ok);
    bool matches=ok && object_int(hg,"get_antialiasing")==value;
    if(matches && (temporal || nativeUpscale)){
        if(!display_aa_saved_cpp){display_aa_original_cpp=*cpp;display_aa_saved_cpp=true;}
        *cpp=nativeUpscale;
    }else if(matches && display_aa_saved_cpp){
        *cpp=display_aa_original_cpp;display_aa_saved_cpp=false;
    }
    if(restore && matches){gchandle_free(display_aa_camera);display_aa_camera=0;}
    if(matches){
        // An explicit menu selection ends a diagnostic bypass and resets history.
        if(atomic_exchange(&native_spatial_enabled,spatial)!=spatial || atomic_load(&native_temporal_bypass))
            atomic_fetch_add(&native_temporal_generation,1);
        atomic_store(&native_temporal_bypass,false);
        atomic_store(&native_temporal_motion_mode,0);
        native_temporal_configure(nativeUpscale,width,height);
        bool retain=native;
        managed_gbuffer_set_enabled(retain);
        if(!retain){
            override_parameter(get_hub(),@"shouldSplitOnePass|OnePassDeferred",@"",true);
            override_parameter(get_hub(),@"TransientGBuffer|RenderTargetSettings",@"",true);
            if(graphics_profile_slot)graphics_gbuffer_retention(false);
        }
        display_reapply_saved_preset();
        display_upscaler_startup_pending=false;
        if([command[@"remember"] boolValue]){
            if(restore)[NSUserDefaults.standardUserDefaults removeObjectForKey:display_upscaler_key];
            else [NSUserDefaults.standardUserDefaults setObject:mode forKey:display_upscaler_key];
        }
    }
    display_last_result=matches ? @"已应用" : @"游戏未采用画质模式";
    fprintf(output,"upscaler_result=%s mode=%s antialiasing=%d\n",matches ? "applied" : "failed",mode.UTF8String,object_int(hg,"get_antialiasing"));
}

static void display_upscaler_startup_tick(void) {
    if(!display_upscaler_startup_pending)return;
    NSString *mode=[NSUserDefaults.standardUserDefaults stringForKey:display_upscaler_key];
    if(![mode isEqual:@"metalfx-native"] &&
       ![mode isEqual:@"metalfx-spatial"] && ![mode isEqual:@"taau"]){
        display_upscaler_startup_pending=false;return;
    }
    if(!input_scene_is_ready())return;
    // Restore once in gameplay; photo capture can temporarily choose its own AA.
    display_upscaler_startup_pending=false;
    fprintf(output,"upscaler_startup=requested mode=%s\n",mode.UTF8String);
    display_upscaler(@{@"mode":mode});
}

static void display_native_temporal_tick(void) {
    if(!atomic_load(&native_temporal_enabled))return;
    void *screen=find_class("UnityEngine","Screen");
    void *w=input_static(screen,"get_width",0,NULL),*h=input_static(screen,"get_height",0,NULL);
    if(w && h)native_temporal_configure(true,*(int *)object_unbox(w),*(int *)object_unbox(h));
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
    int multipass=0;void *args[]={camera,&multipass};
    void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
    static int lastCamera,lastAA;
    int currentCamera=camera ? object_int(camera,"GetInstanceID") : 0;
    int currentAA=hg ? object_int(hg,"get_antialiasing") : -1;
    if(hg)native_temporal_install_size(hg);
    if(currentCamera!=lastCamera || currentAA!=lastAA){
        atomic_fetch_add(&native_temporal_generation,1);
        lastCamera=currentCamera;lastAA=currentAA;
    }
    // Observe camera/photo transitions without overriding the game's AA choice.
}

static void display_sharpen(NSString *mode) {
    bool original=[mode isEqual:@"original"],off=[mode isEqual:@"off"];
    if(!original && !off && ![mode isEqual:@"no-taau"]){fprintf(output,"sharpen_result=invalid_mode\n");return;}
    bool ok=true;
    for(NSString *key in @[@"1KSharpenStrength|TAAU",@"2KSharpenStrength|TAAU",@"4KSharpenStrength|TAAU"])
        ok=override_parameter(get_hub(),key,original ? nil : @"0",original) && ok;
    ok=override_parameter(get_hub(),@"SharpenEnabled|PostProcess",off ? @"0" : nil,!off) && ok;
    display_last_result=ok ? @"已应用" : @"锐化设置未完整应用";
    fprintf(output,"sharpen_result=%s mode=%s\n",ok ? "applied" : "failed",mode.UTF8String);
}

static void display_control(NSDictionary *command) {
    NSDictionary *before=display_state();
    bool hasScale=command[@"renderScale"] || command[@"preset"];
    bool mutate=command[@"width"] || hasScale || [command[@"restore"] boolValue];
    bool ok=true;
    int width=[command[@"width"] intValue],height=[command[@"height"] intValue];
    if(command[@"width"] && (width<640 || width>3840 || height<360 || height>2160))ok=false;
    double scale=[command[@"renderScale"] doubleValue];
    double ratio=0;
    if(command[@"preset"]){
        ratio=display_preset_ratio(command[@"preset"]);
        scale=display_parameter_for_ratio(ratio,before);
        if(!ratio)ok=false;
    }
    if(hasScale && (scale<0.25 || scale>1.0))ok=false;
    if(mutate && ok){
        if(!display_original_width){display_original_width=[before[@"width"] intValue];display_original_height=[before[@"height"] intValue];}
        if(!display_overrides)display_overrides=[NSMutableSet set];
        if([command[@"restore"] boolValue]){
            [NSUserDefaults.standardUserDefaults removeObjectForKey:display_preset_key];
            for(NSString *key in display_overrides)ok=override_parameter(get_hub(),key,nil,true) && ok;
            [display_overrides removeAllObjects];
            width=display_original_width;height=display_original_height;
            ok=display_set_resolution(width,height) && ok;
        }else{
            if(command[@"width"]){
                // The mobile quality table otherwise caps both targets at 1080p.
                for(NSString *key in @[@"BackBufferResolutionHeight|RenderTargetSettings",@"TAAUResolveResolutionHeight|RenderTargetSettings"]){
                    bool applied=override_parameter(get_hub(),key,[NSString stringWithFormat:@"%d",height],false);
                    if(applied)[display_overrides addObject:key];ok=ok && applied;
                }
                if(ok)ok=display_set_resolution(width,height);
            }
            if(hasScale){
                NSString *key=@"RenderingScale|RenderTargetSettings";
                if(game_settings_set_scale(scale)){
                    [display_overrides removeObject:key];
                }else{
                    bool applied=override_parameter(get_hub(),key,[NSString stringWithFormat:@"%.4f",scale],false);
                    if(applied)[display_overrides addObject:key];ok=ok && applied;
                }
            }
        }
        if(ok && width){display_pending_width=width;display_pending_height=height;}
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,mutate ? NSEC_PER_SEC : 0),dispatch_get_main_queue(),^{
        NSDictionary *state=display_state();
        bool matches=ok && (!width || ([state[@"width"] intValue]==width && [state[@"height"] intValue]==height));
        if(hasScale)matches=matches && fabs([state[@"parameters"][@"RenderingScale"] doubleValue]-scale)<0.001;
        if(ratio)matches=matches && display_ratio_matches(state,ratio);
        if(matches && hasScale && (!command[@"remember"] || [command[@"remember"] boolValue])){
            if(command[@"preset"])[NSUserDefaults.standardUserDefaults setObject:command[@"preset"] forKey:display_preset_key];
            else [NSUserDefaults.standardUserDefaults removeObjectForKey:display_preset_key];
        }
        if(mutate && width && matches)display_finish_pending_resolution();
        if(mutate)display_last_result=matches ? @"已应用" : @"游戏未采用请求尺寸";
        [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-display.json") atomically:YES];
        fprintf(output,"display_result=%s output=%dx%d\n",ok ? (matches ? "verified" : "not_applied") : "failed",[state[@"width"] intValue],[state[@"height"] intValue]);
    });
}
