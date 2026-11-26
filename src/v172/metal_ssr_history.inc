/* Opaque scene-color history for the independent Metal reflection path. */
static atomic_bool metal_ssr_history_enabled;
static atomic_uint metal_ssr_history_copies,metal_ssr_history_completed,metal_ssr_history_errors,metal_ssr_history_skipped,metal_ssr_history_buffers,metal_ssr_history_late_passes;
static NSObject *metal_ssr_history_lock;
static id<MTLTexture> metal_ssr_history_textures[2];
static id<MTLTexture> metal_ssr_history_depth[2];
static atomic_uint metal_ssr_steps=32,metal_ssr_depth_copies;
static atomic_bool metal_ssr_optimized=false;
static atomic_bool metal_ssr_rough_filter=true;
static atomic_bool metal_ssr_quad_share=false;
static atomic_uint metal_ssr_mip_generations;
static id<MTLCommandQueue> metal_ssr_history_queue;
static NSUInteger metal_ssr_history_slot;
static bool metal_ssr_history_valid;
static _Atomic(float) metal_ssr_strength,metal_ssr_distance=12,metal_ssr_thickness=0.2,metal_ssr_debug;
static atomic_uint metal_ssr_draws;
static atomic_uint metal_ssr_camera_reads,metal_ssr_camera_missing;
static NSData *metal_ssr_history_projection;
static NSString *metal_ssr_history_reason;
static char metal_ssr_history_encoder_key,metal_ssr_history_buffer_key,metal_ssr_camera_key;
static char metal_ssr_depth_index_key,metal_ssr_inputs_key;

static void metal_ssr_history_input(id encoder,id texture,NSUInteger index) {
    if(!atomic_load(&metal_ssr_history_enabled) || index>=26)return;
    NSMutableDictionary *inputs=objc_getAssociatedObject(encoder,&metal_ssr_inputs_key);
    if(!inputs){inputs=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&metal_ssr_inputs_key,inputs,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(texture)inputs[@(index)]=texture;else [inputs removeObjectForKey:@(index)];
}

static void metal_ssr_camera_buffer(id encoder,id buffer,NSUInteger offset,NSUInteger index) {
    if(index!=0 || !atomic_load(&metal_ssr_history_enabled))return;
    objc_setAssociatedObject(encoder,&metal_ssr_camera_key,buffer ? @[buffer,@(offset)] : nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void metal_ssr_camera_bytes(id encoder,const void *bytes,NSUInteger length,NSUInteger index) {
    if(index!=0 || !atomic_load(&metal_ssr_history_enabled))return;
    objc_setAssociatedObject(encoder,&metal_ssr_camera_key,bytes && length>=448 ? [NSData dataWithBytes:bytes length:448] : nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void metal_ssr_camera_offset(id encoder,NSUInteger offset,NSUInteger index) {
    if(index!=0 || !atomic_load(&metal_ssr_history_enabled))return;
    id entry=objc_getAssociatedObject(encoder,&metal_ssr_camera_key);
    if([entry isKindOfClass:NSArray.class])metal_ssr_camera_buffer(encoder,entry[0],offset,index);
}

static NSData *metal_ssr_camera_projection(id encoder) {
    id entry=objc_getAssociatedObject(encoder,&metal_ssr_camera_key);
    const void *bytes=NULL;
    if([entry isKindOfClass:NSData.class])bytes=[entry bytes];
    else if([entry isKindOfClass:NSArray.class]) {
        id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
        if(buffer.storageMode!=MTLStorageModePrivate && buffer.contents && offset<=buffer.length && buffer.length-offset>=448)
            bytes=(const char *)buffer.contents+offset;
    }
    if(bytes) {
        simd_float4x4 inverse;memcpy(&inverse,(const char *)bytes+384,64);
        float determinant=simd_determinant(inverse);
        if(isfinite(determinant) && fabsf(determinant)>1e-12f) {
            simd_float4x4 projection=simd_inverse(inverse);
            const float *elements=(const float *)&projection;
            bool valid=true;for(int i=0;i<16;i++)if(!isfinite(elements[i]))valid=false;
            if(valid){
                atomic_fetch_add(&metal_ssr_camera_reads,1);
                NSMutableData *data=[NSMutableData dataWithBytes:&projection length:64];
                [data appendBytes:&inverse length:64];return data;
            }
        }
    }
    atomic_fetch_add(&metal_ssr_camera_missing,1);return nil;
}

static void metal_ssr_history_encoder(id encoder,id buffer,MTLRenderPassDescriptor *pass) {
    if(!atomic_load(&metal_ssr_history_enabled))return;
    MTLRenderPassColorAttachmentDescriptor *color=pass.colorAttachments[0];
    if(!color.texture)return;
    NSMutableDictionary *state=[@{@"buffer":[NSValue valueWithNonretainedObject:buffer],
        @"texture":color.texture,@"level":@(color.level),@"slice":@(color.slice),
        @"store":@(color.storeAction)} mutableCopy];
    objc_setAssociatedObject(encoder,&metal_ssr_history_encoder_key,state,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void metal_ssr_history_draw(id encoder,NSNumber *depthIndex) {
    NSMutableDictionary *state=objc_getAssociatedObject(encoder,&metal_ssr_history_encoder_key);
    if(state)state[@"lighting"]=@YES;
    NSDictionary *inputs=objc_getAssociatedObject(encoder,&metal_ssr_inputs_key);
    if(state && depthIndex && inputs[depthIndex])state[@"depth"]=inputs[depthIndex];
    NSData *projection=state ? metal_ssr_camera_projection(encoder) : nil;
    if(projection)state[@"projection"]=projection;
    float settings[]={0,atomic_load(&metal_ssr_distance),atomic_load(&metal_ssr_thickness),0};
    simd_float4x4 previousProjection[3]={matrix_identity_float4x4,matrix_identity_float4x4,matrix_identity_float4x4};
    if(projection.length==128)memcpy(&previousProjection[2],projection.bytes,64);
    id<MTLTexture> history=metal_ao_white,depth=metal_ao_white;
    @synchronized(metal_ssr_history_lock) {
        id<MTLCommandBuffer> consumer=[state[@"buffer"] nonretainedObjectValue];
        if(atomic_load(&metal_ssr_history_enabled) && metal_ssr_history_valid && projection && metal_ssr_history_projection.length==128 &&
           consumer.commandQueue==metal_ssr_history_queue) {
            memcpy(previousProjection,metal_ssr_history_projection.bytes,128);
            history=metal_ssr_history_textures[metal_ssr_history_slot^1] ?: metal_ao_white;
            depth=metal_ssr_history_depth[metal_ssr_history_slot^1] ?: metal_ao_white;
            id<MTLTexture> source=state[@"texture"];
            if(source && history.width==source.width && history.height==source.height && depth.width==source.width && depth.height==source.height) {
                settings[0]=atomic_load(&metal_ssr_strength);settings[3]=atomic_load(&metal_ssr_debug);
                if(settings[0]>0)atomic_fetch_add(&metal_ssr_draws,1);
            }
        }
    }
    [encoder setFragmentBytes:settings length:sizeof settings atIndex:27];
    uint32_t steps=atomic_load(&metal_ssr_steps)|(atomic_load(&metal_ssr_optimized) ? 0x10000u : 0u)|
        (atomic_load(&metal_ssr_rough_filter) ? 0x20000u : 0u)|
        (atomic_load(&metal_ssr_quad_share) ? 0x40000u : 0u);
    [encoder setFragmentBytes:&steps length:sizeof steps atIndex:25];
    [encoder setFragmentBytes:previousProjection length:sizeof previousProjection atIndex:26];
    [encoder setFragmentTexture:depth atIndex:28];
    [encoder setFragmentTexture:history atIndex:29];
}

static void metal_ssr_history_copy(NSDictionary *state) {
    if(!atomic_load(&metal_ssr_history_enabled))return;
    id<MTLCommandBuffer> buffer=[state[@"buffer"] nonretainedObjectValue];
    id<MTLTexture> source=state[@"texture"];
    NSMutableDictionary *frame=objc_getAssociatedObject(buffer,&metal_ssr_history_buffer_key);
    bool lighting=[state[@"lighting"] boolValue];
    if(!lighting && (!frame || frame[@"source"]!=source))return;
    NSUInteger store=[state[@"store"] unsignedIntegerValue];
    @synchronized(metal_ssr_history_lock) {
        if(!buffer || source.textureType!=MTLTextureType2D || source.sampleCount!=1 ||
           source.storageMode==MTLStorageModeMemoryless || [state[@"level"] unsignedIntegerValue] ||
           [state[@"slice"] unsignedIntegerValue] ||
           (store!=MTLStoreActionStore && store!=MTLStoreActionStoreAndMultisampleResolve)) {
            atomic_fetch_add(&metal_ssr_history_skipped,1);
            metal_ssr_history_reason=[NSString stringWithFormat:@"unsupported source type=%lu samples=%lu storage=%lu store=%lu level=%@ slice=%@",
                (unsigned long)source.textureType,(unsigned long)source.sampleCount,(unsigned long)source.storageMode,
                (unsigned long)store,state[@"level"],state[@"slice"]];return;
        }
        if(metal_ssr_history_queue && metal_ssr_history_queue!=buffer.commandQueue) {
            atomic_fetch_add(&metal_ssr_history_skipped,1);metal_ssr_history_reason=@"different command queue";return;
        }
        metal_ssr_history_queue=buffer.commandQueue;
        id<MTLTexture> previous=metal_ssr_history_textures[0];
        if(!previous || previous.width!=source.width || previous.height!=source.height || previous.pixelFormat!=source.pixelFormat) {
            MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat
                width:source.width height:source.height mipmapped:YES];
            descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageShaderRead;
            for(NSUInteger i=0;i<2;i++) {
                metal_ssr_history_textures[i]=[buffer.device newTextureWithDescriptor:descriptor];
                metal_ssr_history_textures[i].label=[NSString stringWithFormat:@"Endfield SSR opaque history %lu",(unsigned long)i];
            }
            metal_ssr_history_slot=0;
            metal_ssr_history_valid=false;
        }
        id<MTLTexture> destination=metal_ssr_history_textures[metal_ssr_history_slot];
        if(!destination){atomic_fetch_add(&metal_ssr_history_errors,1);metal_ssr_history_reason=@"texture allocation failed";return;}
        id<MTLBlitCommandEncoder> blit=[buffer blitCommandEncoder];
        if(!blit){atomic_fetch_add(&metal_ssr_history_errors,1);metal_ssr_history_reason=@"blit encoder unavailable";return;}
        blit.label=@"Endfield opaque scene history";
        MTLSize size=MTLSizeMake(source.width,source.height,1);
        [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:size
            toTexture:destination destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        id<MTLTexture> sourceDepth=state[@"depth"] ?: frame[@"depth"];
        bool depthCopied=false;
        if(sourceDepth.textureType==MTLTextureType2D && sourceDepth.sampleCount==1 && sourceDepth.storageMode!=MTLStorageModeMemoryless &&
           sourceDepth.pixelFormat==MTLPixelFormatR32Float && sourceDepth.width==source.width && sourceDepth.height==source.height) {
            id<MTLTexture> current=metal_ssr_history_depth[metal_ssr_history_slot];
            if(!current || current.width!=source.width || current.height!=source.height) {
                MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                    width:source.width height:source.height mipmapped:NO];
                descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageShaderRead;
                current=[buffer.device newTextureWithDescriptor:descriptor];current.label=@"Endfield SSR depth history";
                metal_ssr_history_depth[metal_ssr_history_slot]=current;
            }
            if(current){
                [blit copyFromTexture:sourceDepth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:size
                    toTexture:current destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                depthCopied=true;atomic_fetch_add(&metal_ssr_depth_copies,1);
            }
        }
        [blit endEncoding];
        if(!lighting)atomic_fetch_add(&metal_ssr_history_late_passes,1);
        atomic_fetch_add(&metal_ssr_history_copies,1);metal_ssr_history_reason=@"capturing";
        frame=[@{@"source":source,@"destination":destination,@"store":@(store),
            @"depthCopied":@(depthCopied),
            @"projection":state[@"projection"] ?: frame[@"projection"] ?: NSData.data,
            @"passes":@([frame[@"passes"] unsignedIntegerValue]+1)} mutableCopy];
        if(sourceDepth)frame[@"depth"]=sourceDepth;
        objc_setAssociatedObject(buffer,&metal_ssr_history_buffer_key,frame,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

static void metal_ssr_history_commit(id<MTLCommandBuffer> buffer) {
    NSDictionary *frame=objc_getAssociatedObject(buffer,&metal_ssr_history_buffer_key);
    if(!frame)return;
    objc_setAssociatedObject(buffer,&metal_ssr_history_buffer_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    @synchronized(metal_ssr_history_lock) {
        id<MTLTexture> texture=frame[@"destination"];
        if(atomic_load(&metal_ssr_rough_filter) && texture.mipmapLevelCount>1) {
            id<MTLBlitCommandEncoder> mips=[buffer blitCommandEncoder];
            if(mips){
                mips.label=@"Endfield SSR roughness history";
                [mips generateMipmapsForTexture:texture];[mips endEncoding];
                atomic_fetch_add(&metal_ssr_mip_generations,1);
            }else{
                atomic_fetch_add(&metal_ssr_history_errors,1);metal_ssr_history_valid=false;return;
            }
        }
        metal_ssr_history_slot^=1;metal_ssr_history_valid=[frame[@"depthCopied"] boolValue];metal_ssr_history_projection=frame[@"projection"];
        atomic_fetch_add(&metal_ssr_history_buffers,1);
        [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){
            if(completed.status==MTLCommandBufferStatusCompleted) {
                atomic_fetch_add(&metal_ssr_history_completed,1);
            } else atomic_fetch_add(&metal_ssr_history_errors,1);
        }];
    }
}

// Scene settings own the reflection history; release it when reflection is off.
static void metal_ssr_history_control(NSDictionary *settings) {
    @synchronized(metal_ssr_history_lock) {
        BOOL enabled = [settings[@"enabled"] boolValue];
        atomic_store(&metal_ssr_history_enabled, enabled);
        if (!enabled) {
            metal_ssr_history_textures[0] = nil; metal_ssr_history_textures[1] = nil;
            metal_ssr_history_depth[0] = nil; metal_ssr_history_depth[1] = nil;
            metal_ssr_history_queue = nil; metal_ssr_history_projection = nil;
            metal_ssr_history_valid = false; metal_ssr_history_reason = @"disabled";
            atomic_store(&metal_ssr_debug, 0);
        }
        atomic_store(&metal_ssr_strength, enabled ? fmax(0, fmin(1, [settings[@"strength"] floatValue])) : 0);
        atomic_store(&metal_ssr_steps, MAX(16, MIN(64, [settings[@"steps"] unsignedIntValue])));
    }
}
