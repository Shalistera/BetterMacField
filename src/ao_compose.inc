/* AO binding follows the active render-graph lighting input; explicit binding is diagnostic. */
static void ao_compose(NSDictionary *command) {
    if(command[@"captureOfficialFrames"]){
        @synchronized(metal_ao_lock){
            official_gtao_capture_index=0;
            atomic_store(&official_gtao_capture_frames,MIN(2,MAX(0,[command[@"captureOfficialFrames"] intValue])));
        }
    }
    if([command[@"sampleOfficial"] boolValue])atomic_store(&official_gtao_sample_pending,true);
    if([command[@"sampleOfficialContact"] boolValue])atomic_store(&official_contact_sample_pending,true);
    if(command[@"legacyPattern"])atomic_store(&metal_screen_ao_legacy_pattern,[command[@"legacyPattern"] boolValue]);
    if(command[@"screenSpaceDebug"])atomic_store(&metal_screen_ao_debug,MAX(0,MIN(2,[command[@"screenSpaceDebug"] intValue])));
    if(command[@"screenSpace"] && ![command[@"screenSpace"] boolValue])atomic_store(&metal_screen_ao_debug,0);
    if(command[@"nativeContactDebug"])atomic_store(&metal_native_contact_debug,MAX(0,MIN(2,[command[@"nativeContactDebug"] intValue])));
    if(command[@"nativeContact"])
        atomic_store(&metal_native_contact_strength,[command[@"nativeContact"] boolValue] ?
            fmaxf(0,fminf(1,command[@"contactStrength"] ? [command[@"contactStrength"] floatValue] : 1)) : 0);
    if(command[@"nativeContact"] && ![command[@"nativeContact"] boolValue])
        atomic_store(&metal_native_contact_debug,0);
    if(command[@"contactLength"])
        atomic_store(&metal_native_contact_length,fmaxf(0.05f,fminf(2,[command[@"contactLength"] floatValue])));
    if(command[@"screenSpace"])
        atomic_store(&metal_screen_ao_strength,[command[@"screenSpace"] boolValue] ?
            fmaxf(0,fminf(2,command[@"strength"] ? [command[@"strength"] floatValue] : 1)) : 0);
    if(command[@"radius"])
        atomic_store(&metal_screen_ao_radius,fmaxf(0.1f,fminf(3,[command[@"radius"] floatValue])));
    if(command[@"enabled"] && ![command[@"enabled"] boolValue]) {
        atomic_store(&metal_screen_ao_strength,0);
        atomic_store(&metal_screen_ao_debug,0);
        atomic_store(&metal_native_contact_strength,0);
        atomic_store(&metal_native_contact_debug,0);
        atomic_store(&metal_ao_automatic,false);
        @synchronized(metal_ao_lock){metal_ao_texture=nil;}
    } else if([command[@"automatic"] boolValue] && metal_ao_white) {
        atomic_store(&metal_ao_automatic,true);
    } else if(command[@"instance"]) {
        atomic_store(&metal_ao_automatic,false);
        int instance=[command[@"instance"] intValue];bool ok=false;
        void *klass=find_class("UnityEngine","RenderTexture"),*source=NULL;
        void *args[]={type_get_object(class_get_type(klass))};
        void *all=input_static(find_class("UnityEngine","Resources"),"FindObjectsOfTypeAll",1,args);
        for(uintptr_t i=0;all && i<array_length(all);i++) {
            void *entry=array_item(all,i);
            if(object_int(entry,"GetInstanceID")==instance){source=entry;break;}
        }
        NSString *name=source ? object_name(source) : @"";
        if(![name hasPrefix:@"GT Ambient Occlusion_"] || ![name containsString:@"R8_UNorm"] || !metal_ao_white) {
            fprintf(output,"ao_compose=invalid_target\n");return;
        }
        void *native=invoke(source,"GetNativeTexturePtr",0,NULL,&ok);
        uintptr_t pointer=ok && native ? *(uintptr_t *)object_unbox(native) : 0;
        id<MTLTexture> texture=pointer ? (__bridge id<MTLTexture>)(void *)pointer : nil;
        if(!texture || texture.pixelFormat!=MTLPixelFormatR8Unorm || texture.width<2 || texture.height<2) {
            fprintf(output,"ao_compose=native_texture_unavailable\n");return;
        }
        @synchronized(metal_ao_lock){metal_ao_texture=texture;}
    }
    id<MTLTexture> texture;NSString *gtaoReason;
    @synchronized(metal_ao_lock){texture=metal_ao_texture;gtaoReason=official_gtao_reason;}
    NSDictionary *state=@{@"enabled":@(texture!=nil),@"compiled":@(atomic_load(&metal_ao_compiled)),
        @"officialGTAO":gtaoReason,@"officialEncodes":@(atomic_load(&official_gtao_encodes)),
        @"officialContact":official_contact_reason,@"officialContactEncodes":@(atomic_load(&official_contact_encodes)),
        @"officialContactMissing":@(atomic_load(&official_contact_missing)),@"officialContactErrors":@(atomic_load(&official_contact_errors)),
        @"officialEnabled":@(atomic_load(&metal_screen_ao_strength)>0),
        @"officialMissing":@(atomic_load(&official_gtao_missing)),@"officialErrors":@(atomic_load(&official_gtao_errors)),
        @"screenSpaceStrength":@(atomic_load(&metal_screen_ao_strength)),@"screenSpaceRadius":@(atomic_load(&metal_screen_ao_radius)),
        @"screenSpaceDraws":@(atomic_load(&metal_screen_ao_draws)),
        @"screenSpaceDebug":@(atomic_load(&metal_screen_ao_debug)),
        @"legacyPattern":@(atomic_load(&metal_screen_ao_legacy_pattern)),
        @"nativeContactStrength":@(atomic_load(&metal_native_contact_strength)),
        @"nativeContactLength":@(atomic_load(&metal_native_contact_length)),
        @"nativeContactDraws":@(atomic_load(&metal_native_contact_draws)),
        @"nativeContactDebug":@(atomic_load(&metal_native_contact_debug)),
        @"automatic":@(atomic_load(&metal_ao_automatic)),
        @"sourceCalls":@(atomic_load(&ao_source_calls)),@"sourceUpdates":@(atomic_load(&ao_source_updates)),
        @"sourceFailures":@(atomic_load(&ao_source_failures)),@"sourceHook":@(ao_lighting_handle!=0),
        @"candidates":@(metal_ao_patches.count),@"whiteReady":@(metal_ao_white!=nil),
        @"draws":@(atomic_load(&metal_ao_draws)),@"boundDraws":@(atomic_load(&metal_ao_bound_draws)),
        @"texture":texture.label ?: @"white",@"width":@(texture.width),@"height":@(texture.height)};
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:runtime_path(@"endfield-ios-ao-compose.json") atomically:YES];
    fprintf(output,"ao_compose=ready enabled=%d compiled=%u draws=%u bound=%u\n",texture!=nil,
        atomic_load(&metal_ao_compiled),atomic_load(&metal_ao_draws),atomic_load(&metal_ao_bound_draws));
}

static bool native_lighting_any(void) {
    return atomic_load(&metal_screen_ao_strength)>0 || atomic_load(&metal_native_contact_strength)>0 ||
        atomic_load(&metal_ssr_history_enabled);
}

static bool native_reflection_available(void) {
    for(NSDictionary *patch in metal_ao_patches.allValues)
        if([patch[@"reflection"] boolValue])return true;
    return false;
}

static bool native_lighting_ready(void) {
    bool *engine=graphics_engine_cpp_flag(),ok=false;
    if(!engine || !metal_ao_white || !input_scene_is_loaded())return false;
    void *graph=invoke(graphics_pipeline(),"get_renderGraph",0,NULL,&ok);
    void *cpp=ok && graph ? invoke(graph,"get_enableCppRenderPath",0,NULL,&ok) : NULL;
    if(!ok || !cpp || *(bool *)object_unbox(cpp)!=*engine)return false;
    int multipass=0;
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL),*args[]={camera,&multipass};
    void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
    void *path=hg ? invoke(hg,"get_renderPathInstance",0,NULL,&ok) : NULL;
    if(!path || strcmp(class_get_name(object_get_class(path)),"HGRenderPathOnePassDeferred"))return false;
    void *mobile=invoke(get_hub(),"get_isMobileShader",0,NULL,&ok);
    return ok && mobile && *(bool *)object_unbox(mobile);
}

/* Menu and CLI share split outputs across native and MetalFX managed rendering. */
static bool native_lighting_control(NSDictionary *command) {
    if([command[@"reflection"] boolValue] && !native_reflection_available()) {
        fprintf(output,"native_lighting=reflection_unavailable\n");return false;
    }
    bool enabling=[command[@"screenSpace"] boolValue] || [command[@"nativeContact"] boolValue] ||
        [command[@"reflection"] boolValue];
    bool wasActive=native_lighting_any(),ok=true;
    void *hub=get_hub();
    if(enabling) {
        if(!native_lighting_ready()) {
            fprintf(output,"native_lighting=unavailable\n");return false;
        }
        if([command[@"screenSpace"] boolValue]){
            if(!official_gtao_prepare(metal_ao_white.device)){
                fprintf(output,"native_lighting=official_gtao_unavailable %s\n",official_gtao_reason.UTF8String);return false;
            }
            native_gtao_control(@{@"sample":@YES});
            @synchronized(metal_ao_lock){official_gtao_reset++;}
        }
        if([command[@"nativeContact"] boolValue]){
            if(!official_contact_layout_valid() || !official_contact_prepare(metal_ao_white.device)){
                fprintf(output,"native_lighting=official_contact_unavailable %s\n",official_contact_reason.UTF8String);return false;
            }
            native_gtao_control(@{@"sample":@YES});
        }
        bool managed=!*graphics_engine_cpp_flag();
        if(!graphics_gbuffer_retention(true)) {
            fprintf(output,"native_lighting=gbuffer_retention_failed\n");return false;
        }
        managed_gbuffer_set_enabled(true);
        atomic_store(&native_decal_depth_enabled,!managed);
        ok=override_parameter(hub,@"TransientGBuffer|RenderTargetSettings",@"0",false);
        if(ok)ok=override_parameter(hub,@"shouldSplitOnePass|OnePassDeferred",@"1",false);
        if(!ok) {
            if(!wasActive && !atomic_load(&native_temporal_enabled)) {
                override_parameter(hub,@"shouldSplitOnePass|OnePassDeferred",@"",true);
                override_parameter(hub,@"TransientGBuffer|RenderTargetSettings",@"",true);
                managed_gbuffer_set_enabled(false);
                atomic_store(&native_decal_depth_enabled,false);
                if(graphics_profile_slot)graphics_gbuffer_retention(false);
            }
            fprintf(output,"native_lighting=parameter_failed\n");return false;
        }
    }
    NSMutableDictionary *settings=[command mutableCopy];
    settings[@"nativeContactDebug"]=@0;
    if(command[@"reflection"]) {
        bool enabled=[command[@"reflection"] boolValue];
        metal_ssr_history_control(@{@"enabled":@(enabled),@"strength":@(enabled ? 0.65 : 0),@"steps":@32,@"debug":@0});
    }
    ao_compose(settings);
    if(!native_lighting_any()) {
        ok=override_parameter(hub,@"shouldSplitOnePass|OnePassDeferred",@"",true);
        atomic_store(&native_decal_depth_enabled,false);
        if(!atomic_load(&native_temporal_enabled)) {
            ok=override_parameter(hub,@"TransientGBuffer|RenderTargetSettings",@"",true) && ok;
            if(ok) {
                managed_gbuffer_set_enabled(false);
                if(graphics_profile_slot)ok=graphics_gbuffer_retention(false);
            }
        }
    }
    fprintf(output,"native_lighting=%s ao=%d contact=%d reflection=%d\n",ok ? "ready" : "restore_failed",
        atomic_load(&metal_screen_ao_strength)>0,atomic_load(&metal_native_contact_strength)>0,
        atomic_load(&metal_ssr_history_enabled));
    return ok;
}

static bool ao_requested;
static unsigned ao_request_generation;
static NSString *ao_last_result=@"AO：关闭";

