/* Spatial consumes the verified native post-process output after original AA,
   bloom and tone mapping, immediately before the full-resolution UI composite. */
static atomic_ulong native_spatial_encodes;
static atomic_uint native_spatial_input_width,native_spatial_input_height;

static void native_spatial_ended(EFNativeTemporalFrame *frame) {
    if(@available(macOS 13.0,*)){
        @synchronized(native_temporal_lock()){
            id<MTLTexture> color=frame.target;
            unsigned width=atomic_load(&native_temporal_width),height=atomic_load(&native_temporal_height);
            if(!color || color.pixelFormat!=MTLPixelFormatRGBA8Unorm ||
               width<color.width || height<color.height || !width || !height){
                native_temporal_reason=@"Spatial input/output size unavailable";return;
            }
            static id<MTLFXSpatialScaler> scaler;
            static id<MTLTexture> result;
            id<MTLDevice> device=frame.commandBuffer.device;
            if(!scaler || scaler.inputWidth!=color.width || scaler.inputHeight!=color.height ||
               scaler.outputWidth!=width || scaler.outputHeight!=height){
                MTLFXSpatialScalerDescriptor *descriptor=[MTLFXSpatialScalerDescriptor new];
                descriptor.inputWidth=color.width;descriptor.inputHeight=color.height;
                descriptor.outputWidth=width;descriptor.outputHeight=height;
                descriptor.colorTextureFormat=color.pixelFormat;descriptor.outputTextureFormat=color.pixelFormat;
                descriptor.colorProcessingMode=MTLFXSpatialScalerColorProcessingModePerceptual;
                scaler=[descriptor newSpatialScalerWithDevice:device];result=nil;
            }
            if(!scaler){native_temporal_reason=@"Spatial scaler unavailable";return;}
            if((color.usage & scaler.colorTextureUsage)!=scaler.colorTextureUsage){
                native_temporal_reason=@"Spatial input usage incompatible";return;
            }
            if(!result){
                MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:color.pixelFormat
                    width:width height:height mipmapped:NO];
                descriptor.usage=scaler.outputTextureUsage|MTLTextureUsageShaderRead;
                descriptor.storageMode=MTLStorageModePrivate;
                result=[device newTextureWithDescriptor:descriptor];result.label=@"Native GI MetalFX Spatial Output";
            }
            if(!result){native_temporal_reason=@"Spatial output allocation failed";return;}
            scaler.colorTexture=color;scaler.outputTexture=result;
            scaler.inputContentWidth=color.width;scaler.inputContentHeight=color.height;
            [scaler encodeToCommandBuffer:frame.commandBuffer];
            objc_setAssociatedObject(frame.commandBuffer,&native_temporal_alias_key,@[frame.target,result],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [frame.commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer){
                if(buffer.status==MTLCommandBufferStatusCompleted)atomic_fetch_add(&native_temporal_completed,1);
                else {atomic_fetch_add(&native_temporal_errors,1);native_temporal_reason=buffer.error.localizedDescription ?: @"Spatial GPU command failed";}
            }];
            atomic_store(&native_spatial_input_width,(unsigned)color.width);
            atomic_store(&native_spatial_input_height,(unsigned)color.height);
            atomic_fetch_add(&native_spatial_encodes,1);atomic_fetch_add(&native_temporal_encodes,1);
            native_temporal_reason=@"Spatial encoded after native tone mapping; full-resolution UI retained";
        }
    }
}
