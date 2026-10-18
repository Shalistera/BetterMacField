/* Timed native-graph experiment: match the shipped iOS GBuffer encoding. */
static bool native_gbuffer_enabled;
static NSMapTable *native_gbuffer_aliases;
static NSMapTable *native_gbuffer_variants;
static unsigned native_gbuffer_passes;
static char native_gbuffer_targets_key,native_gbuffer_formats_key;
static NSMutableSet *native_gbuffer_pairs,*native_gbuffer_mismatches;

static void native_gbuffer_encoder(id encoder,MTLRenderPassDescriptor *pass) {
    if(!native_gbuffer_enabled)return;
    NSMutableArray *formats=[NSMutableArray array];
    for(NSUInteger i=0;i<8;i++)[formats addObject:@(pass.colorAttachments[i].texture.pixelFormat)];
    objc_setAssociatedObject(encoder,&native_gbuffer_targets_key,formats,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void native_gbuffer_check(id encoder,id pipeline) {
    if(!native_gbuffer_enabled)return;
    NSArray *targets=objc_getAssociatedObject(encoder,&native_gbuffer_targets_key);
    NSArray *formats=objc_getAssociatedObject(pipeline,&native_gbuffer_formats_key);
    if(!targets)return;
    NSString *pair=[NSString stringWithFormat:@"pso=%p targets=%@ pipeline=%@",(__bridge void *)pipeline,targets,formats];
    @synchronized(native_gbuffer_pairs) {
        [native_gbuffer_pairs addObject:pair];
        if(!formats || ![targets isEqual:formats])[native_gbuffer_mismatches addObject:pair];
    }
}

static id<MTLTexture> native_gbuffer_texture(id<MTLTexture> texture) {
    if(!texture)return nil;
    return native_gbuffer_enabled ? ([native_gbuffer_aliases objectForKey:texture] ?: texture) : texture;
}

static bool native_gbuffer_layout(MTLPixelFormat a,MTLPixelFormat b,MTLPixelFormat c) {
    return a==MTLPixelFormatRGB10A2Unorm && b==MTLPixelFormatRGB10A2Unorm && c==MTLPixelFormatRGBA8Unorm_sRGB;
}

static MTLRenderPipelineDescriptor *native_gbuffer_pipeline(MTLRenderPipelineDescriptor *descriptor) {
    if(!native_gbuffer_enabled || !native_gbuffer_layout(descriptor.colorAttachments[2].pixelFormat,
       descriptor.colorAttachments[3].pixelFormat,descriptor.colorAttachments[4].pixelFormat))return descriptor;
    MTLRenderPipelineDescriptor *copy=[descriptor copy];
    copy.colorAttachments[2].pixelFormat=MTLPixelFormatRGBA8Unorm;
    copy.colorAttachments[3].pixelFormat=MTLPixelFormatRGBA8Unorm_sRGB;
    copy.colorAttachments[4].pixelFormat=MTLPixelFormatR32Float;
    return copy;
}

static MTLRenderPassDescriptor *native_gbuffer_pass(MTLRenderPassDescriptor *pass) {
    if(!native_gbuffer_enabled)return pass;
    if(native_gbuffer_layout(pass.colorAttachments[2].texture.pixelFormat,
       pass.colorAttachments[3].texture.pixelFormat,pass.colorAttachments[4].texture.pixelFormat)) {
        const MTLPixelFormat formats[]={MTLPixelFormatRGBA8Unorm,MTLPixelFormatRGBA8Unorm_sRGB,MTLPixelFormatR32Float};
        for(NSUInteger i=0;i<3;i++) {
            id<MTLTexture> original=pass.colorAttachments[i+2].texture;
            if(original.textureType!=MTLTextureType2D || original.sampleCount!=1)continue;
            NSMutableDictionary *variants=[native_gbuffer_variants objectForKey:original];
            id<MTLTexture> replacement=variants[@(formats[i])];
            if(replacement) {
                [native_gbuffer_aliases setObject:replacement forKey:original];
                continue;
            }
            MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:formats[i]
                width:original.width height:original.height mipmapped:NO];
            desc.storageMode=MTLStorageModePrivate;
            desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
            replacement=[original.device newTextureWithDescriptor:desc];
            replacement.label=[NSString stringWithFormat:@"Endfield native mobile GBuffer%lu",(unsigned long)i];
            if(replacement) {
                if(!variants){variants=[NSMutableDictionary dictionary];[native_gbuffer_variants setObject:variants forKey:original];}
                variants[@(formats[i])]=replacement;
                [native_gbuffer_aliases setObject:replacement forKey:original];
            }
        }
        native_gbuffer_passes++;
    }
    MTLRenderPassDescriptor *copy=[pass copy];
    for(NSUInteger i=0;i<8;i++) {
        copy.colorAttachments[i].texture=native_gbuffer_texture(pass.colorAttachments[i].texture);
        copy.colorAttachments[i].resolveTexture=native_gbuffer_texture(pass.colorAttachments[i].resolveTexture);
    }
    return copy;
}
