/* Keep native C++ lighting/GI and replace only the consumer of the verified
   1.6.3 TAAU Performance pass. Enabled by the experimental display mode. */
static atomic_bool native_temporal_enabled;
static atomic_bool native_temporal_bypass;
static atomic_bool native_spatial_enabled;
static _Atomic(float) native_temporal_jitter_x_sign=-1;
static _Atomic(float) native_temporal_jitter_y_sign=-1;
static atomic_uint native_temporal_motion_mode;
static atomic_uint native_temporal_width,native_temporal_height,native_temporal_generation;
static atomic_ulong native_temporal_encodes,native_temporal_aliases,native_temporal_completed,native_temporal_errors;
static atomic_ulong native_temporal_fullres_calls;
static atomic_bool native_temporal_sample_motion;
static char native_temporal_pipeline_key,native_temporal_encoder_key,native_temporal_buffer_key,native_temporal_alias_key;
static NSString *native_temporal_reason=@"disabled";

@interface EFNativeTemporalFrame : NSObject
@property(nonatomic,strong) id<MTLCommandBuffer> commandBuffer;
@property(nonatomic,strong) id<MTLTexture> target;
@property(nonatomic,strong) id<MTLTexture> color;
@property(nonatomic,strong) id<MTLTexture> depth;
@property(nonatomic,strong) id<MTLTexture> motion;
@property(nonatomic,strong) id<MTLBuffer> globals;
@property(nonatomic,strong) NSData *inlineGlobals;
@property(nonatomic) NSUInteger globalsOffset;
@property(nonatomic) float jitterX;
@property(nonatomic) float jitterY;
@property(nonatomic) BOOL matched;
@end
@implementation EFNativeTemporalFrame
@end

static NSObject *native_temporal_lock(void) {
    static NSObject *lock;static dispatch_once_t once;
    dispatch_once(&once,^{lock=[NSObject new];});return lock;
}

#include "temporal_alignment.inc"
#include "native_spatial.inc"

static void native_temporal_classify(id pipeline,NSString *description) {
    if([description containsString:@"sha256=d6af24c94a2dd3ab6c038fae09d80f379a09de5eecabfdfe25922484bbbe9c87"])
        objc_setAssociatedObject(pipeline,&native_temporal_pipeline_key,@1,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if([description containsString:@"sha256=cf0be4c6e5bf51c6563254de7ab81a0dfaf46ab16e51267e61261b3532a9e9e9"])
        objc_setAssociatedObject(pipeline,&native_temporal_pipeline_key,@2,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void native_temporal_encoder(id encoder,id<MTLCommandBuffer> commandBuffer,MTLRenderPassDescriptor *pass) {
    if(!atomic_load(&native_temporal_enabled))return;
    objc_setAssociatedObject(encoder,&native_temporal_buffer_key,commandBuffer,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    id<MTLTexture> target=pass.colorAttachments[0].texture;
    MTLPixelFormat format=atomic_load(&native_spatial_enabled) ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatRG11B10Float;
    bool spatial=atomic_load(&native_spatial_enabled);
    if(target.pixelFormat!=format || (!spatial && (pass.depthAttachment.texture || pass.colorAttachments[1].texture)) ||
       target.width<32 || target.height<32)return;
    EFNativeTemporalFrame *frame=[EFNativeTemporalFrame new];
    frame.commandBuffer=commandBuffer;frame.target=target;
    objc_setAssociatedObject(encoder,&native_temporal_encoder_key,frame,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static id<MTLTexture> native_temporal_texture(id encoder,id<MTLTexture> texture) {
    if(!atomic_load(&native_temporal_enabled) || atomic_load(&native_temporal_bypass) || !texture)return texture;
    id buffer=objc_getAssociatedObject(encoder,&native_temporal_buffer_key);
    NSArray *alias=objc_getAssociatedObject(buffer,&native_temporal_alias_key);
    if(alias && texture==alias[0]){atomic_fetch_add(&native_temporal_aliases,1);return alias[1];}
    return texture;
}

static void native_temporal_input(id encoder,id<MTLTexture> texture,NSUInteger index) {
    if(!atomic_load(&native_temporal_enabled) || index>2)return;
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    if(index==0)frame.color=texture;else if(index==1)frame.depth=texture;else frame.motion=texture;
}

static void native_temporal_buffer(id encoder,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger index) {
    if(!atomic_load(&native_temporal_enabled) || index)return;
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    frame.globals=buffer;frame.globalsOffset=offset;frame.inlineGlobals=nil;
}

static void native_temporal_bytes(id encoder,const void *bytes,NSUInteger length,NSUInteger index) {
    if(!atomic_load(&native_temporal_enabled) || index)return;
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    if(!frame)return;
    frame.globals=nil;frame.globalsOffset=0;
    frame.inlineGlobals=bytes && length>=320 ? [NSData dataWithBytes:bytes length:320] : nil;
}

static void native_temporal_offset(id encoder,NSUInteger offset,NSUInteger index) {
    if(!atomic_load(&native_temporal_enabled) || index)return;
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    frame.globalsOffset=offset;
}

static void native_temporal_draw(id encoder,id pipeline) {
    if(!atomic_load(&native_temporal_enabled))return;
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    unsigned kind=[objc_getAssociatedObject(pipeline,&native_temporal_pipeline_key) unsignedIntValue];
    if(!frame)return;
    if(atomic_load(&native_spatial_enabled)){if(kind==2)frame.matched=YES;return;}
    if(kind!=1)return;
    const void *bytes=frame.inlineGlobals.bytes;
    if(!bytes && frame.globals.storageMode!=MTLStorageModePrivate && frame.globals.contents &&
       frame.globalsOffset<=frame.globals.length && frame.globals.length-frame.globalsOffset>=320)
        bytes=(const char *)frame.globals.contents+frame.globalsOffset;
    if(!bytes){native_temporal_reason=@"native jitter unavailable";return;}
    float jitter[4];memcpy(jitter,(const char *)bytes+304,sizeof jitter);
    if(!isfinite(jitter[0]) || !isfinite(jitter[1]) || fabsf(jitter[0])>1 || fabsf(jitter[1])>1)return;
    frame.jitterX=jitter[0];frame.jitterY=jitter[1];frame.matched=YES;
}

static void native_temporal_ended(id encoder) {
    EFNativeTemporalFrame *frame=objc_getAssociatedObject(encoder,&native_temporal_encoder_key);
    objc_setAssociatedObject(encoder,&native_temporal_encoder_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if(!atomic_load(&native_temporal_enabled) || !frame.matched)return;
    if(atomic_load(&native_temporal_bypass)){
        temporal_alignment_capture(frame,frame.target);
        native_temporal_reason=@"diagnostic bypass; original full-resolution TAAU";return;
    }
    if(atomic_load(&native_spatial_enabled)){native_spatial_ended(frame);return;}
    if(@available(macOS 13.0,*)){
        id<MTLTexture> color=frame.color,depth=frame.depth,motion=frame.motion;
        NSUInteger width=atomic_load(&native_temporal_width),height=atomic_load(&native_temporal_height);
        if(!color || !depth || !motion || color.pixelFormat!=MTLPixelFormatRG11B10Float ||
           depth.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8 || motion.pixelFormat!=MTLPixelFormatRGB10A2Unorm ||
           depth.width!=color.width || depth.height!=color.height || motion.width!=color.width || motion.height!=color.height ||
           color.storageMode==MTLStorageModeMemoryless || depth.storageMode==MTLStorageModeMemoryless ||
           motion.storageMode==MTLStorageModeMemoryless || width<color.width || height<color.height) {
            native_temporal_reason=@"native input format/dimensions unavailable";return;
        }
        @synchronized(native_temporal_lock()) {
            static id<MTLFXTemporalScaler> scaler;
            static id<MTLComputePipelineState> unpack;
            static id<MTLTexture> unpacked;
            static id<MTLTexture> result;
            static unsigned generation;
            static double lastEncode;
            id<MTLDevice> device=frame.commandBuffer.device;
            bool reset=generation!=atomic_load(&native_temporal_generation) || !scaler ||
                scaler.inputWidth!=color.width || scaler.inputHeight!=color.height ||
                scaler.outputWidth!=width || scaler.outputHeight!=height;
            if(reset){
                MTLFXTemporalScalerDescriptor *descriptor=[MTLFXTemporalScalerDescriptor new];
                descriptor.inputWidth=color.width;descriptor.inputHeight=color.height;
                descriptor.outputWidth=width;descriptor.outputHeight=height;
                descriptor.colorTextureFormat=color.pixelFormat;descriptor.depthTextureFormat=depth.pixelFormat;
                descriptor.motionTextureFormat=MTLPixelFormatRG16Float;descriptor.outputTextureFormat=color.pixelFormat;
                scaler=[descriptor newTemporalScalerWithDevice:device];
                generation=atomic_load(&native_temporal_generation);unpacked=nil;result=nil;
            }
            if(!scaler){native_temporal_reason=@"MetalFX scaler unavailable";return;}
            if(!unpack){
                // Exact half-precision decode from native BlitMotionVector
                // fragment da0098a3...: both axes negated, no extra Y flip.
                NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
                    "kernel void unpack_motion(texture2d<float,access::read> src [[texture(0)]],"
                    "texture2d<half,access::write> dst [[texture(1)]],constant uint &mode [[buffer(0)]],uint2 p [[thread_position_in_grid]]){"
                    "if(p.x>=src.get_width()||p.y>=src.get_height())return;"
                    "half2 v=half2(src.read(p).xy);half2 t=abs(v)*half(2)-half(1);"
                    "half2 result=mode==1 ? half2(0) : -(t*t*sign(v-half(.5)));dst.write(half4(result,0,0),p);}";
                NSError *error=nil;id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
                if(library)unpack=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"unpack_motion"] error:&error];
                if(!unpack){native_temporal_reason=error.localizedDescription ?: @"motion decoder unavailable";return;}
            }
            if((color.usage & scaler.colorTextureUsage)!=scaler.colorTextureUsage ||
               (depth.usage & scaler.depthTextureUsage)!=scaler.depthTextureUsage){
                native_temporal_reason=@"native input usage incompatible";return;
            }
            if(!unpacked){
                MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                    width:color.width height:color.height mipmapped:NO];
                descriptor.usage=scaler.motionTextureUsage|MTLTextureUsageShaderWrite;descriptor.storageMode=MTLStorageModePrivate;
                unpacked=[device newTextureWithDescriptor:descriptor];unpacked.label=@"Native GI MetalFX Motion";
            }
            if(!result){
                MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:color.pixelFormat width:width height:height mipmapped:NO];
                descriptor.usage=scaler.outputTextureUsage|MTLTextureUsageShaderRead;descriptor.storageMode=MTLStorageModePrivate;
                result=[device newTextureWithDescriptor:descriptor];result.label=@"Native GI MetalFX Output";
            }
            if(!unpacked || !result){native_temporal_reason=@"MetalFX texture allocation failed";return;}
            id<MTLComputeCommandEncoder> compute=[frame.commandBuffer computeCommandEncoder];
            if(!compute){native_temporal_reason=@"motion encoder unavailable";return;}
            [compute setComputePipelineState:unpack];[compute setTexture:motion atIndex:0];[compute setTexture:unpacked atIndex:1];
            unsigned motionMode=atomic_load(&native_temporal_motion_mode);
            [compute setBytes:&motionMode length:sizeof motionMode atIndex:0];
            NSUInteger group=unpack.threadExecutionWidth;
            [compute dispatchThreads:MTLSizeMake(color.width,color.height,1) threadsPerThreadgroup:MTLSizeMake(group,1,1)];
            [compute endEncoding];
            if(atomic_exchange(&native_temporal_sample_motion,false)){
                NSUInteger rowBytes=(color.width*4+255)&~255UL;
                id<MTLBuffer> readback=[device newBufferWithLength:rowBytes*color.height options:MTLResourceStorageModeShared];
                id<MTLBlitCommandEncoder> blit=[frame.commandBuffer blitCommandEncoder];
                [blit copyFromTexture:unpacked sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(color.width,color.height,1) toBuffer:readback destinationOffset:0
                    destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes*color.height];
                [blit endEncoding];
                NSUInteger inputWidth=color.width,inputHeight=color.height;
                [frame.commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer){
                    if(buffer.status!=MTLCommandBufferStatusCompleted)return;
                    double sumX=0,sumY=0,maxX=0,maxY=0;NSUInteger count=0,nearZero=0;
                    NSMutableArray *grid=[NSMutableArray array];
                    for(NSUInteger y=0;y<inputHeight;y++)for(NSUInteger x=0;x<inputWidth;x++){
                        const _Float16 *v=(const _Float16 *)((const char *)readback.contents+y*rowBytes+x*4);
                        double vx=(float)v[0]*inputWidth,vy=(float)v[1]*inputHeight;
                        sumX+=fabs(vx);sumY+=fabs(vy);maxX=fmax(maxX,fabs(vx));maxY=fmax(maxY,fabs(vy));count++;
                        if(fabs(vx)<.01 && fabs(vy)<.01)nearZero++;
                        if(x%MAX(inputWidth/8,1)==0 && y%MAX(inputHeight/6,1)==0)
                            [grid addObject:@[@(x),@(y),@(vx),@(vy)]];
                    }
                    NSDictionary *sample=@{@"size":@[@(inputWidth),@(inputHeight)],@"meanAbsolutePixels":@[@(sumX/count),@(sumY/count)],
                        @"maxAbsolutePixels":@[@(maxX),@(maxY)],@"nearZeroFraction":@((double)nearZero/count),@"grid":grid};
                    [[NSJSONSerialization dataWithJSONObject:sample options:NSJSONWritingPrettyPrinted error:nil]
                        writeToFile:runtime_path(@"endfield-ios-native-motion.json") atomically:YES];
                }];
            }
            double now=NSProcessInfo.processInfo.systemUptime;
            scaler.colorTexture=color;scaler.depthTexture=depth;scaler.motionTexture=unpacked;scaler.outputTexture=result;
            scaler.inputContentWidth=color.width;scaler.inputContentHeight=color.height;
            // These are native TAAU globals, not the managed camera's jitter.
            // GPU captures show raster displacement = -jitter on both axes.
            // MetalFX expects the source sampling offset that removes it.
            scaler.jitterOffsetX=atomic_load(&native_temporal_jitter_x_sign)*frame.jitterX;
            scaler.jitterOffsetY=atomic_load(&native_temporal_jitter_y_sign)*frame.jitterY;
            scaler.motionVectorScaleX=color.width;scaler.motionVectorScaleY=color.height;
            scaler.depthReversed=YES;scaler.preExposure=1;scaler.reset=reset || now-lastEncode>.5;
            [scaler encodeToCommandBuffer:frame.commandBuffer];lastEncode=now;
            temporal_alignment_capture(frame,result);
            objc_setAssociatedObject(frame.commandBuffer,&native_temporal_alias_key,@[frame.target,result],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [frame.commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer){
                if(buffer.status==MTLCommandBufferStatusCompleted)atomic_fetch_add(&native_temporal_completed,1);
                else {atomic_fetch_add(&native_temporal_errors,1);native_temporal_reason=buffer.error.localizedDescription ?: @"GPU command failed";}
            }];
            atomic_fetch_add(&native_temporal_encodes,1);native_temporal_reason=@"encoded; post-process alias scoped to original command buffer";
        }
    }
}

static bool native_temporal_configure(bool enabled,unsigned width,unsigned height) {
    if(enabled && (!width || !height || width>7680 || height>4320))return false;
    @synchronized(native_temporal_lock()) {
        if(enabled==atomic_load(&native_temporal_enabled) && width==atomic_load(&native_temporal_width) &&
           height==atomic_load(&native_temporal_height))return true;
        atomic_store(&native_temporal_width,width);atomic_store(&native_temporal_height,height);
        atomic_fetch_add(&native_temporal_generation,1);atomic_store(&native_temporal_enabled,enabled);
        native_temporal_reason=enabled ? @"armed; requires verified native TAAU Performance pass" : @"disabled";
    }
    return true;
}

static NSDictionary *native_temporal_state(void) {
    return @{@"enabled":@(atomic_load(&native_temporal_enabled)),
        @"bypass":@(atomic_load(&native_temporal_bypass)),
        @"spatial":@(atomic_load(&native_spatial_enabled)),
        @"spatialEncodes":@(atomic_load(&native_spatial_encodes)),
        @"spatialInputSize":@[@(atomic_load(&native_spatial_input_width)),@(atomic_load(&native_spatial_input_height))],
        @"jitterXSign":@(atomic_load(&native_temporal_jitter_x_sign)),
        @"jitterYSign":@(atomic_load(&native_temporal_jitter_y_sign)),
        @"motionMode":@(atomic_load(&native_temporal_motion_mode)),
        @"width":@(atomic_load(&native_temporal_width)),@"height":@(atomic_load(&native_temporal_height)),
        @"encodes":@(atomic_load(&native_temporal_encodes)),@"aliases":@(atomic_load(&native_temporal_aliases)),
        @"completed":@(atomic_load(&native_temporal_completed)),@"errors":@(atomic_load(&native_temporal_errors)),
        @"fullResolutionRequests":@(atomic_load(&native_temporal_fullres_calls)),
        @"reason":native_temporal_reason ?: @""};
}

static void native_temporal_control(NSDictionary *command) {
    if(command[@"jitterXSign"]){
        float sign=[command[@"jitterXSign"] floatValue];
        if((sign==1 || sign==-1) && atomic_exchange(&native_temporal_jitter_x_sign,sign)!=sign)
            atomic_fetch_add(&native_temporal_generation,1);
    }
    if(command[@"motionMode"]){
        unsigned mode=[command[@"motionMode"] unsignedIntValue];
        if(mode<=1 && atomic_exchange(&native_temporal_motion_mode,mode)!=mode)
            atomic_fetch_add(&native_temporal_generation,1);
    }
    if([command[@"sampleAlignment"] boolValue]){
        @synchronized(native_temporal_lock()){
            if(!atomic_load(&temporal_alignment_remaining)){
                temporal_alignment_rows=[NSMutableArray array];
                atomic_store(&temporal_alignment_remaining,16);
            }
        }
    }
    if(command[@"jitterYSign"]){
        float sign=[command[@"jitterYSign"] floatValue];
        if((sign==1 || sign==-1) && atomic_exchange(&native_temporal_jitter_y_sign,sign)!=sign)
            atomic_fetch_add(&native_temporal_generation,1);
    }
    // Keep the original full-resolution allocation for a controlled TAAU/MetalFX comparison.
    if(command[@"bypass"] && atomic_exchange(&native_temporal_bypass,[command[@"bypass"] boolValue])!=[command[@"bypass"] boolValue])
        atomic_fetch_add(&native_temporal_generation,1);
    if([command[@"sampleMotion"] boolValue])atomic_store(&native_temporal_sample_motion,true);
    if(command[@"enabled"] && !native_temporal_configure([command[@"enabled"] boolValue],
       [command[@"width"] unsignedIntValue],[command[@"height"] unsignedIntValue])){
        fprintf(output,"native_temporal=invalid_output_size\n");return;
    }
    NSDictionary *state=native_temporal_state();
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:runtime_path(@"endfield-ios-native-temporal.json") atomically:YES];
    fprintf(output,"native_temporal=%s\n",[[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:state options:0 error:nil] encoding:NSUTF8StringEncoding].UTF8String);
}
