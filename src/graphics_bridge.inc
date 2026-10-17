static bool *graphics_engine_cpp_flag(void) {
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    void *native=resolve ? resolve("UnityEngine.HyperGryph.HGGraphicsUtils::get_enableCppRenderPath()") : NULL;
    Dl_info info;
    if(!native || !dladdr(native,&info))return NULL;
    uint8_t *base=info.dli_fbase;
    uintptr_t rva=(uintptr_t)native-(uintptr_t)base;
    if(rva==0x117a9e08) {
        /* 1.6.3 getter: ADRP/LDRB from the verified writable __DATA byte. */
        const uint8_t wrapper[]={0xfd,0x7b,0xbf,0xa9,0xfd,0x03,0x00,0x91,0xee,0x81,0x28,0x94,0xfd,0x7b,0xc1,0xa8,0xc0,0x03,0x5f,0xd6};
        const uint8_t getter[]={0x08,0xa5,0x02,0xf0,0x00,0x65,0x52,0x39,0xc0,0x03,0x5f,0xd6};
        if(memcmp(native,wrapper,sizeof wrapper) || memcmp(base+0x121ca5c8,getter,sizeof getter))return NULL;
        bool *flag=(bool *)(base+0x1766d499);
        return *(uint8_t *)flag<=1 ? flag : NULL;
    }
    if(rva!=0xdda3d68)return NULL;
    const uint8_t wrapper[]={0xfd,0x7b,0xbf,0xa9,0xfd,0x03,0x00,0x91,0x99,0x09,0x27,0x94,0xfd,0x7b,0xc1,0xa8,0xc0,0x03,0x5f,0xd6};
    const uint8_t getter[]={0xc8,0x73,0x02,0xb0,0x00,0xe5,0x4d,0x39,0xc0,0x03,0x5f,0xd6};
    if(memcmp(native,wrapper,sizeof wrapper) || memcmp(base+0xe7663d4,getter,sizeof getter))return NULL;
    // Validated __DATA vmaddr=0x12928000, vmsize=0x1234000, initprot=RW.
    bool *flag=(bool *)(base+0x135df379);
    return *(uint8_t *)flag<=1 ? flag : NULL;
}

#include <mach/mach.h>
#include <mach/mach_vm.h>
static bool graphics_native_read(uintptr_t address,void *target,size_t size) {
    mach_vm_size_t copied=0;
    return address && mach_vm_read_overwrite(mach_task_self(),address,size,
        (mach_vm_address_t)target,&copied)==KERN_SUCCESS && copied==size;
}

typedef int (*GBufferProfileFunc)(void *,const void *);
static GBufferProfileFunc graphics_profile_original;
static void **graphics_profile_slot;
static int graphics_retained_profile(void *path,const void *method) {
    int profile=graphics_profile_original(path,method),memoryless=0;
    void *manager=field_reference(path,"m_gBufferProfileMgr");
    bool ok=false;void *args[]={&profile,&memoryless};
    if(manager)invoke_typed(manager,"SetGBufferTextureMemoryless",2,
        "HG.Rendering.Runtime.GBufferProfileManager.GBufferProfileConfig",args,&ok);
    return profile;
}

static bool graphics_gbuffer_retention(bool enabled) {
    if(!enabled) {
        bool restored=!graphics_profile_slot || *graphics_profile_slot==(void *)graphics_retained_profile;
        if(graphics_profile_slot && restored)*graphics_profile_slot=(void *)graphics_profile_original;
        if(restored)graphics_profile_slot=NULL;
        fprintf(output,"gbuffer_retention=disabled restored=%d\n",restored);return restored;
    }
    if(graphics_profile_slot)return true;
    void *klass=find_class("HG.Rendering.Runtime","HGRenderPathOnePassDeferred");
    if(!klass)return false;
    void **slot=(void **)((uint8_t *)klass+0x260);
    const void *method=slot[1];void *pointer=slot[0];Dl_info info={0};
    const uint8_t expected[]={0x68,0x4b,0x05,0xf0,0x08,0x61,0x0f,0x91,0x08,0x01,0x40,0xf9,0x08,0x41,0x40,0xf9};
    if(!pointer || !method || !dladdr(pointer,&info) ||
       (uintptr_t)pointer-(uintptr_t)info.dli_fbase!=0xcef7174 || memcmp(pointer,expected,16) ||
       strcmp(method_get_name(method),"GetGBufferProfileConfig") || method_get_param_count(method)!=0) {
        fprintf(output,"gbuffer_retention=identity_mismatch\n");return false;
    }
    graphics_profile_original=(GBufferProfileFunc)pointer;graphics_profile_slot=slot;
    *slot=(void *)graphics_retained_profile;
    fprintf(output,"gbuffer_retention=enabled\n");return true;
}
