// The native scaler output is scoped to the command buffer that produced it.
static id<MTLTexture> sceneTextureAlias172(id encoder,id<MTLTexture> texture) {
    if(texture) {
        @synchronized(metal_ao_lock) {
            texture=[managed_gbuffer_stored_textures objectForKey:texture] ?: texture;
        }
    }
    return native_temporal_texture(encoder,texture);
}

static void nativeComputeHooks172(Class bufferClass,Class computeClass) {
    SEL begin=@selector(computeCommandEncoder);
    IMP create=class_getMethodImplementation(bufferClass,begin);
    sceneSwizzle172(bufferClass,begin,^id(id command){
        id encoder=((id(*)(id,SEL))create)(command,begin);
        native_temporal_command_buffer(encoder,command);
        return encoder;
    });
    SEL texture=@selector(setTexture:atIndex:);
    IMP set=class_getMethodImplementation(computeClass,texture);
    sceneSwizzle172(computeClass,texture,^(id encoder,id value,NSUInteger index){
        value=sceneTextureAlias172(encoder,value);
        ((void(*)(id,SEL,id,NSUInteger))set)(encoder,texture,value,index);
    });
    SEL textures=@selector(setTextures:withRange:);
    IMP sets=class_getMethodImplementation(computeClass,textures);
    sceneSwizzle172(computeClass,textures,^(id encoder,const id<MTLTexture> *values,NSRange range){
        __unsafe_unretained id<MTLTexture> aliases[128];
        if(range.length<=128) {
            for(NSUInteger i=0;i<range.length;i++)aliases[i]=sceneTextureAlias172(encoder,values[i]);
            values=aliases;
        }
        ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))sets)(encoder,textures,values,range);
    });
}

static char sceneGIBindingsKey172,sceneGITextureInfoKey172;
static NSDictionary *sceneLastGI172;
static atomic_ulong sceneGIDraws172,sceneGIValidDraws172;

static void sceneGIInput172(id encoder,id<MTLTexture> texture,NSUInteger index) {
    if(index>=32)return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&sceneGIBindingsKey172);
    if(texture.textureType!=MTLTextureType3D) {
        [bindings removeObjectForKey:@(index)];return;
    }
    if(!bindings) {
        bindings=[NSMutableDictionary dictionary];
        objc_setAssociatedObject(encoder,&sceneGIBindingsKey172,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    NSDictionary *info=objc_getAssociatedObject(texture,&sceneGITextureInfoKey172);
    if(!info) {
        info=@{@"size":@[@(texture.width),@(texture.height),@(texture.depth)],
            @"format":@(texture.pixelFormat),@"label":texture.label ?: @""};
        objc_setAssociatedObject(texture,&sceneGITextureInfoKey172,info,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    bindings[@(index)]=info;
}

static void sceneGIObserve172(id encoder,NSDictionary *patch) {
    NSArray *indices=patch[@"giTextureIndices"];
    if(!indices.count)return;
    NSDictionary *bindings=objc_getAssociatedObject(encoder,&sceneGIBindingsKey172);
    NSMutableDictionary *values=[NSMutableDictionary dictionary];BOOL valid=YES;
    for(NSNumber *index in indices) {
        NSDictionary *info=bindings[index];
        values[index.stringValue]=info ?: (id)NSNull.null;
        NSArray *size=info[@"size"];
        valid=valid && size.count==3 && [size[0] unsignedIntValue]>1 && [size[1] unsignedIntValue]>1;
    }
    atomic_fetch_add(&sceneGIDraws172,1);
    if(valid)atomic_fetch_add(&sceneGIValidDraws172,1);
    @synchronized(metal_ao_lock) {
        if(![sceneLastGI172[@"textures"] isEqual:values])
            sceneLastGI172=@{@"textures":[values copy],@"valid":@(valid)};
    }
}
