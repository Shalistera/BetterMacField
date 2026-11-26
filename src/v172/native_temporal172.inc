// Preserve native GI; replace only the verified TAAU/postprocess boundary.
#import <MetalFX/MetalFX.h>
#import <objc/runtime.h>

static atomic_bool native_temporal_enabled, native_spatial_enabled;
static atomic_uint native_temporal_width, native_temporal_height, native_temporal_generation;
static atomic_ulong native_temporal_encodes, native_temporal_aliases, native_temporal_completed, native_temporal_errors;
static atomic_ulong native_temporal_scaler_creations, native_temporal_history_resets, native_spatial_encodes;
static _Atomic(float) native_temporal_jitter_x_sign = -1, native_temporal_jitter_y_sign = -1;
static char native_temporal_pipeline_key, native_temporal_encoder_key, native_temporal_buffer_key, native_temporal_alias_key;
static NSDictionary *native_temporal_catalog172, *native_temporal_last172;
static NSString *native_temporal_motion_source172, *native_temporal_reason = @"disabled";

@interface BMFNativeTemporalFrame172 : NSObject
@property(nonatomic,strong) id<MTLCommandBuffer> commandBuffer;
@property(nonatomic,strong) id<MTLTexture> target, color, depth, motion;
@property(nonatomic,strong) id<MTLBuffer> globals;
@property(nonatomic,strong) NSData *inlineGlobals;
@property(nonatomic) NSUInteger globalsOffset;
@property(nonatomic) float jitterX, jitterY;
@property(nonatomic) unsigned generation;
@property(nonatomic) BOOL matched, spatial;
@end
@implementation BMFNativeTemporalFrame172
@end

static NSObject *native_temporal_lock(void) {
    static NSObject *lock; static dispatch_once_t once;
    dispatch_once(&once, ^{ lock = [NSObject new]; }); return lock;
}

static BOOL native_temporal_prepare172(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSString *directory = graphicsResource172(@"native172");
        NSData *raw = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"manifest.json"]];
        NSDictionary *catalog = raw.length && raw.length < 65536 ?
            [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil] : nil;
        if (![catalog isKindOfClass:NSDictionary.class] || ![catalog[@"version"] isEqual:@"1.7.2"]) return;
        for (NSString *role in @[@"taau",@"postprocess",@"motion",@"decal",@"visibility"])
            if (!wardHex172(catalog[@"sources"][role][@"sha256"])) return;
        NSDictionary *inputs = catalog[@"temporalInputs"];
        if (![inputs isEqual:@{@"color":@0,@"depth":@1,@"motion":@2,@"history":@3,
                              @"globals":@0,@"globalsBytes":@3328,@"jitterOffset":@304}]) return;
        NSDictionary *decoder = catalog[@"motionDecoder"];
        if (![decoder[@"file"] isEqual:@"motion.metal"]) return;
        NSData *source = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"motion.metal"]];
        if (!source.length || ![wardHash172(source.bytes,source.length) isEqual:decoder[@"sha256"]]) return;
        native_temporal_motion_source172 = [[NSString alloc] initWithData:source encoding:NSUTF8StringEncoding];
        if (native_temporal_motion_source172.length) native_temporal_catalog172 = catalog;
    });
    return native_temporal_catalog172 != nil;
}

static void native_temporal_classify(id pipeline, NSString *description) {
    if (!pipeline || !native_temporal_prepare172()) return;
    for (NSString *role in @[@"taau",@"postprocess"]) {
        NSString *marker = [@"sha256=" stringByAppendingString:native_temporal_catalog172[@"sources"][role][@"sha256"]];
        if ([description containsString:marker])
            objc_setAssociatedObject(pipeline,&native_temporal_pipeline_key,
                [role isEqual:@"taau"] ? @1 : @2,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

// Also call this for compute encoders so aliases reach every downstream consumer.
static void native_temporal_command_buffer(id encoder, id<MTLCommandBuffer> commandBuffer) {
    if (atomic_load(&native_temporal_enabled))
        objc_setAssociatedObject(encoder,&native_temporal_buffer_key,commandBuffer,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void native_temporal_encoder(id encoder, id<MTLCommandBuffer> commandBuffer, MTLRenderPassDescriptor *pass) {
    if (!atomic_load(&native_temporal_enabled)) return;
    native_temporal_command_buffer(encoder,commandBuffer);
    BOOL spatial = atomic_load(&native_spatial_enabled);
    id<MTLTexture> target = pass.colorAttachments[0].texture;
    if (target.pixelFormat != (spatial ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatRG11B10Float) ||
        (!spatial && (pass.depthAttachment.texture || pass.colorAttachments[1].texture)) ||
        target.width < 32 || target.height < 32) return;
    BMFNativeTemporalFrame172 *frame = [BMFNativeTemporalFrame172 new];
    frame.commandBuffer = commandBuffer; frame.target = target; frame.spatial = spatial;
    frame.generation = atomic_load(&native_temporal_generation);
    objc_setAssociatedObject(encoder,&native_temporal_encoder_key,frame,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static id<MTLTexture> native_temporal_texture(id encoder, id<MTLTexture> texture) {
    if (!atomic_load(&native_temporal_enabled) || !texture) return texture;
    id buffer = objc_getAssociatedObject(encoder,&native_temporal_buffer_key);
    NSArray *alias = objc_getAssociatedObject(buffer,&native_temporal_alias_key);
    if (alias.count == 3 && texture == alias[0] && [alias[2] unsignedIntValue] == atomic_load(&native_temporal_generation)) {
        atomic_fetch_add(&native_temporal_aliases,1); return alias[1];
    }
    return texture;
}

static void native_temporal_input(id encoder, id<MTLTexture> texture, NSUInteger index) {
    if (!atomic_load(&native_temporal_enabled) || index > 2) return;
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    if (index == 0) frame.color = texture; else if (index == 1) frame.depth = texture; else frame.motion = texture;
}

static void native_temporal_buffer(id encoder, id<MTLBuffer> buffer, NSUInteger offset, NSUInteger index) {
    if (!atomic_load(&native_temporal_enabled) || index) return;
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    frame.globals = buffer; frame.globalsOffset = offset; frame.inlineGlobals = nil;
}

static void native_temporal_bytes(id encoder, const void *bytes, NSUInteger length, NSUInteger index) {
    if (!atomic_load(&native_temporal_enabled) || index) return;
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    frame.globals = nil; frame.globalsOffset = 0;
    frame.inlineGlobals = bytes && length >= 320 ? [NSData dataWithBytes:bytes length:320] : nil;
}

static void native_temporal_offset(id encoder, NSUInteger offset, NSUInteger index) {
    if (!atomic_load(&native_temporal_enabled) || index) return;
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    frame.globalsOffset = offset;
}

static void native_temporal_draw(id encoder, id pipeline) {
    if (!atomic_load(&native_temporal_enabled)) return;
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    unsigned kind = [objc_getAssociatedObject(pipeline,&native_temporal_pipeline_key) unsignedIntValue];
    if (!frame || kind != (frame.spatial ? 2 : 1)) return;
    if (frame.spatial) { frame.matched = YES; return; }
    const void *bytes = frame.inlineGlobals.bytes;
    if (!bytes && frame.globals.storageMode != MTLStorageModePrivate && frame.globals.contents &&
        frame.globalsOffset <= frame.globals.length && frame.globals.length-frame.globalsOffset >= 320)
        bytes = (const char *)frame.globals.contents + frame.globalsOffset;
    if (!bytes) {
        @synchronized(native_temporal_lock()) { native_temporal_reason = @"native jitter unavailable"; }
        return;
    }
    float jitter[4]; memcpy(jitter,(const char *)bytes+304,sizeof jitter);
    if (!isfinite(jitter[0]) || !isfinite(jitter[1]) || fabsf(jitter[0]) > 1 || fabsf(jitter[1]) > 1) return;
    frame.jitterX = jitter[0]; frame.jitterY = jitter[1]; frame.matched = YES;
}

static void native_temporal_encoded172(BMFNativeTemporalFrame172 *frame, id<MTLTexture> input,
                                     id<MTLTexture> result, BOOL reset) {
    double now = NSProcessInfo.processInfo.systemUptime;
    native_temporal_last172 = @{@"time":@(now),@"input":@[@(input.width),@(input.height),@(input.pixelFormat)],
        @"output":@[@(result.width),@(result.height),@(result.pixelFormat)],@"reset":@(reset),
        @"spatial":@(frame.spatial),@"jitter":@[@(frame.jitterX),@(frame.jitterY)],
        @"generation":@(frame.generation)};
    objc_setAssociatedObject(frame.commandBuffer,&native_temporal_alias_key,@[frame.target,result,@(frame.generation)],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [frame.commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        if (buffer.status == MTLCommandBufferStatusCompleted) atomic_fetch_add(&native_temporal_completed,1);
        else {
            atomic_fetch_add(&native_temporal_errors,1);
            @synchronized(native_temporal_lock()) { native_temporal_reason = buffer.error.localizedDescription ?: @"GPU command failed"; }
        }
    }];
    atomic_fetch_add(&native_temporal_encodes,1);
    if (frame.spatial) atomic_fetch_add(&native_spatial_encodes,1);
    if (reset) atomic_fetch_add(&native_temporal_history_resets,1);
    native_temporal_reason = frame.spatial ? @"Spatial encoded after native tone mapping" : @"Temporal encoded from native TAAU inputs";
}

static void native_temporal_ended(id encoder) {
    BMFNativeTemporalFrame172 *frame = objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    objc_setAssociatedObject(encoder,&native_temporal_encoder_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (!atomic_load(&native_temporal_enabled) || !frame.matched ||
        frame.generation != atomic_load(&native_temporal_generation)) return;
    if (@available(macOS 13.0,*)) {
        @synchronized(native_temporal_lock()) {
            id<MTLDevice> device = frame.commandBuffer.device;
            NSUInteger width = atomic_load(&native_temporal_width), height = atomic_load(&native_temporal_height);
            id<MTLTexture> color = frame.spatial ? frame.target : frame.color;
            if (!color || color.sampleCount != 1 || color.storageMode == MTLStorageModeMemoryless ||
                !width || !height || width < color.width || height < color.height) {
                native_temporal_reason = @"native input/output dimensions unavailable"; return;
            }
            if (frame.spatial) {
                static id<MTLFXSpatialScaler> scaler;
                static id<MTLTexture> result;
                if (color.pixelFormat != MTLPixelFormatRGBA8Unorm) return;
                if (!scaler || result.device != device || scaler.inputWidth != color.width || scaler.inputHeight != color.height ||
                    scaler.outputWidth != width || scaler.outputHeight != height) {
                    MTLFXSpatialScalerDescriptor *descriptor = [MTLFXSpatialScalerDescriptor new];
                    descriptor.inputWidth = color.width; descriptor.inputHeight = color.height;
                    descriptor.outputWidth = width; descriptor.outputHeight = height;
                    descriptor.colorTextureFormat = color.pixelFormat; descriptor.outputTextureFormat = color.pixelFormat;
                    descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
                    scaler = [descriptor newSpatialScalerWithDevice:device]; result = nil;
                    if (scaler) atomic_fetch_add(&native_temporal_scaler_creations,1);
                }
                if (!scaler) { native_temporal_reason = @"Spatial scaler unavailable"; return; }
                if ((color.usage & scaler.colorTextureUsage) != scaler.colorTextureUsage) {
                    native_temporal_reason = @"Spatial input usage incompatible"; return;
                }
                if (!result) {
                    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:color.pixelFormat width:width height:height mipmapped:NO];
                    descriptor.usage = scaler.outputTextureUsage | MTLTextureUsageShaderRead; descriptor.storageMode = MTLStorageModePrivate;
                    result = [device newTextureWithDescriptor:descriptor]; result.label = @"Native GI MetalFX Spatial Output";
                }
                if (!result) { native_temporal_reason = @"Spatial output unavailable"; return; }
                scaler.colorTexture = color; scaler.outputTexture = result;
                scaler.inputContentWidth = color.width; scaler.inputContentHeight = color.height;
                [scaler encodeToCommandBuffer:frame.commandBuffer];
                native_temporal_encoded172(frame,color,result,NO); return;
            }
            id<MTLTexture> depth = frame.depth, motion = frame.motion;
            if (color.pixelFormat != MTLPixelFormatRG11B10Float || depth.pixelFormat != MTLPixelFormatDepth32Float_Stencil8 ||
                motion.pixelFormat != MTLPixelFormatRGB10A2Unorm || depth.sampleCount != 1 || motion.sampleCount != 1 ||
                depth.width != color.width || depth.height != color.height || motion.width != color.width || motion.height != color.height ||
                depth.storageMode == MTLStorageModeMemoryless || motion.storageMode == MTLStorageModeMemoryless) {
                native_temporal_reason = @"native color/depth/motion format unavailable"; return;
            }
            static id<MTLFXTemporalScaler> scaler;
            static id<MTLComputePipelineState> unpack;
            static id<MTLTexture> unpacked, result;
            static unsigned generation;
            static double lastEncode;
            BOOL deviceChanged = unpack && unpack.device != device;
            BOOL recreate = !scaler || deviceChanged || scaler.inputWidth != color.width || scaler.inputHeight != color.height ||
                scaler.outputWidth != width || scaler.outputHeight != height;
            if (deviceChanged) unpack = nil;
            if (recreate) {
                MTLFXTemporalScalerDescriptor *descriptor = [MTLFXTemporalScalerDescriptor new];
                descriptor.inputWidth = color.width; descriptor.inputHeight = color.height;
                descriptor.outputWidth = width; descriptor.outputHeight = height;
                descriptor.colorTextureFormat = color.pixelFormat; descriptor.depthTextureFormat = depth.pixelFormat;
                descriptor.motionTextureFormat = MTLPixelFormatRG16Float; descriptor.outputTextureFormat = color.pixelFormat;
                scaler = [descriptor newTemporalScalerWithDevice:device]; unpacked = nil; result = nil; lastEncode = 0;
                if (scaler) atomic_fetch_add(&native_temporal_scaler_creations,1);
            }
            if (!scaler) { native_temporal_reason = @"Temporal scaler unavailable"; return; }
            if (!unpack) {
                NSError *error = nil;
                id<MTLLibrary> library = [device newLibraryWithSource:native_temporal_motion_source172 options:nil error:&error];
                if (library) unpack = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"bmf_native_unpack_motion"] error:&error];
                if (!unpack) { native_temporal_reason = error.localizedDescription ?: @"motion decoder unavailable"; return; }
            }
            if ((color.usage & scaler.colorTextureUsage) != scaler.colorTextureUsage ||
                (depth.usage & scaler.depthTextureUsage) != scaler.depthTextureUsage || !(motion.usage & MTLTextureUsageShaderRead)) {
                native_temporal_reason = @"native input usage incompatible"; return;
            }
            if (!unpacked) {
                MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float width:color.width height:color.height mipmapped:NO];
                descriptor.usage = scaler.motionTextureUsage | MTLTextureUsageShaderWrite; descriptor.storageMode = MTLStorageModePrivate;
                unpacked = [device newTextureWithDescriptor:descriptor]; unpacked.label = @"Native GI MetalFX Motion";
            }
            if (!result) {
                MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:color.pixelFormat width:width height:height mipmapped:NO];
                descriptor.usage = scaler.outputTextureUsage | MTLTextureUsageShaderRead; descriptor.storageMode = MTLStorageModePrivate;
                result = [device newTextureWithDescriptor:descriptor]; result.label = @"Native GI MetalFX Output";
            }
            if (!unpacked || !result) { native_temporal_reason = @"MetalFX textures unavailable"; return; }
            id<MTLComputeCommandEncoder> compute = [frame.commandBuffer computeCommandEncoder];
            if (!compute) { native_temporal_reason = @"motion encoder unavailable"; return; }
            [compute setComputePipelineState:unpack]; [compute setTexture:motion atIndex:0]; [compute setTexture:unpacked atIndex:1];
            [compute dispatchThreads:MTLSizeMake(color.width,color.height,1) threadsPerThreadgroup:MTLSizeMake(unpack.threadExecutionWidth,1,1)];
            [compute endEncoding];
            double now = NSProcessInfo.processInfo.systemUptime;
            BOOL reset = recreate || generation != frame.generation || now-lastEncode > .5;
            scaler.colorTexture = color; scaler.depthTexture = depth; scaler.motionTexture = unpacked; scaler.outputTexture = result;
            scaler.inputContentWidth = color.width; scaler.inputContentHeight = color.height;
            // Verified native globals have their own convention; managed-camera
            // jitter correction must not run a second time on this scaler.
            scaler.jitterOffsetX = atomic_load(&native_temporal_jitter_x_sign) * frame.jitterX;
            scaler.jitterOffsetY = atomic_load(&native_temporal_jitter_y_sign) * frame.jitterY;
            scaler.motionVectorScaleX = color.width; scaler.motionVectorScaleY = color.height;
            scaler.depthReversed = YES; scaler.preExposure = 1; scaler.reset = reset;
            [scaler encodeToCommandBuffer:frame.commandBuffer];
            generation = frame.generation; lastEncode = now;
            native_temporal_encoded172(frame,color,result,reset);
        }
    }
}

static bool native_temporal_configure(bool enabled, unsigned width, unsigned height) {
    if (enabled && (!width || !height || width > 7680 || height > 4320 || !native_temporal_prepare172())) return false;
    @synchronized(native_temporal_lock()) {
        static BOOL spatial;
        BOOL nextSpatial = atomic_load(&native_spatial_enabled);
        if (enabled == atomic_load(&native_temporal_enabled) && width == atomic_load(&native_temporal_width) &&
            height == atomic_load(&native_temporal_height) && spatial == nextSpatial) return true;
        atomic_store(&native_temporal_width,width); atomic_store(&native_temporal_height,height);
        atomic_fetch_add(&native_temporal_generation,1); atomic_store(&native_temporal_enabled,enabled);
        spatial = nextSpatial; native_temporal_last172 = nil;
        native_temporal_reason = enabled ? @"waiting for verified native pass" : @"disabled";
    }
    return true;
}

static void native_temporal_jitter172(float x, float y) {
    if ((x != -1 && x != 1) || (y != -1 && y != 1)) return;
    @synchronized(native_temporal_lock()) {
        float oldX = atomic_exchange(&native_temporal_jitter_x_sign,x), oldY = atomic_exchange(&native_temporal_jitter_y_sign,y);
        if (oldX != x || oldY != y) atomic_fetch_add(&native_temporal_generation,1);
    }
}

static NSDictionary *native_temporal_state(void) {
    @synchronized(native_temporal_lock()) {
        double now = NSProcessInfo.processInfo.systemUptime, last = [native_temporal_last172[@"time"] doubleValue];
        BOOL enabled = atomic_load(&native_temporal_enabled), encoding = enabled && last && now-last < 2;
        return @{@"enabled":@(enabled),@"spatial":@(atomic_load(&native_spatial_enabled)),
            @"status":enabled ? (encoding ? @"active" : @"waitingForRender") : @"off",@"encoding":@(encoding),
            @"catalogVerified":@(native_temporal_catalog172 != nil),@"lastEncode":native_temporal_last172 ?: @{},
            @"lastEncodeTime":@(last),@"generation":@(atomic_load(&native_temporal_generation)),
            @"width":@(atomic_load(&native_temporal_width)),@"height":@(atomic_load(&native_temporal_height)),
            @"jitterXSign":@(atomic_load(&native_temporal_jitter_x_sign)),@"jitterYSign":@(atomic_load(&native_temporal_jitter_y_sign)),
            @"encodes":@(atomic_load(&native_temporal_encodes)),@"completed":@(atomic_load(&native_temporal_completed)),
            @"errors":@(atomic_load(&native_temporal_errors)),@"aliases":@(atomic_load(&native_temporal_aliases)),
            @"spatialEncodes":@(atomic_load(&native_spatial_encodes)),@"scalerCreations":@(atomic_load(&native_temporal_scaler_creations)),
            @"historyResets":@(atomic_load(&native_temporal_history_resets)),@"reason":native_temporal_reason};
    }
}
