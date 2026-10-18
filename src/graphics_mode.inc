/* Process-local rendering classification and graph experiments. */
static bool graphics_saved_device, graphics_saved_cpp;
static int graphics_original_device;
static bool graphics_original_cpp;
static bool graphics_saved_debug,graphics_original_debug;
static uint32_t graphics_culling_handle;
static bool graphics_original_disable_culling;
static bool graphics_saved_defines;
static uint32_t graphics_original_defines[6];
static bool graphics_saved_lod;
static int graphics_original_lod;
static NSMutableArray *graphics_saved_gbuffer_formats;
static uint32_t graphics_subpass_material_handle;
static bool graphics_original_subpass_keyword;
static uint32_t graphics_iv_switch_handle;
static bool graphics_original_iv_switch;
static const char *graphics_subpass_keyword="HG_USE_SUBPASS_INPUT_UNDER_ONE_PASS_DEFERRED";

static void *graphics_lighting_material(void) {
    bool ok=false;int multipass=0;
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
    void *args[]={camera,&multipass};
    void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
    void *path=hg ? invoke(hg,"get_renderPathInstance",0,NULL,&ok) : NULL;
    if(!path)return NULL;
    // OnePass inherits the default constructor too; select the one used by this path.
    bool onePass=!strcmp(class_get_name(object_get_class(path)),"HGRenderPathOnePassDeferred");
    void *constructor=field_reference(path,onePass ? "m_onePassDeferredPassConstructor" : "m_deferredLightingPassConstructor");
    return constructor ? field_reference(constructor,"m_deferredLightingMaterial") : NULL;
}

static bool graphics_set_subpass_keyword(bool enabled,bool restore) {
    void *material=restore ? gchandle_get_target(graphics_subpass_material_handle) : graphics_lighting_material();
    if(!material)return false;
    // A saved override belongs to one material; restore before changing render paths.
    if(!restore && graphics_subpass_material_handle &&
       gchandle_get_target(graphics_subpass_material_handle)!=material)return false;
    bool ok=false;void *args[]={string_new(graphics_subpass_keyword)};
    void *value=invoke_typed(material,"IsKeywordEnabled",1,"System.String",args,&ok);
    if(!ok || !value)return false;
    if(!restore && !graphics_subpass_material_handle) {
        graphics_original_subpass_keyword=*(bool *)object_unbox(value);
        graphics_subpass_material_handle=gchandle_new(material,false);
    }
    bool requested=restore ? graphics_original_subpass_keyword : enabled;
    invoke_typed(material,requested ? "EnableKeyword" : "DisableKeyword",1,"System.String",args,&ok);
    if(!ok)return false;
    value=invoke_typed(material,"IsKeywordEnabled",1,"System.String",args,&ok);
    bool verified=ok && value && *(bool *)object_unbox(value)==requested;
    if(restore && verified){gchandle_free(graphics_subpass_material_handle);graphics_subpass_material_handle=0;}
    return verified;
}

/* The shipped iOS fragment programs use normal/material, base color, and depth.
   DefaultDeferred's desktop attachment formats do not match that encoding. */
static bool graphics_gbuffer_formats(bool restore) {
    bool ok=false;
    if(restore) {
        for(NSDictionary *saved in graphics_saved_gbuffer_formats) {
            void *attachment=gchandle_get_target([saved[@"handle"] unsignedIntValue]);
            void *field=attachment ? class_get_field_from_name(object_get_class(attachment),"attachmentDesc") : NULL;
            void *desc=field ? field_get_value_object(field,attachment) : NULL;
            int format=[saved[@"format"] intValue];
            if(!desc || !set_field(desc,"colorFormat",&format))return false;
            field_set_value(attachment,field,object_unbox(desc));
            gchandle_free([saved[@"handle"] unsignedIntValue]);
        }
        graphics_saved_gbuffer_formats=nil;
        return true;
    }
    if(graphics_saved_gbuffer_formats.count)return true;
    void *camera=input_static(find_class("UnityEngine","Camera"),"get_main",0,NULL);
    int multipass=0;void *args[]={camera,&multipass};
    void *hg=camera ? input_static(find_class("HG.Rendering.Runtime","HGCamera"),"TryGet",2,args) : NULL;
    void *path=hg ? invoke(hg,"get_renderPathInstance",0,NULL,&ok) : NULL;
    if(!path || strcmp(class_get_name(object_get_class(path)),"HGRenderPathDefaultDeferred"))return false;
    void *manager=field_reference(path,"m_gBufferProfileMgr");
    void *profiles=manager ? field_reference(manager,"m_profiles") : NULL;
    void *array=profiles ? field_reference(profiles,"m_Array") : NULL;
    if(!array || array_length(array)<1)return false;
    void *profile=array_item(array,0),*attachments=profile ? field_reference(profile,"attachmentProfiles") : NULL;
    if(object_int(attachments,"get_Count")!=3)return false;
    void *formatClass=find_class("UnityEngine.Experimental.Rendering","GraphicsFormat");
    const char *names[]={"R8G8B8A8_UNorm","R8G8B8A8_SRGB","R32_SFloat"};
    NSMutableArray *saved=[NSMutableArray array];
    for(int i=0;i<3;i++) {
        void *index[]={&i},*attachment=invoke(attachments,"get_Item",1,index,&ok);
        void *field=attachment ? class_get_field_from_name(object_get_class(attachment),"attachmentDesc") : NULL;
        void *desc=field ? field_get_value_object(field,attachment) : NULL;
        void *formatField=formatClass ? class_get_field_from_name(formatClass,names[i]) : NULL;
        void *target=formatField ? field_get_value_object(formatField,NULL) : NULL;
        void *oldField=desc ? class_get_field_from_name(object_get_class(desc),"colorFormat") : NULL;
        void *old=oldField ? field_get_value_object(oldField,desc) : NULL;
        if(!ok || !old || !target) {
            graphics_saved_gbuffer_formats=saved;
            graphics_gbuffer_formats(true);
            return false;
        }
        int value=*(int *)object_unbox(target);
        [saved addObject:@{@"handle":@(gchandle_new(attachment,false)),@"format":@(*(int *)object_unbox(old))}];
        set_field(desc,"colorFormat",&value);
        field_set_value(attachment,field,object_unbox(desc));
        void *actual=field_get_value_object(field,attachment);
        void *actualFormat=field_get_value_object(oldField,actual);
        fprintf(output,"gbuffer_format=index%d original=%d actual=%d requested=%d\n",i,[saved.lastObject[@"format"] intValue],*(int *)object_unbox(actualFormat),value);
    }
    graphics_saved_gbuffer_formats=saved;
    return true;
}

static uint32_t *graphics_builtin_defines(void) {
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    void *native=resolve ? resolve("UnityEngine.Rendering.GraphicsSettings::HasShaderDefine(UnityEngine.Rendering.GraphicsTier,UnityEngine.Rendering.BuiltinShaderDefine)") : NULL;
    Dl_info info;
    if(!native || !dladdr(native,&info))return NULL;
    uint8_t *base=info.dli_fbase;
    uintptr_t rva=(uintptr_t)native-(uintptr_t)base;
    if(rva==0x117452ac) {
        const uint8_t wrapper[]={0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9,0xfd,0x43,0x00,0x91,0xf3,0x03,0x01,0xaa,0xf4,0x03,0x00,0xaa,0x78,0x7b,0x26,0x94,0xe1,0x03,0x14,0xaa,0xe2,0x03,0x13,0xaa,0xab,0xe1,0x26,0x94,0xfd,0x7b,0x41,0xa9,0xf4,0x4f,0xc2,0xa8,0xc0,0x03,0x5f,0xd6};
        const uint8_t singleton[]={0xc0,0x00,0x80,0x52,0x9c,0x54,0xdd,0x17};
        const uint8_t getter[]={0x08,0x4c,0x21,0x8b,0x49,0x7c,0x05,0x53,0x08,0x49,0x29,0x8b,0x08,0x35,0x41,0xb9,0x08,0x25,0xc2,0x1a,0x00,0x01,0x00,0x12,0xc0,0x03,0x5f,0xd6};
        if(memcmp(native,wrapper,sizeof wrapper) || memcmp(base+0x120e40a0,singleton,sizeof singleton) ||
           memcmp(base+0x120fd978,getter,sizeof getter))return NULL;
        void *settings=((void *(*)(void))(base+0x120e40a0))();
        // 1.6.3 moved the same three 64-bit tier masks from0x11c to0x134.
        return settings ? (uint32_t *)((uint8_t *)settings+0x134) : NULL;
    }
    if(rva!=0xdd44bb4)return NULL;
    const uint8_t singleton[]={0xc0,0x00,0x80,0x52,0x67,0xb9,0xde,0x17};
    const uint8_t getter[]={0x08,0x4c,0x21,0x8b,0x49,0x7c,0x05,0x53,0x08,0x49,0x29,0x8b,0x08,0x1d,0x41,0xb9,0x08,0x25,0xc2,0x1a,0x00,0x01,0x00,0x12,0xc0,0x03,0x5f,0xd6};
    if(memcmp(base+0xe680dc4,singleton,sizeof singleton) || memcmp(base+0xe69a438,getter,sizeof getter))return NULL;
    void *settings=((void *(*)(void))(base+0xe680dc4))();
    // HasShaderDefine: settings + 0x11c + tier * 8 + (define >> 5) * 4.
    return settings ? (uint32_t *)((uint8_t *)settings+0x11c) : NULL;
}

/* The graph's property is rewritten from this engine setting every frame.
   Only the writable data byte changes; code and on-disk client files stay intact. */
static bool *graphics_engine_cpp_flag(void) {
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    void *native=resolve ? resolve("UnityEngine.HyperGryph.HGGraphicsUtils::get_enableCppRenderPath()") : NULL;
    Dl_info info;
    if(!native || !dladdr(native,&info))return NULL;
    uint8_t *base=info.dli_fbase;
    uintptr_t rva=(uintptr_t)native-(uintptr_t)base;
    if(probe_cn_client && rva==0x117f8ae8) {
        const uint8_t wrapper[]={0xfd,0x7b,0xbf,0xa9,0xfd,0x03,0x00,0x91,0x02,0x85,0x28,0x94,0xfd,0x7b,0xc1,0xa8,0xc0,0x03,0x5f,0xd6};
        const uint8_t getter[]={0x28,0xc5,0x02,0xf0,0x00,0x65,0x5f,0x39,0xc0,0x03,0x5f,0xd6};
        if(memcmp(native,wrapper,sizeof wrapper) || memcmp(base+0x12219ef8,getter,sizeof getter))return NULL;
        // CN 1.6.3: IDA-confirmed ADRP/LDRB, inside the writable __DATA segment.
        bool *flag=(bool *)(base+0x17ac07d9);
        return *(uint8_t *)flag<=1 ? flag : NULL;
    }
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

static void *graphics_pipeline(void) {
    return input_static(find_class("HG.Rendering.Runtime","HGRenderPipeline"),"get_currentPipeline",0,NULL);
}

#include <mach/mach.h>
#include <mach/mach_vm.h>

static bool graphics_native_read(uintptr_t address,void *target,size_t size) {
    mach_vm_size_t copied=0;
    return address && mach_vm_read_overwrite(mach_task_self(),address,size,
        (mach_vm_address_t)target,&copied)==KERN_SUCCESS && copied==size;
}

static bool native_temporal_owns_render(uintptr_t entry,const uint8_t *base);

/* V3 owns native Gfx textures, not managed UnityEngine.Texture objects. */
static NSDictionary *graphics_native_iv(void *camera,const uint8_t *base) {
    const uint8_t executeBytes[]={0xff,0x83,0x04,0xd1,0xfc,0x6f,0x0c,0xa9,0xfa,0x67,0x0d,0xa9,0xf8,0x5f,0x0e,0xa9};
    const uint8_t frameBytes[]={0xeb,0x2b,0xb8,0x6d,0xe9,0x23,0x01,0x6d,0xfc,0x6f,0x02,0xa9,0xfa,0x67,0x03,0xa9};
    void *field=class_get_field_from_name(object_get_class(camera),"renderPathInstanceCPP");
    if(!field || field_get_offset(field)!=1808)return @{@"error":@"camera_layout_mismatch"};
    uintptr_t path=0,manager=0;field_get_value(camera,field,&path);
    NSMutableDictionary *result=[NSMutableDictionary dictionaryWithObject:
        [NSString stringWithFormat:@"0x%lx",path] forKey:@"nativePath"];
    // HGRenderPath_Create returns {implementation, beforeCulling, render, destroy}.
    uintptr_t wrapper[4]={0};uint64_t header[4]={0};
    if(!graphics_native_read(path,wrapper,sizeof wrapper)){
        result[@"error"]=@"unreadable_path_wrapper";return result;
    }
    NSMutableArray *entries=[NSMutableArray array];
    for(int i=1;i<4;i++){
        Dl_info entryInfo={0};uint8_t bytes[16]={0};
        if(dladdr((void *)wrapper[i],&entryInfo) && entryInfo.dli_fbase==base &&
           graphics_native_read(wrapper[i],bytes,sizeof bytes)){
            NSMutableString *hex=[NSMutableString string];
            for(int j=0;j<16;j++)[hex appendFormat:@"%02x",bytes[j]];
            [entries addObject:@{@"rva":[NSString stringWithFormat:@"0x%lx",wrapper[i]-(uintptr_t)base],@"bytes":hex}];
        }else [entries addObject:NSNull.null];
    }
    result[@"wrapperEntries"]=entries;
    const uint8_t currentRender[]={0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,0xf6,0x57,0x03,0xa9};
    const uint8_t currentManager[]={0xff,0xc3,0x04,0xd1,0xfc,0x6f,0x0d,0xa9,0xfa,0x67,0x0e,0xa9,0xf8,0x5f,0x0f,0xa9};
    bool current=wrapper[1]==(uintptr_t)base+0x126ae8b0 &&
        wrapper[3]==(uintptr_t)base+0x126aef8c &&
        (wrapper[2]==(uintptr_t)base+0x126aead0 || native_temporal_owns_render(wrapper[2],base)) &&
        !memcmp(base+0x126aead0,currentRender,sizeof currentRender) &&
        !memcmp(base+0x12659960,currentManager,sizeof currentManager);
    if(!current && (memcmp(base+0xebeac64,executeBytes,sizeof executeBytes) ||
       memcmp(base+0xeb5babc,frameBytes,sizeof frameBytes) ||
       wrapper[1]!=(uintptr_t)base+0xebeaa34 || wrapper[2]!=(uintptr_t)base+0xebeac64 ||
       wrapper[3]!=(uintptr_t)base+0xebeb0d4)){
        result[@"error"]=@"unexpected_path_wrapper";return result;
    }
    result[@"implementation"]=[NSString stringWithFormat:@"0x%lx",wrapper[0]];
    // Both native SSR implementations resolve the shader ID at owner+0 before building passes.
    const uint8_t ssrBytes[]={0xc5,0x01,0x00,0x37,0x68,0x00,0x40,0x39,0x1f,0x05,0x00,0x71,0x61,0x01,0x00,0x54};
    uintptr_t ssr=0;uint8_t ssrData[104]={0};
    if(!current && !memcmp(base+0xeb80264,ssrBytes,sizeof ssrBytes) &&
       graphics_native_read(wrapper[0]+0x190,&ssr,sizeof ssr) &&
       graphics_native_read(ssr,ssrData,sizeof ssrData)){
        int shader=0,frame=0;memcpy(&shader,ssrData,4);memcpy(&frame,ssrData+4,4);
        NSMutableArray *history=[NSMutableArray array];
        for(int i=0;i<5;i++){
            int offset=i<2 ? 8+i*16 : 56+(i-2)*16;
            uint64_t handle=0;memcpy(&handle,ssrData+offset,8);
            [history addObject:[NSString stringWithFormat:@"0x%llx",handle]];
        }
        NSMutableDictionary *state=[@{@"shaderInstanceID":@(shader),@"frameIndex":@(frame),
            @"reset":@(ssrData[40]!=0),@"historyHandles":history} mutableCopy];
        void *resources=find_class("UnityEngine","Resources");
        const void *lookup=resources ? class_get_method_from_name(resources,"InstanceIDToObject",1) : NULL;
        if(lookup && shader){void *args[]={&shader},*exception=NULL;void *object=runtime_invoke(lookup,NULL,args,&exception);
            if(!exception && object){state[@"shaderName"]=object_name(object);state[@"shaderType"]=@(class_get_name(object_get_class(object)));}}
        NSMutableDictionary *sceneHistory=[NSMutableDictionary dictionary];
        for(int i=0;i<2;i++){
            uint64_t handle=0;
            if(graphics_native_read(wrapper[0]+(i ? 0x360 : 0x340),&handle,8))
                sceneHistory[i ? @"depthPyramid" : @"sceneColor"]=[NSString stringWithFormat:@"0x%llx",handle];
        }
        state[@"sceneHistory"]=sceneHistory;
        result[@"nativeSSR"]=state;
    }

    if(!graphics_native_read(wrapper[0],&manager,sizeof manager) ||
       !graphics_native_read(manager,header,sizeof header)){
        result[@"error"]=@"unreadable_manager";return result;
    }
    result[@"manager"]=[NSString stringWithFormat:@"0x%lx",manager];
    result[@"instanceCount"]=@(header[3]);
    if(header[3]>16){result[@"error"]=@"unexpected_manager_layout";return result;}
    NSMutableArray *instances=[NSMutableArray array];
    for(uint64_t i=0;i<header[3];i++) {
        uintptr_t instance=0;uint8_t data[656]={0};
        if(!graphics_native_read(header[1]+i*8,&instance,8) ||
           !graphics_native_read(instance,data,current ? 656 : 648))continue;
        uint64_t identifier=0;memcpy(&identifier,data+(current ? 536 : 528),8);
        NSMutableArray *textures=[NSMutableArray array];
        for(int t=0;t<6;t++) {
            uintptr_t texture=0;uint8_t gfx[16]={0};memcpy(&texture,data+48+t*8,8);
            if(!graphics_native_read(texture,gfx,sizeof gfx)){[textures addObject:NSNull.null];continue;}
            uint32_t textureID;uint16_t width,height,depth;
            memcpy(&textureID,gfx,4);memcpy(&width,gfx+4,2);memcpy(&height,gfx+6,2);memcpy(&depth,gfx+12,2);
            [textures addObject:@{@"nativeGfx":[NSString stringWithFormat:@"0x%lx",texture],
                @"id":@(textureID),@"width":@(width),@"height":@(height),@"depth":@(depth)}];
        }
        [instances addObject:@{@"id":@(identifier),@"nativeInstance":[NSString stringWithFormat:@"0x%lx",instance],
            @"clipmapsAThenB":textures}];
    }
    result[@"instances"]=instances;return result;
}

static NSArray *graphics_camera_paths(void) {
    NSMutableArray *rows=[NSMutableArray array];
    void *klass=find_class("HG.Rendering.Runtime","HGCamera");
    void *field=klass ? class_get_field_from_name(klass,"s_Cameras") : NULL;
    void *map=field ? field_get_value_object(field,NULL) : NULL;bool ok=false;
    void *iterator=invoke(map,"GetEnumerator",0,NULL,&ok);
    if(!ok)return rows;
    for(int i=0;i<16;i++) {
        void *next=invoke(iterator,"MoveNext",0,NULL,&ok);
        if(!ok || !next || !*(bool *)object_unbox(next))break;
        void *pair=invoke(iterator,"get_Current",0,NULL,&ok);
        void *camera=invoke(pair,"get_Value",0,NULL,&ok);
        if(!ok || !camera)continue;
        void *path=invoke(camera,"get_renderPathInstance",0,NULL,&ok);
        NSMutableDictionary *row=[NSMutableDictionary dictionary];
        row[@"name"]=object_name(field_reference(camera,"camera"));
        row[@"path"]=render_value(path,0);
        if(path) {
            const void *method=class_get_method_from_name(object_get_class(path),"RenderInternal",1);
            void *code=method ? *(void **)method : NULL;Dl_info info;
            if(code && dladdr(code,&info)) {
                row[@"renderInternalRva"]=[NSString stringWithFormat:@"0x%lx",(uintptr_t)code-(uintptr_t)info.dli_fbase];
                if([row[@"name"] isEqual:@"MainCamera"])row[@"nativeIrradianceV3"]=graphics_native_iv(camera,info.dli_fbase);
            }
        }
        [rows addObject:row];
    }
    return rows;
}

/* Character self-shadow registration goes through the game's own helper setter. */
static void *graphics_character_helpers(void) {
    void *klass=find_class("HG.Rendering.Runtime","HGCharacterHelper");
    if(!klass)return NULL;
    void *args[]={type_get_object(class_get_type(klass))};
    return input_static(find_class("UnityEngine","Resources"),"FindObjectsOfTypeAll",1,args);
}

static NSArray *graphics_character_shadow_state(void) {
    NSMutableArray *rows=[NSMutableArray array];void *array=graphics_character_helpers();
    for(uintptr_t i=0;array && i<array_length(array);i++) {
        void *helper=array_item(array,i);bool ok=false;
        NSMutableDictionary *row=[NSMutableDictionary dictionaryWithObject:object_name(helper) forKey:@"name"];
        row[@"instance"]=@(object_int(helper,"GetInstanceID"));
        for(const char **getter=(const char*[]){"get_isActiveAndEnabled","get_enableCastSelfShadow","get_enableCastHDPunctualLightShadow",NULL};*getter;getter++) {
            void *value=invoke(helper,*getter,0,NULL,&ok);if(ok && value)row[@(*getter)]=render_value(value,0);
        }
        [rows addObject:row];
    }
    return rows;
}

static bool graphics_character_self_shadow(NSDictionary *command) {
    void *array=graphics_character_helpers();bool enabled=[command[@"characterSelfShadow"] boolValue];
    NSString *filter=command[@"nameContains"] ?: @"wulfa";unsigned changed=0;
    for(uintptr_t i=0;array && i<array_length(array);i++) {
        void *helper=array_item(array,i);bool ok=false;
        if(![object_name(helper).lowercaseString containsString:filter.lowercaseString])continue;
        void *args[]={&enabled};invoke(helper,"set_enableCastSelfShadow",1,args,&ok);if(ok)changed++;
    }
    fprintf(output,"character_self_shadow=%d helpers=%u\n",enabled,changed);
    return changed>0;
}

static NSDictionary *graphics_state(void) {
    NSMutableDictionary *state=[NSMutableDictionary dictionary];
    void *shaderClass=find_class("UnityEngine","Shader");
    const void *globalTexture=typed_method(shaderClass,"GetGlobalTexture",1,"System.String");
    if(globalTexture) {
        void *args[]={string_new("_IndirectAmbientOcclusionTexture")},*exception=NULL;
        void *texture=runtime_invoke(globalTexture,NULL,args,&exception);
        if(!exception && texture)state[@"globalAmbientOcclusionTexture"]=@{@"name":object_name(texture),@"width":@(object_int(texture,"get_width")),@"height":@(object_int(texture,"get_height"))};
    }
    state[@"characterShadowHelpers"]=graphics_character_shadow_state();
    void *lighting=graphics_lighting_material();bool keywordOk=false;
    if(lighting) {
        void *keywords=invoke(lighting,"get_shaderKeywords",0,NULL,&keywordOk);
        NSMutableArray *names=[NSMutableArray array];
        if(keywordOk && keywords)for(uintptr_t i=0;i<array_length(keywords);i++)
            [names addObject:managed_string(array_item(keywords,i)) ?: @""];
        state[@"deferredLightingMaterial"]=@{@"instance":@(object_int(lighting,"GetInstanceID")),@"keywords":names};
    }
    void *lod=input_static(find_class("UnityEngine","Shader"),"get_globalMaximumLOD",0,NULL);
    if(lod)state[@"globalMaximumLOD"]=render_value(lod,0);
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    if(resolve) {
        NSMutableDictionary *irradianceNative=[NSMutableDictionary dictionary];
        const char *ivCalls[]={
            "UnityEngine.HyperGryph.HGIrradianceVolume::PipelineUpdate(UnityEngine.HyperGryph.HGIrradianceVolumePipelineUpdateResult&,System.Int64,System.IntPtr,UnityEngine.Rendering.CommandBuffer,System.Int32,System.Int32)",
            "UnityEngine.HyperGryph.HGIrradianceVolume::SetMapV3(System.Int64,System.String)",
            "UnityEngine.HyperGryph.HGIrradianceVolume::SetEnableV3(System.Boolean)",
            "UnityEngine.HyperGryph.HGIrradianceVolume::Create(UnityEngine.HyperGryph.HGIrradianceVolumeConfig&)"};
        for(int i=0;i<4;i++) {
            void *entry=resolve(ivCalls[i]);Dl_info entryInfo;
            if(entry && dladdr(entry,&entryInfo))
                irradianceNative[@(ivCalls[i])]=[NSString stringWithFormat:@"0x%lx",(uintptr_t)entry-(uintptr_t)entryInfo.dli_fbase];
        }
        state[@"irradianceNativeRvas"]=irradianceNative;
        void *native=resolve("UnityEngine.HyperGryph.HGGraphicsUtils::get_enableCppRenderPath()");
        Dl_info info;
        if(native && dladdr(native,&info)) {
            state[@"engineCppGetterRva"]=[NSString stringWithFormat:@"0x%lx",(uintptr_t)native-(uintptr_t)info.dli_fbase];
            state[@"engineCppRenderPath"]=@(((bool(*)(void))native)());
        }
        native=resolve("UnityEngine.Rendering.GraphicsSettings::HasShaderDefine(UnityEngine.Rendering.GraphicsTier,UnityEngine.Rendering.BuiltinShaderDefine)");
        if(native && dladdr(native,&info))state[@"shaderDefineGetterRva"]=[NSString stringWithFormat:@"0x%lx",(uintptr_t)native-(uintptr_t)info.dli_fbase];
    }
    NSMutableDictionary *defines=[NSMutableDictionary dictionary];
    void *define_class=find_class("UnityEngine.Rendering","BuiltinShaderDefine");
    void *settings_class=find_class("UnityEngine.Rendering","GraphicsSettings");
    void *iterator=NULL,*define_field;
    while(define_class && (define_field=class_get_fields(define_class,&iterator))) {
        const char *name=field_get_name(define_field);
        if(strncmp(name,"HG_RENDER_PATH_",15))continue;
        void *boxed=field_get_value_object(define_field,NULL);
        if(!boxed)continue;
        int value=*(int *)object_unbox(boxed);void *args[]={&value};
        void *enabled=input_static(settings_class,"HasShaderDefine",1,args);
        defines[@(name)]=@{@"id":@(value),@"enabled":enabled ? render_value(enabled,0) : NSNull.null};
    }
    state[@"shaderDefines"]=defines;
    void *hub=get_hub(),*pipeline=graphics_pipeline();bool ok=false;
    const char *hub_getters[]={"get_currentDeviceType","get_isMobileShader","get_isMobileHighShader","get_isMobileLowShader"};
    for(int i=0;i<4;i++) {
        void *v=invoke(hub,hub_getters[i],0,NULL,&ok);
        if(ok && v)state[@(hub_getters[i])]=render_value(v,0);
    }
    void *graph=invoke(pipeline,"get_renderGraph",0,NULL,&ok);
    void *cpp=invoke(graph,"get_enableCppRenderPath",0,NULL,&ok);
    if(ok && cpp)state[@"cppRenderPath"]=render_value(cpp,0);
    void *debugParameters=field_reference(graph,"m_DebugParameters");
    if(debugParameters)state[@"graphDebugParameters"]=render_value(debugParameters,1);
    void *debug=input_static(find_class("HG.Rendering.RenderGraphModule","HGRenderGraph"),"get_requireDebugData",0,NULL);
    if(debug)state[@"requireDebugData"]=render_value(debug,0);
    if(graph)state[@"renderGraphs"]=dictionary_entries(field_reference(graph,"m_DebugData"));
    void *resources=field_reference(field_reference(pipeline,"m_GlobalSettings"),"m_RenderPipelineResources");
    void *shaders=field_reference(resources,"shaders");
    NSMutableDictionary *shader_state=[NSMutableDictionary dictionary];
    const char *names[]={"GTAmbientOcclusionCS","screenSpaceReflectionCS","screenSpaceReflectionPS","contactShadowCS","deferredPS","depthPyramidCS"};
    for(int i=0;i<6;i++) {
        void *shader=field_reference(shaders,names[i]);
        NSMutableDictionary *row=[NSMutableDictionary dictionaryWithObject:@(shader!=NULL) forKey:@"present"];
        if(shader) {
            row[@"name"]=object_name(shader);
            if(!strcmp(class_get_name(object_get_class(shader)),"Shader")) {
                void *supported=invoke(shader,"get_isSupported",0,NULL,&ok);
                if(ok && supported)row[@"supported"]=render_value(supported,0);
            }
        }
        shader_state[@(names[i])]=row;
    }
    state[@"shaders"]=shader_state;
    state[@"cameras"]=graphics_camera_paths();
    return state;
}

/* OnePass resets the profile during OnPreRendering. PrepareGBufferOutput asks
   for that profile again immediately before native resource allocation. */
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
    const uint8_t expectedCN[]={0x08,0x75,0x05,0xd0,0x08,0x61,0x3d,0x91,0x08,0x01,0x40,0xf9,0x08,0x41,0x40,0xf9};
    if(!pointer || !method || !dladdr(pointer,&info) ||
       (uintptr_t)pointer-(uintptr_t)info.dli_fbase!=(probe_cn_client ? 0xcef716c : 0xcef7174) ||
       memcmp(pointer,probe_cn_client ? expectedCN : expected,16) ||
       strcmp(method_get_name(method),"GetGBufferProfileConfig") || method_get_param_count(method)!=0) {
        fprintf(output,"gbuffer_retention=identity_mismatch\n");return false;
    }
    graphics_profile_original=(GBufferProfileFunc)pointer;graphics_profile_slot=slot;
    *slot=(void *)graphics_retained_profile;
    fprintf(output,"gbuffer_retention=enabled\n");return true;
}

static void graphics_mode(NSDictionary *command) {
    NSString *mode=command[@"mode"] ?: @"status";
    void *hub=get_hub(),*impl=field_reference(hub,"m_impl");bool ok=false;
    bool success=impl!=NULL;
    if(command[@"gbufferMemoryless"]) {
        int requested=[command[@"gbufferMemoryless"] intValue];
        success=(requested==0 || requested==1) && graphics_gbuffer_retention(requested==0) && success;
    }
    if(command[@"characterSelfShadow"])success=graphics_character_self_shadow(command) && success;
    if(command[@"irradianceV2"] || ([mode isEqual:@"restore"] && graphics_iv_switch_handle)) {
        bool restore=[mode isEqual:@"restore"];
        void *featureClass=find_class("HG.Rendering.Runtime","HGGraphicsFeatureManager");
        void *featureField=featureClass ? class_get_field_from_name(featureClass,"irradianceVolumeV2") : NULL;
        void *feature=restore ? gchandle_get_target(graphics_iv_switch_handle) :
            (featureField ? field_get_value_object(featureField,NULL) : NULL);
        void *field=feature ? class_get_field_from_name(object_get_class(feature),"m_defaultValue") : NULL;
        if(!field || field_get_offset(field)!=16 || strcmp(class_get_name(object_get_class(feature)),"HGGraphicsFeatureSwitch"))
            success=false;
        else {
            if(!graphics_iv_switch_handle) {
                field_get_value(feature,field,&graphics_original_iv_switch);
                graphics_iv_switch_handle=gchandle_new(feature,false);
            }
            bool value=restore ? graphics_original_iv_switch : [command[@"irradianceV2"] boolValue];
            field_set_value(feature,field,&value);
            bool actual=false;field_get_value(feature,field,&actual);
            success=success && actual==value;
            fprintf(output,"irradiance_v2=%d restored=%d\n",actual,restore);
            if(restore && actual==value){gchandle_free(graphics_iv_switch_handle);graphics_iv_switch_handle=0;}
        }
    }
    if([mode isEqual:@"restore"] && graphics_subpass_material_handle)
        success=graphics_set_subpass_keyword(false,true) && success;
    else if(command[@"subpassKeyword"])
        success=graphics_set_subpass_keyword([command[@"subpassKeyword"] boolValue],false) && success;
    if([mode isEqual:@"restore"] && graphics_saved_gbuffer_formats.count)
        success=graphics_gbuffer_formats(true) && success;
    if(command[@"gbufferLayout"]) {
        bool requested=[command[@"gbufferLayout"] isEqual:@"mobile"];
        bool applied=requested && graphics_gbuffer_formats(false);
        fprintf(output,"gbuffer_layout=%s\n",applied ? "applied" : "failed");
        success=applied && success;
    }
    if([mode isEqual:@"desktop"] && impl) {
        void *current=invoke(impl,"get_currentDeviceType",0,NULL,&ok);
        void *enum_class=find_class("HG.Rendering.Runtime","HGDeviceType");
        void *desktop_field=enum_class ? class_get_field_from_name(enum_class,"Desktop") : NULL;
        void *desktop=desktop_field ? field_get_value_object(desktop_field,NULL) : NULL;
        if(!ok || !current || !desktop)success=false;
        else {
            if(!graphics_saved_device) {
                graphics_original_device=*(int *)object_unbox(current);
                graphics_saved_device=true;
            }
            int value=*(int *)object_unbox(desktop);void *args[]={&value};
            invoke_typed(impl,"set_currentDeviceType",1,"HG.Rendering.Runtime.HGDeviceType",args,&ok);
            success=ok;
        }
    } else if([mode isEqual:@"restore"]) {
        if(graphics_saved_device && impl) {
            void *args[]={&graphics_original_device};
            invoke_typed(impl,"set_currentDeviceType",1,"HG.Rendering.Runtime.HGDeviceType",args,&ok);
            success=success && ok;if(ok)graphics_saved_device=false;
        }
    } else if(![mode isEqual:@"status"])success=false;
    if(command[@"lod"] || ([mode isEqual:@"restore"] && graphics_saved_lod)) {
        void *klass=find_class("UnityEngine","Shader");
        void *current=input_static(klass,"get_globalMaximumLOD",0,NULL);
        const void *setter=klass ? typed_method(klass,"set_globalMaximumLOD",1,"System.Int32") : NULL;
        if(!current || !setter)success=false;
        else {
            if(!graphics_saved_lod){graphics_original_lod=*(int *)object_unbox(current);graphics_saved_lod=true;}
            int value=[mode isEqual:@"restore"] ? graphics_original_lod : [command[@"lod"] intValue];
            void *args[]={&value},*exception=NULL;runtime_invoke(setter,NULL,args,&exception);
            if(exception)success=false;
            else if([mode isEqual:@"restore"])graphics_saved_lod=false;
        }
    }
    if(command[@"deferred"] || command[@"shaderPlatform"] || ([mode isEqual:@"restore"] && graphics_saved_defines)) {
        uint32_t *defines=graphics_builtin_defines();
        if(!defines)success=false;
        else if([mode isEqual:@"restore"] || (command[@"deferred"] && ![command[@"deferred"] boolValue])) {
            if(graphics_saved_defines){memcpy(defines,graphics_original_defines,sizeof graphics_original_defines);graphics_saved_defines=false;}
        } else {
            if(!graphics_saved_defines){memcpy(graphics_original_defines,defines,sizeof graphics_original_defines);graphics_saved_defines=true;}
            // IDs 36 and 37 are DEFAULT_DEFERRED and ONE_PASS_DEFERRED, respectively.
            if([command[@"deferred"] boolValue])
                for(int tier=0;tier<3;tier++)defines[tier*2+1]=(defines[tier*2+1]&~(1u<<5))|(1u<<4);
            // The material classification setter does not change these engine capability bits.
            if(command[@"shaderPlatform"]) {
                NSString *platform=command[@"shaderPlatform"];
                if([platform isEqual:@"desktop"] || [platform isEqual:@"mobile"]) {
                    uint32_t selected=[platform isEqual:@"desktop"] ? (1u<<2) : (1u<<1);
                    for(int tier=0;tier<3;tier++)defines[tier*2+1]=(defines[tier*2+1]&~((1u<<1)|(1u<<2)))|selected;
                } else success=false;
            }
        }
    }
    if(command[@"cpp"] || ([mode isEqual:@"restore"] && graphics_saved_cpp)) {
        bool *flag=graphics_engine_cpp_flag();
        if(!flag)success=false;
        else {
            if(!graphics_saved_cpp) {
                graphics_original_cpp=*flag;
                graphics_saved_cpp=true;
            }
            bool value=[mode isEqual:@"restore"] ? graphics_original_cpp : [command[@"cpp"] boolValue];
            *flag=value;
            if([mode isEqual:@"restore"])graphics_saved_cpp=false;
        }
    }
    if(command[@"debug"] || ([mode isEqual:@"restore"] && graphics_saved_debug)) {
        void *klass=find_class("HG.Rendering.RenderGraphModule","HGRenderGraph");
        void *current=input_static(klass,"get_requireDebugData",0,NULL);
        const void *setter=klass ? typed_method(klass,"set_requireDebugData",1,"System.Boolean") : NULL;
        if(!current || !setter)success=false;
        else {
            if(!graphics_saved_debug){graphics_original_debug=*(bool *)object_unbox(current);graphics_saved_debug=true;}
            bool value=[mode isEqual:@"restore"] ? graphics_original_debug : [command[@"debug"] boolValue];
            void *args[]={&value},*exception=NULL;runtime_invoke(setter,NULL,args,&exception);
            if(exception)success=false;
            else if([mode isEqual:@"restore"])graphics_saved_debug=false;
        }
    }
    if(command[@"passCulling"] || ([mode isEqual:@"restore"] && graphics_culling_handle)) {
        bool restore=[mode isEqual:@"restore"];
        void *graph=invoke(graphics_pipeline(),"get_renderGraph",0,NULL,&ok);
        void *parameters=restore ? gchandle_get_target(graphics_culling_handle) : field_reference(graph,"m_DebugParameters");
        void *field=parameters ? class_get_field_from_name(object_get_class(parameters),"disablePassCulling") : NULL;
        void *current=field ? field_get_value_object(field,parameters) : NULL;
        if(!current)success=false;
        else {
            if(!restore && !graphics_culling_handle){
                graphics_original_disable_culling=*(bool *)object_unbox(current);
                graphics_culling_handle=gchandle_new(parameters,false);
            }
            bool disabled=restore ? graphics_original_disable_culling : ![command[@"passCulling"] boolValue];
            field_set_value(parameters,field,&disabled);
            bool actual=*(bool *)object_unbox(field_get_value_object(field,parameters));
            success=(actual==disabled) && success;
            if(restore && actual==disabled){gchandle_free(graphics_culling_handle);graphics_culling_handle=0;}
        }
    }
    NSDictionary *state=graphics_state();
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:runtime_path(@"endfield-ios-graphics-state.json") atomically:YES];
    fprintf(output,"graphics_mode=%s mode=%s\n",success ? "applied" : "failed",mode.UTF8String);
}
