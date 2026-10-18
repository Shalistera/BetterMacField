/* Preserve the existing camera output size across the mobile C++ request gate.
   ExecuteRenderRequestCPP 1.6.3 substitutes sceneRTSize for taauRTSize when
   currentDeviceType == Mobile. The original renderer still owns all targets. */
typedef void (*EFNativeSizedRender)(void *,void *,void *,void *,void *,void *);
static EFNativeSizedRender native_sized_render_original;
static void native_sized_render(void *,void *,void *,void *,void *,void *);
static bool native_temporal_install_size(void *hg);
static atomic_uint native_sized_render_calls, native_sized_render_nonmain, native_sized_render_active;
static atomic_bool native_gtao_sample_pending, native_gtao_sample_ready;
static _Atomic double native_gtao_test_until;
static atomic_uint native_gtao_test_calls;
static uint8_t native_gtao_sample[64], native_gtao_sample_mobile;
static uint32_t official_contact_camera_handle,official_contact_selector_handle;
static void *official_contact_visible_field,*official_contact_index_field;
static const void *official_contact_select_method,*official_contact_direction_method;

static bool official_contact_select_light(const uint8_t *data,float direction[4]) {
    void *selector=gchandle_get_target(official_contact_selector_handle);
    void *hg=gchandle_get_target(official_contact_camera_handle);
    if(!selector || !hg || !official_contact_select_method)return false;
    // Reuse the request's current culled lights. The private selector only
    // supplies storage for the two fields read by the original methods.
    struct {uintptr_t buffer;int length,allocator;} visible={0};
    memcpy(&visible.buffer,data+264,8);memcpy(&visible.length,data+272,4);
    if(!visible.buffer || visible.length<1)return false;
    void *exception=NULL,*args[]={&visible};
    void *value=runtime_invoke(official_contact_select_method,NULL,args,&exception);
    if(exception || !value)return false;
    int index=*(int *)object_unbox(value);if(index<0 || index>=visible.length)return false;
    field_set_value(selector,official_contact_visible_field,&visible);
    field_set_value(selector,official_contact_index_field,&index);
    void *directionArgs[]={direction,hg};
    runtime_invoke(official_contact_direction_method,selector,directionArgs,&exception);
    memset(&visible,0,sizeof visible);field_set_value(selector,official_contact_visible_field,&visible);
    if(exception)return false;
    float length=0;for(int i=0;i<3;i++){if(!isfinite(direction[i]))return false;length+=direction[i]*direction[i];}
    return length>0.5f && length<1.5f;
}

static bool native_temporal_owns_render(uintptr_t entry,const uint8_t *base) {
    return entry==(uintptr_t)native_sized_render &&
        (uintptr_t)native_sized_render_original==(uintptr_t)base+(probe_cn_client ? 0x126fe400 : 0x126aead0);
}

static void native_sized_render(void *owner,void *parameters,void *culling,void *camera,void *context,void *command) {
    atomic_fetch_add(&native_sized_render_calls,1);
    if(!NSThread.isMainThread)atomic_fetch_add(&native_sized_render_nonmain,1);
    atomic_fetch_add(&native_sized_render_active,1);
    uint8_t *data=parameters;uint64_t saved=0,full=0;
    // Observe the actual C++ request, after the managed camera/Volume gates.
    // The bounded test only opens the existing native GTAO pass and restores its
    // request flags before returning; it never changes device classification.
    uint8_t *gtao=NULL,gtaoFlags[4]={0},settings[64];
    double gtaoUntil=atomic_load(&native_gtao_test_until);
    bool gtaoTest=gtaoUntil>0 && CFAbsoluteTimeGetCurrent()<gtaoUntil;
    if(data && (gtaoTest || atomic_load(&native_gtao_sample_pending) || atomic_load(&metal_screen_ao_strength)>0 || atomic_load(&metal_native_contact_strength)>0)){
        memcpy(&gtao,data+432,sizeof gtao);
        if(gtao && graphics_native_read((uintptr_t)gtao,settings,sizeof settings)){
            int outputSize[2];memcpy(outputSize,data+240,8);
            official_gtao_request(settings,outputSize,camera);
            if(atomic_exchange(&native_gtao_sample_pending,false)){
                memcpy(native_gtao_sample,settings,sizeof settings);
                native_gtao_sample_mobile=data[360];
                atomic_store(&native_gtao_sample_ready,true);
            }
            if(gtaoTest){
                memcpy(gtaoFlags,gtao,sizeof gtaoFlags);
                gtao[0]=1;gtao[3]=1;
                atomic_fetch_add(&native_gtao_test_calls,1);
            }
        }else gtaoTest=false;
    }else gtaoTest=false;
    if(data && atomic_load(&metal_native_contact_strength)>0){
        uintptr_t phase=0,config=0;uint8_t values[20];float direction[4]={0};
        memcpy(&phase,data+168,8);
        bool valid=graphics_native_read(phase+104,&config,8) && graphics_native_read(config,values,sizeof values) &&
            official_contact_select_light(data,direction);
        @synchronized(metal_ao_lock){
            official_contact_request_valid=valid;
            if(valid){memcpy(official_contact_config,values,sizeof values);memcpy(official_contact_direction,direction,sizeof direction);}
        }
    }
    bool change=atomic_load(&native_temporal_enabled) && !atomic_load(&native_spatial_enabled) && data && data[56];
    if(change){
        memcpy(&saved,data+232,8);memcpy(&full,data+240,8);
        int dimensions[2];memcpy(dimensions,&full,8);
        change=dimensions[0]>0 && dimensions[1]>0 && dimensions[0]<=7680 && dimensions[1]<=4320;
        if(change){memcpy(data+232,&full,8);atomic_fetch_add(&native_temporal_fullres_calls,1);}
    }
    native_sized_render_original(owner,parameters,culling,camera,context,command);
    if(gtaoTest)memcpy(gtao,gtaoFlags,sizeof gtaoFlags);
    if(change)memcpy(data+232,&saved,8);
    atomic_fetch_sub(&native_sized_render_active,1);
}

static void native_gtao_control(NSDictionary *command) {
    if(command[@"seconds"] || [command[@"sample"] boolValue]){
        void *klass=find_class("UnityEngine.HyperGryphEngineCode","HGRenderPathParamsCPP");
        void *field=klass ? class_get_field_from_name(klass,"gtaoSettingParameters") : NULL;
        void *settings=find_class("UnityEngine.HyperGryphEngineCode","HGGTAmbientOcclusionSettingParameters");
        uint32_t alignment=0;
        if(!field || field_get_offset(field)!=448 || !settings || class_value_size(settings,&alignment)!=64){
            fprintf(output,"native_gtao=layout_mismatch\n");return;
        }
        int multipass=0;
        void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
        void *args[]={camera,&multipass};
        void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
        if(!hg || !native_temporal_install_size(hg)){fprintf(output,"native_gtao=render_unavailable\n");return;}
        if(official_contact_camera_handle)gchandle_free(official_contact_camera_handle);
        official_contact_camera_handle=gchandle_new(hg,false);
        if(command[@"seconds"]){
            double seconds=MAX(0,MIN(30,[command[@"seconds"] doubleValue]));
            atomic_store(&native_gtao_test_until,seconds ? CFAbsoluteTimeGetCurrent()+seconds : 0);
            atomic_store(&native_gtao_test_calls,0);
        }
        atomic_store(&native_gtao_sample_ready,false);
        atomic_store(&native_gtao_sample_pending,true);
    }
    NSMutableDictionary *result=[@{@"testRemaining":@(MAX(0,atomic_load(&native_gtao_test_until)-CFAbsoluteTimeGetCurrent())),
        @"testCalls":@(atomic_load(&native_gtao_test_calls)),@"sampleReady":@(atomic_load(&native_gtao_sample_ready))} mutableCopy];
    if(atomic_load(&native_gtao_sample_ready)){
        result[@"renderPathMobile"]=@(native_gtao_sample_mobile);
        const char *flags[]={"enableAO","enableFP32Depths","enableBentNormals","generateNormalsInplace"};
        for(int i=0;i<4;i++)result[@(flags[i])]=@(native_gtao_sample[i]);
        const char *ints[]={"screenWidth","screenHeight","qualityLevel","denoisePasses","cameraFrameCount"};
        for(int i=0;i<5;i++){int v;memcpy(&v,native_gtao_sample+4+i*4,4);result[@(ints[i])]=@(v);}
        const char *floats[]={"finalValuePower","depthMIPSamplingOffset","radius","radiusMultiplier","falloffRange","sampleDistributionPower","thinOccluderCompensation","mvFactor","depthFactor"};
        const int offsets[]={24,28,36,40,44,48,52,56,60};
        for(int i=0;i<9;i++){float v;memcpy(&v,native_gtao_sample+offsets[i],4);if(isfinite(v))result[@(floats[i])]=@(v);}
        result[@"useCustomSettings"]=@(native_gtao_sample[32]);
    }
    NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
    [json writeToFile:runtime_path(@"endfield-ios-native-gtao.json") atomically:YES];
    fprintf(output,"native_gtao=%s\n",[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
}

static bool official_contact_layout_valid(void) {
    const char *classes[]={"HGRenderPathParamsCPP","HGEnvironmentPhaseCPP",
        "HGContactShadowConfigCPP","HGContactShadowConfigCPP","HGContactShadowConfigCPP"};
    const char *fields[]={"envPhase","contactShadowConfig","ignoreEdgePixels","intensity","contract"};
    const int offsets[]={184,120,16,20,32};
    for(int i=0;i<5;i++){
        void *klass=find_class("UnityEngine.HyperGryphEngineCode",classes[i]);
        void *field=klass ? class_get_field_from_name(klass,fields[i]) : NULL;
        if(!field || field_get_offset(field)!=offsets[i])return false;
    }
    void *request=find_class("UnityEngine.HyperGryphEngineCode","HGRenderPathParamsCPP");
    const char *requestNames[]={"visibleLights","visibleLightCount"};
    for(int i=0;i<2;i++){
        void *field=class_get_field_from_name(request,requestNames[i]);
        if(!field || field_get_offset(field)!=(i ? 288 : 280))return false;
    }
    void *klass=find_class("HG.Rendering.Runtime","LightClusteringPassConstructor");
    if(!klass)return false;
    official_contact_visible_field=class_get_field_from_name(klass,"m_visibleLights");
    official_contact_index_field=class_get_field_from_name(klass,"m_directionalLightIndex");
    if(!official_contact_visible_field || field_get_offset(official_contact_visible_field)!=16 ||
       !official_contact_index_field || field_get_offset(official_contact_index_field)!=84)return false;
    official_contact_select_method=class_get_method_from_name(klass,"GetDirectionalLightIndex",1);
    official_contact_direction_method=class_get_method_from_name(klass,"TryGetDirectionalLightDir",2);
    const void *methods[]={official_contact_select_method,official_contact_direction_method};
    const uintptr_t addresses[]={probe_cn_client ? 0xd21c48c : 0xd21c494,probe_cn_client ? 0xd21c5ec : 0xd21c5f4};
    for(int i=0;i<2;i++){
        Dl_info info={0};const void *code=methods[i] ? *(const void **)methods[i] : NULL;
        if(!code || !dladdr(code,&info) || (uintptr_t)code-(uintptr_t)info.dli_fbase!=addresses[i])return false;
    }
    if(!official_contact_selector_handle){
        void *selector=object_new(klass);if(!selector)return false;
        official_contact_selector_handle=gchandle_new(selector,false);
    }
    return true;
}

static bool native_temporal_install_size(void *hg) {
    void *field=hg ? class_get_field_from_name(object_get_class(hg),"renderPathInstanceCPP") : NULL;
    if(!field || field_get_offset(field)!=1808)return false;
    uintptr_t path=0,wrapper[4]={0};field_get_value(hg,field,&path);
    if(!graphics_native_read(path,wrapper,sizeof wrapper))return false;
    if(wrapper[2]==(uintptr_t)native_sized_render)return true;
    Dl_info info={0};
    const uint8_t expected[]={0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,0xf6,0x57,0x03,0xa9};
    uintptr_t expectedRVA=probe_cn_client ? 0x126fe400 : 0x126aead0;
    if(!dladdr((void *)wrapper[2],&info) || wrapper[2]!=(uintptr_t)info.dli_fbase+expectedRVA ||
       memcmp((void *)wrapper[2],expected,sizeof expected))return false;
    void *klass=find_class("UnityEngine.HyperGryphEngineCode","HGRenderPathParamsCPP");
    const char *names[]={"enableTAAU","sceneRTSize","taauRTSize","finalRTSize"};
    const int offsets[]={72,232,248,256};
    for(int i=0;i<4;i++){
        void *f=klass ? class_get_field_from_name(klass,names[i]) : NULL;
        if(!f || field_get_offset(f)!=offsets[i])return false;
    }
    native_sized_render_original=(EFNativeSizedRender)wrapper[2];
    *(void **)(path+16)=(void *)native_sized_render;
    fprintf(output,"native_temporal_size=installed rva=0x%lx\n",expectedRVA);
    return true;
}
