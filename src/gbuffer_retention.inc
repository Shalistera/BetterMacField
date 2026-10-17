static atomic_bool managed_gbuffer_enabled;
static NSMapTable *managed_gbuffer_stored_textures;
static char managed_gbuffer_frame_key,managed_gbuffer_ready_key;
static atomic_uint managed_gbuffer_layouts,managed_gbuffer_size_mismatches,managed_gbuffer_memoryless,managed_gbuffer_captures;
static id<MTLTexture> managed_gbuffer_stored(id<MTLTexture> texture) {
    bool resolveDepth=false;
    if(texture.storageMode!=MTLStorageModeMemoryless && (!resolveDepth || (texture.usage&MTLTextureUsageShaderWrite)))return texture;
    @synchronized(bmf_metal_lock) {
    if(!managed_gbuffer_stored_textures)managed_gbuffer_stored_textures=[NSMapTable weakToStrongObjectsMapTable];
    id<MTLTexture> stored=[managed_gbuffer_stored_textures objectForKey:texture];
    if(stored && (!resolveDepth || (stored.usage&MTLTextureUsageShaderWrite)))return stored;
    if(texture.textureType!=MTLTextureType2D || texture.sampleCount!=1)return nil;
    MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:texture.pixelFormat
        width:texture.width height:texture.height mipmapped:NO];
    descriptor.storageMode=MTLStorageModePrivate;
    descriptor.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    if(resolveDepth)descriptor.usage|=MTLTextureUsageShaderWrite;
    stored=[texture.device newTextureWithDescriptor:descriptor];
    if(stored){
        stored.label=[@"Endfield stored " stringByAppendingString:texture.label ?: @"GBuffer"];
        [managed_gbuffer_stored_textures setObject:stored forKey:texture];
    }
    return stored;
    }
}

static void managed_gbuffer_set_enabled(bool enabled) {
    @synchronized(bmf_metal_lock) {
        atomic_store(&managed_gbuffer_enabled,enabled);
        if(!enabled)managed_gbuffer_stored_textures=nil;
    }
}

static MTLRenderPassDescriptor *managed_gbuffer_pass(id buffer,MTLRenderPassDescriptor *pass) {
    if(!atomic_load(&managed_gbuffer_enabled))return pass;
    id<MTLTexture> normal=pass.colorAttachments[2].texture;
    id<MTLTexture> color=pass.colorAttachments[3].texture;
    id<MTLTexture> depth=pass.colorAttachments[4].texture;
    if(normal.pixelFormat!=MTLPixelFormatRGBA8Unorm || color.pixelFormat!=MTLPixelFormatRGBA8Unorm_sRGB ||
       depth.pixelFormat!=MTLPixelFormatR32Float)return pass;
    atomic_fetch_add(&managed_gbuffer_layouts,1);
    if(normal.width!=depth.width || normal.height!=depth.height || color.width!=depth.width || color.height!=depth.height){
        atomic_fetch_add(&managed_gbuffer_size_mismatches,1);return pass;
    }
    if(normal.storageMode==MTLStorageModeMemoryless || color.storageMode==MTLStorageModeMemoryless || depth.storageMode==MTLStorageModeMemoryless){
        atomic_fetch_add(&managed_gbuffer_memoryless,1);
    }
    normal=managed_gbuffer_stored(normal);color=managed_gbuffer_stored(color);depth=managed_gbuffer_stored(depth);
    if(!normal || !color || !depth)return pass;
    MTLRenderPassDescriptor *copy=[pass copy];
    copy.colorAttachments[2].texture=normal;copy.colorAttachments[3].texture=color;copy.colorAttachments[4].texture=depth;
    for(NSUInteger i=2;i<=4;i++)copy.colorAttachments[i].storeAction=MTLStoreActionStore;
    objc_setAssociatedObject(buffer,&managed_gbuffer_frame_key,@[normal,color,depth],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(buffer,&managed_gbuffer_ready_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    atomic_fetch_add(&managed_gbuffer_captures,1);
    return copy;
}
