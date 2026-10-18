/* Follow the AO TextureHandle supplied to the original deferred lighting delegate. */
typedef void (*AOLightingFunc)(void *,void *,void *,const void *);
static AOLightingFunc ao_lighting_original;
static uint32_t ao_lighting_handle;
static void *ao_registry_class;
static const void *ao_registry_get_texture;
static NSMutableDictionary *ao_native_cache;
static atomic_uint ao_source_calls,ao_source_updates,ao_source_failures;

static void ao_lighting_source(void *target,void *data,void *context,const void *method) {
    if(atomic_load(&metal_ao_automatic)) {
        atomic_fetch_add(&ao_source_calls,1);
        id<MTLTexture> texture=nil;
        void *field=data ? class_get_field_from_name(object_get_class(data),"indirectAmbientOcclusionTexture") : NULL;
        if(field && field_get_offset(field)==80) {
            uint64_t handle[2]={0};field_get_value(data,field,handle);
            void *registry=input_static(ao_registry_class,"get_current",0,NULL);
            void *args[]={handle},*exception=NULL;
            void *rtHandle=registry ? runtime_invoke(ao_registry_get_texture,registry,args,&exception) : NULL;
            void *source=rtHandle && !exception ? field_reference(rtHandle,"m_RT") : NULL;
            if(source) {
                NSNumber *key=@(object_int(source,"GetInstanceID"));
                texture=ao_native_cache[key];
                if(!texture && [object_name(source) hasPrefix:@"GT Ambient Occlusion_"]) {
                    bool ok=false;void *native=invoke(source,"GetNativeTexturePtr",0,NULL,&ok);
                    uintptr_t pointer=ok && native ? *(uintptr_t *)object_unbox(native) : 0;
                    texture=pointer ? (__bridge id<MTLTexture>)(void *)pointer : nil;
                    if(texture && texture.pixelFormat==MTLPixelFormatR8Unorm) {
                        if(ao_native_cache.count>=4)[ao_native_cache removeAllObjects];
                        ao_native_cache[key]=texture;atomic_fetch_add(&ao_source_updates,1);
                    } else texture=nil;
                }
            }
        }
        if(!texture)atomic_fetch_add(&ao_source_failures,1);
        @synchronized(metal_ao_lock){metal_ao_texture=texture;}
    }
    ao_lighting_original(target,data,context,method);
}

static bool ao_source_install(void) {
    if(ao_lighting_handle)return true;
    void *klass=find_class("HG.Rendering.Runtime","DeferredLightingPassConstructor");
    void *field=klass ? class_get_field_from_name(klass,"s_fromDeferredLightingToForwardOpaqueRenderFunc") : NULL;
    void *delegate=field ? field_get_value_object(field,NULL) : NULL;
    void *pointer=NULL,*invokePointer=NULL;Dl_info info={0};
    void *pointerField=delegate ? class_get_field_from_name(object_get_class(delegate),"method_ptr") : NULL;
    void *invokeField=delegate ? class_get_field_from_name(object_get_class(delegate),"invoke_impl") : NULL;
    if(pointerField)field_get_value(delegate,pointerField,&pointer);
    if(invokeField)field_get_value(delegate,invokeField,&invokePointer);
    const uint8_t expected[]={0xff,0x43,0x05,0xd1,0xfc,0x6f,0x0f,0xa9,0xfa,0x67,0x10,0xa9,0xf8,0x5f,0x11,0xa9};
    if(!pointer || pointer!=invokePointer || !dladdr(pointer,&info) ||
       (uintptr_t)pointer-(uintptr_t)info.dli_fbase!=0x9d8efa8 || memcmp(pointer,expected,sizeof expected))return false;
    ao_registry_class=find_class("HG.Rendering.RenderGraphModule","HGRenderGraphResourceRegistry");
    ao_registry_get_texture=typed_method(ao_registry_class,"GetTexture",1,"HG.Rendering.RenderGraphModule.TextureHandle&");
    if(!ao_registry_get_texture)return false;
    char *result=type_get_name(method_get_return_type(ao_registry_get_texture));
    bool typeOK=result && !strcmp(result,"UnityEngine.Rendering.RTHandle");
    if(result)runtime_free(result);if(!typeOK)return false;
    ao_lighting_original=(AOLightingFunc)pointer;ao_native_cache=[NSMutableDictionary dictionary];
    ao_lighting_handle=gchandle_new(delegate,false);
    void *replacement=(void *)ao_lighting_source;
    field_set_value(delegate,pointerField,&replacement);field_set_value(delegate,invokeField,&replacement);
    return true;
}

static bool ao_source_restore(void) {
    if(!ao_lighting_handle)return true;
    void *delegate=gchandle_get_target(ao_lighting_handle);
    for(const char **name=(const char*[]){"method_ptr","invoke_impl",NULL};*name;name++) {
        void *field=class_get_field_from_name(object_get_class(delegate),*name),*actual=NULL;
        field_get_value(delegate,field,&actual);
        if(actual!=(void *)ao_lighting_source)return false;
    }
    void *original=(void *)ao_lighting_original;
    set_field(delegate,"method_ptr",&original);set_field(delegate,"invoke_impl",&original);
    gchandle_free(ao_lighting_handle);ao_lighting_handle=0;ao_native_cache=nil;return true;
}
