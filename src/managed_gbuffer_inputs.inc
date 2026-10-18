/* Preserve managed OnePass split outputs for lighting and reflection inputs. */
static atomic_bool managed_gbuffer_enabled;
static atomic_uint managed_gbuffer_captures,managed_gbuffer_draws,managed_gbuffer_missing;
static atomic_uint managed_gbuffer_layouts,managed_gbuffer_memoryless,managed_gbuffer_size_mismatches;
static char managed_gbuffer_frame_key,managed_gbuffer_encoder_key,managed_gbuffer_indices_key,managed_gbuffer_store_key;
static NSMapTable *managed_gbuffer_stored_textures;
static char managed_gbuffer_ready_key,managed_gbuffer_source_key;
static atomic_uint managed_character_depth_draws,managed_character_depth_missing;
static atomic_bool native_decal_depth_enabled;
static atomic_uint native_decal_depth_draws,native_decal_depth_missing;
static char native_decal_depth_pipeline_key;
static atomic_bool managed_gbuffer_sample_pending;
static char managed_gbuffer_sample_depth_key;
static atomic_uint native_decal_depth_resolves;

static bool managed_gbuffer_resolve_depth(id<MTLCommandBuffer> buffer,id<MTLTexture> packed,id<MTLTexture> hardware) {
    // The native split prepass leaves clear=1 in the R32 G-buffer for terrain,
    // while the same pass's hardware depth contains its actual device depth.
    if(!hardware || hardware.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8 ||
       hardware.storageMode==MTLStorageModeMemoryless || packed.pixelFormat!=MTLPixelFormatR32Float ||
       packed.width!=hardware.width || packed.height!=hardware.height || !(packed.usage&MTLTextureUsageShaderWrite))return false;
    static id<MTLComputePipelineState> pipeline;
    if(!pipeline){
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void resolveDepth(depth2d<float,access::read> src [[texture(0)]],"
            "texture2d<float,access::write> dst [[texture(1)]],uint2 p [[thread_position_in_grid]]){"
            "if(p.x>=dst.get_width()||p.y>=dst.get_height())return;dst.write(float4(src.read(p)),p);}";
        NSError *error=nil;id<MTLLibrary> lib=[buffer.device newLibraryWithSource:source options:nil error:&error];
        pipeline=lib ? [buffer.device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"resolveDepth"] error:&error] : nil;
        if(!pipeline){fprintf(output,"native_depth_resolve=compile_failed %s\n",error.description.UTF8String);return false;}
    }
    id<MTLComputeCommandEncoder> compute=[buffer computeCommandEncoder];
    if(!compute)return false;
    [compute setComputePipelineState:pipeline];[compute setTexture:hardware atIndex:0];[compute setTexture:packed atIndex:1];
    [compute dispatchThreads:MTLSizeMake(packed.width,packed.height,1) threadsPerThreadgroup:MTLSizeMake(pipeline.threadExecutionWidth,1,1)];
    [compute endEncoding];atomic_fetch_add(&native_decal_depth_resolves,1);return true;
}

static void managed_gbuffer_sample(id<MTLCommandBuffer> buffer,id<MTLTexture> packed,id<MTLTexture> hardware) {
    if(!hardware || hardware.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8 ||
       !atomic_exchange(&managed_gbuffer_sample_pending,false))return;
    NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void sampleDepth(texture2d<float,access::read> a [[texture(0)]],"
        "depth2d<float,access::read> b [[texture(1)]],device float2* out [[buffer(0)]],uint i [[thread_position_in_grid]]){"
        "if(i>=81)return;uint2 p=uint2((float(i%9)+.5)*a.get_width()/9,(float(i/9)+.5)*a.get_height()/9);"
        "out[i]=float2(a.read(p).x,b.read(p));}";
    NSError *error=nil;id<MTLLibrary> lib=[buffer.device newLibraryWithSource:source options:nil error:&error];
    id<MTLComputePipelineState> pipeline=lib ? [buffer.device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"sampleDepth"] error:&error] : nil;
    if(!pipeline){fprintf(output,"managed_gbuffer_sample=compile_failed %s\n",error.description.UTF8String);return;}
    id<MTLBuffer> values=[buffer.device newBufferWithLength:81*2*sizeof(float) options:MTLResourceStorageModeShared];
    id<MTLComputeCommandEncoder> compute=[buffer computeCommandEncoder];
    [compute setComputePipelineState:pipeline];[compute setTexture:packed atIndex:0];[compute setTexture:hardware atIndex:1];
    [compute setBuffer:values offset:0 atIndex:0];
    [compute dispatchThreads:MTLSizeMake(81,1,1) threadsPerThreadgroup:MTLSizeMake(27,1,1)];[compute endEncoding];
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){
        NSMutableArray *samples=[NSMutableArray array];const float *v=values.contents;
        for(int i=0;i<81;i++)[samples addObject:@[@(v[i*2]),@(v[i*2+1])]];
        NSDictionary *result=@{@"width":@(packed.width),@"height":@(packed.height),@"status":@(completed.status),
            @"columns":@[@"GBufferDepth",@"HardwareDepth"],@"samples":samples};
        [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:runtime_path(@"endfield-ios-gbuffer-depth-sample.json") atomically:YES];
        fprintf(output,"managed_gbuffer_sample=complete status=%lu\n",(unsigned long)completed.status);fflush(output);
    }];
}

static void managed_gbuffer_classify(id pipeline,NSString *description) {
    // 1.6.3 HGRP/DecalLit program16, COMPLETE_PBR + NORMAL_ANGLE_BLEND.
    // Authored _GBufferTexture2 at fragment texture2 supplies device depth;
    // current AIR reconstructs position with inverse VP before box clipping.
    if([description containsString:@"sha256=78f34ad24245102168f092a1c7f9b50be79e1f0d52c41db6fb46968945c0282b"])
        objc_setAssociatedObject(pipeline,&native_decal_depth_pipeline_key,@2,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    // Terrain variants use the authored per-fragment depth bindings generated
    // by current_native_indirect; optional features change the argument index.
    // 1.6.3 VisibilitySH program10, HG_ENABLE_MV, passes4/6. The sole
    // fragment texture is authored _GBufferTexture2 at slot0. Its device
    // depth reconstructs position/normal before capsule visibility writes
    // the G-buffer; stale texture1/2 bindings are not inputs to this shader.
    if([description containsString:@"sha256=4a123c03a0a08fad55f790951ba3dc75f17d81506576f965668a05f10bea64d0"])
        objc_setAssociatedObject(pipeline,&native_decal_depth_pipeline_key,@0,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static id<MTLTexture> managed_gbuffer_stored(id<MTLTexture> texture) {
    bool resolveDepth=atomic_load(&native_decal_depth_enabled) && texture.pixelFormat==MTLPixelFormatR32Float;
    if(texture.storageMode!=MTLStorageModeMemoryless && (!resolveDepth || (texture.usage&MTLTextureUsageShaderWrite)))return texture;
    @synchronized(metal_ao_lock) {
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
    @synchronized(metal_ao_lock) {
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
    if(atomic_load(&metal_native_contact_strength)>0)copy.stencilAttachment.storeAction=MTLStoreActionStore;
    if(atomic_load(&metal_screen_ao_strength)>0){
        id<MTLTexture> motion=copy.colorAttachments[1].texture;
        if(motion.pixelFormat==MTLPixelFormatRGB10A2Unorm){
            copy.colorAttachments[1].texture=managed_gbuffer_stored(motion);
            copy.colorAttachments[1].storeAction=MTLStoreActionStore;
        }
    }
    copy.colorAttachments[2].texture=normal;copy.colorAttachments[3].texture=color;copy.colorAttachments[4].texture=depth;
    for(NSUInteger i=2;i<=4;i++)copy.colorAttachments[i].storeAction=MTLStoreActionStore;
    objc_setAssociatedObject(buffer,&managed_gbuffer_frame_key,@[normal,color,depth],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    official_gtao_begin(buffer);
    official_contact_begin(buffer);
    objc_setAssociatedObject(buffer,&managed_gbuffer_ready_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    atomic_fetch_add(&managed_gbuffer_captures,1);
    return copy;
}

static void managed_gbuffer_encoder(id encoder,id buffer,MTLRenderPassDescriptor *pass) {
    if(atomic_load(&managed_gbuffer_enabled)) {
        objc_setAssociatedObject(encoder,&managed_gbuffer_encoder_key,[NSValue valueWithNonretainedObject:buffer],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        NSArray *textures=objc_getAssociatedObject(buffer,&managed_gbuffer_frame_key);
        bool source=textures.count==3 && pass.colorAttachments[2].texture==textures[0] &&
            pass.colorAttachments[3].texture==textures[1] && pass.colorAttachments[4].texture==textures[2];
        objc_setAssociatedObject(encoder,&managed_gbuffer_store_key,@(source),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if(source){
            official_gtao_source(encoder,pass);
            official_contact_source(encoder,pass);
            objc_setAssociatedObject(encoder,&managed_gbuffer_source_key,textures,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            if(atomic_load(&managed_gbuffer_sample_pending) || atomic_load(&native_decal_depth_enabled))
                objc_setAssociatedObject(encoder,&managed_gbuffer_sample_depth_key,pass.depthAttachment.texture,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
    }
}

static void managed_gbuffer_ended(id encoder) {
    NSArray *textures=objc_getAssociatedObject(encoder,&managed_gbuffer_source_key);
    id buffer=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
    bool ready=textures && textures==objc_getAssociatedObject(buffer,&managed_gbuffer_frame_key);
    if(ready && atomic_load(&native_decal_depth_enabled))
        ready=managed_gbuffer_resolve_depth(buffer,textures[2],objc_getAssociatedObject(encoder,&managed_gbuffer_sample_depth_key));
    if(ready)
        objc_setAssociatedObject(buffer,&managed_gbuffer_ready_key,textures,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if(ready)official_gtao_capture(encoder,buffer,textures[2]);
    if(ready)official_gtao_encode(encoder,buffer,textures[2]);
    if(ready)official_contact_encode(encoder,buffer,textures[2]);
    if(textures && atomic_load(&managed_gbuffer_sample_pending))
        managed_gbuffer_sample(buffer,textures[2],objc_getAssociatedObject(encoder,&managed_gbuffer_sample_depth_key));
}

static void managed_character_depth_draw(id<MTLRenderCommandEncoder> encoder) {
    id buffer=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
    NSArray *textures=buffer ? objc_getAssociatedObject(buffer,&managed_gbuffer_ready_key) : nil;
    bool ready=atomic_load(&managed_gbuffer_enabled) && textures.count==3 &&
        ![objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue];
    id<MTLTexture> depth=ready ? textures[2] : metal_ao_white;
    float settings[]={ready ? atomic_load(&metal_screen_ao_strength) : 0,atomic_load(&metal_screen_ao_radius),
        ready ? atomic_load(&metal_native_contact_strength) : 0,atomic_load(&metal_native_contact_length)};
    uint32_t debug=ready ? atomic_load(&metal_native_contact_debug) | (atomic_load(&metal_screen_ao_debug)<<8) : 0;
    [encoder setFragmentTexture:depth atIndex:31];
    [encoder setFragmentBytes:settings length:sizeof settings atIndex:29];
    [encoder setFragmentBytes:&debug length:sizeof debug atIndex:28];
    atomic_fetch_add(ready ? &managed_character_depth_draws : &managed_character_depth_missing,1);
}

static void managed_gbuffer_finish(id<MTLRenderCommandEncoder> encoder) {
    if([objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue] && atomic_load(&metal_native_contact_strength)>0)
        [encoder setStencilStoreAction:MTLStoreActionStore];
    if([objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue])
        for(NSUInteger i=atomic_load(&metal_screen_ao_strength)>0 ? 1 : 2;i<=4;i++)[encoder setColorStoreAction:MTLStoreActionStore atIndex:i];
}

static void managed_gbuffer_draw(id<MTLRenderCommandEncoder> encoder,id pipeline) {
    if(!atomic_load(&managed_gbuffer_enabled))return;
    id buffer=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
    NSNumber *depthIndex=objc_getAssociatedObject(pipeline,&native_decal_depth_pipeline_key);
    if(atomic_load(&native_decal_depth_enabled) && depthIndex) {
        NSArray *ready=buffer ? objc_getAssociatedObject(buffer,&managed_gbuffer_ready_key) : nil;
        if(ready.count==3 && ![objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue]) {
            [encoder setFragmentTexture:ready[2] atIndex:depthIndex.unsignedIntegerValue];
            atomic_fetch_add(&native_decal_depth_draws,1);
        } else atomic_fetch_add(&native_decal_depth_missing,1);
    }
    NSArray *indices=objc_getAssociatedObject(pipeline,&managed_gbuffer_indices_key);
    if(indices.count!=3)return;
    NSArray *textures=buffer ? objc_getAssociatedObject(buffer,&managed_gbuffer_frame_key) : nil;
    if(textures.count!=3){atomic_fetch_add(&managed_gbuffer_missing,1);return;}
    for(NSUInteger i=0;i<3;i++)[encoder setFragmentTexture:textures[i] atIndex:[indices[i] unsignedIntegerValue]];
    atomic_fetch_add(&managed_gbuffer_draws,1);
}

static void managed_gbuffer_control(NSDictionary *command) {
    if([command[@"sampleDepth"] boolValue])atomic_store(&managed_gbuffer_sample_pending,true);
    if(command[@"enabled"])managed_gbuffer_set_enabled([command[@"enabled"] boolValue]);
    if(command[@"decalDepth"])atomic_store(&native_decal_depth_enabled,[command[@"decalDepth"] boolValue]);
    fprintf(output,"managed_gbuffer=ready enabled=%d captures=%u draws=%u missing=%u layouts=%u memoryless=%u size_mismatches=%u\n",
        atomic_load(&managed_gbuffer_enabled),atomic_load(&managed_gbuffer_captures),
        atomic_load(&managed_gbuffer_draws),atomic_load(&managed_gbuffer_missing),atomic_load(&managed_gbuffer_layouts),
        atomic_load(&managed_gbuffer_memoryless),atomic_load(&managed_gbuffer_size_mismatches));
    fprintf(output,"managed_character_depth=draws:%u missing:%u\n",atomic_load(&managed_character_depth_draws),atomic_load(&managed_character_depth_missing));
    fprintf(output,"native_decal_depth=enabled:%d draws:%u missing:%u\n",atomic_load(&native_decal_depth_enabled),atomic_load(&native_decal_depth_draws),atomic_load(&native_decal_depth_missing));
    fprintf(output,"native_depth_resolve=encodes:%u\n",atomic_load(&native_decal_depth_resolves));
}
