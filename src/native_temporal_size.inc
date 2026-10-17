/* Preserve the existing camera output size across the mobile C++ request gate.
   ExecuteRenderRequestCPP 1.6.3 substitutes sceneRTSize for taauRTSize when
   currentDeviceType == Mobile. The original renderer still owns all targets. */
typedef void (*EFNativeSizedRender)(void *,void *,void *,void *,void *,void *);
static EFNativeSizedRender native_sized_render_original;
static void native_sized_render(void *,void *,void *,void *,void *,void *);
static atomic_uint native_sized_render_calls, native_sized_render_nonmain, native_sized_render_active;

static bool native_temporal_owns_render(uintptr_t entry,const uint8_t *base) {
    return entry==(uintptr_t)native_sized_render &&
        (uintptr_t)native_sized_render_original==(uintptr_t)base+0x126aead0;
}

static void native_sized_render(void *owner,void *parameters,void *culling,void *camera,void *context,void *command) {
    atomic_fetch_add(&native_sized_render_calls,1);
    if(!NSThread.isMainThread)atomic_fetch_add(&native_sized_render_nonmain,1);
    atomic_fetch_add(&native_sized_render_active,1);
    uint8_t *data=parameters;uint64_t saved=0,full=0;
    bool change=atomic_load(&native_temporal_enabled) && !atomic_load(&native_spatial_enabled) && data && data[56];
    if(change){
        memcpy(&saved,data+232,8);memcpy(&full,data+240,8);
        int dimensions[2];memcpy(dimensions,&full,8);
        change=dimensions[0]>0 && dimensions[1]>0 && dimensions[0]<=7680 && dimensions[1]<=4320;
        if(change){memcpy(data+232,&full,8);atomic_fetch_add(&native_temporal_fullres_calls,1);}
    }
    native_sized_render_original(owner,parameters,culling,camera,context,command);
    if(change)memcpy(data+232,&saved,8);
    atomic_fetch_sub(&native_sized_render_active,1);
}

static bool native_temporal_install_size(void *hg) {
    void *field=hg ? class_get_field_from_name(object_get_class(hg),"renderPathInstanceCPP") : NULL;
    if(!field || field_get_offset(field)!=1808)return false;
    uintptr_t path=0,wrapper[4]={0};field_get_value(hg,field,&path);
    if(!graphics_native_read(path,wrapper,sizeof wrapper))return false;
    if(wrapper[2]==(uintptr_t)native_sized_render)return true;
    Dl_info info={0};
    const uint8_t expected[]={0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,0xf6,0x57,0x03,0xa9};
    if(!dladdr((void *)wrapper[2],&info) || wrapper[2]!=(uintptr_t)info.dli_fbase+0x126aead0 ||
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
    fprintf(output,"native_temporal_size=installed rva=0x126aead0\n");
    return true;
}
