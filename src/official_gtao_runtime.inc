/* Dispatch the installed game's GTAO programs in native pass order. The mobile
   graph skips this block; all occlusion/filtering math stays in original AIR. */
static NSDictionary *official_gtao_pipelines;
static char official_gtao_motion_key,official_gtao_result_key;
static atomic_uint official_gtao_encodes,official_gtao_missing,official_gtao_errors;
static atomic_bool official_gtao_sample_pending;
static uint8_t official_gtao_settings[64];
static int official_gtao_output_size[2];
static uintptr_t official_gtao_request_camera;
static unsigned official_gtao_reset;
static NSString *official_gtao_reason=@"disabled";

static void official_gtao_begin(id command) {
    objc_setAssociatedObject(command,&official_gtao_result_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void official_gtao_source(id encoder,MTLRenderPassDescriptor *pass) {
    if(atomic_load(&metal_screen_ao_strength)>0)
        objc_setAssociatedObject(encoder,&official_gtao_motion_key,pass.colorAttachments[1].texture,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void official_gtao_request(const uint8_t *settings,const int *outputSize,void *camera) {
    @synchronized(metal_ao_lock){
        memcpy(official_gtao_settings,settings,64);
        memcpy(official_gtao_output_size,outputSize,8);
        official_gtao_request_camera=(uintptr_t)camera;
    }
}

static bool official_gtao_prepare(id<MTLDevice> device) {
    if(official_gtao_pipelines)return true;
    const char *path=getenv("ENDFIELD_OFFICIAL_GTAO_PATH");if(!path){official_gtao_reason=@"kernel path absent";return false;}
    NSString *root=@(path);NSError *error=nil;
    NSData *data=[NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"kernels.json"]];
    NSDictionary *manifest=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:&error] : nil;
    if(![manifest[@"bundleMD5"] isEqual:@"f6792102e70a80a4420a7adb8f74636e"]){official_gtao_reason=@"kernel manifest unavailable or mismatched";return false;}
    NSSet *names=[NSSet setWithArray:@[@"GTAOPrefilterDepths16x16",@"GTAOLow",@"GTAOMedium",@"GTAOHigh",@"GTAOUltra",
        @"GTAOLowNativeNormals",@"GTAOMediumNativeNormals",@"GTAOHighNativeNormals",@"GTAOUltraNativeNormals",
        @"GTAOTemporalPass",@"GTAODenoisePass",@"GTAODenoiseLastPass",@"GTAOUpasmplePass"]];
    NSMutableDictionary *pipelines=[NSMutableDictionary dictionary];
    for(NSDictionary *kernel in manifest[@"kernels"]){
        NSString *name=kernel[@"name"];if(![names containsObject:name])continue;
        NSURL *url=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:[name stringByAppendingString:@".metallib"]]];
        NSData *libraryData=[NSData dataWithContentsOfURL:url];
        if(![metal_sha256(libraryData) isEqual:kernel[@"sha256"]]){official_gtao_reason=[@"kernel missing or mismatched: " stringByAppendingString:name];return false;}
        id<MTLLibrary> library=[device newLibraryWithURL:url error:&error];
        if(library)metal_watch_library(library,[NSString stringWithFormat:@"OfficialGTAO %@ sha256=%@",name,kernel[@"sha256"]]);
        id<MTLComputePipelineState> pipeline=library ? [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"computeMain"] error:&error] : nil;
        if(!pipeline){official_gtao_reason=error.localizedDescription ?: @"kernel unavailable";return false;}
        pipelines[name]=pipeline;
    }
    if(pipelines.count!=names.count){official_gtao_reason=@"kernel manifest incomplete";return false;}
    official_gtao_pipelines=pipelines;official_gtao_reason=@"ready";return true;
}

static id<MTLTexture> official_gtao_texture(id<MTLDevice> device,MTLPixelFormat format,NSUInteger w,NSUInteger h,NSUInteger mips) {
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:mips>1];
    d.mipmapLevelCount=mips;d.storageMode=MTLStorageModePrivate;
    d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsagePixelFormatView;
    return [device newTextureWithDescriptor:d];
}

static bool official_gtao_camera_valid(NSArray *camera) {
    if(camera.count!=2)return false;
    const NSUInteger lengths[]={1312,3328};
    for(NSUInteger i=0;i<2;i++){
        id entry=camera[i];
        if([entry isKindOfClass:NSData.class]){if([entry length]<lengths[i])return false;}
        else{ id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(offset>buffer.length || buffer.length-offset<lengths[i])return false; }
    }
    return true;
}

static void official_gtao_bind(id<MTLComputeCommandEncoder> encoder,id entry,NSUInteger length,NSUInteger index) {
    if([entry isKindOfClass:NSData.class])[encoder setBytes:[entry bytes] length:length atIndex:index];
    else [encoder setBuffer:entry[0] offset:[entry[1] unsignedIntegerValue] atIndex:index];
}

static void official_gtao_dispatch(id<MTLComputeCommandEncoder> encoder,NSUInteger w,NSUInteger h,NSUInteger pixelsX,NSUInteger pixelsY) {
    [encoder dispatchThreadgroups:MTLSizeMake((w+pixelsX-1)/pixelsX,(h+pixelsY-1)/pixelsY,1)
        threadsPerThreadgroup:MTLSizeMake(8,8,1)];[encoder endEncoding];
}

#include "official_gtao_diagnostic.inc"

static void official_gtao_encode(id source,id<MTLCommandBuffer> command,id<MTLTexture> depth) {
    if(atomic_load(&metal_screen_ao_strength)<=0)return;
    NSArray *camera=objc_getAssociatedObject(source,&official_gtao_camera_key);
    id<MTLTexture> motion=objc_getAssociatedObject(source,&official_gtao_motion_key);
    if(!official_gtao_pipelines || !official_gtao_camera_valid(camera) || !motion ||
       motion.pixelFormat!=MTLPixelFormatRGB10A2Unorm || motion.storageMode==MTLStorageModeMemoryless ||
       motion.width!=depth.width || motion.height!=depth.height){
        official_gtao_reason=@"waiting for same-pass camera/depth/motion";atomic_fetch_add(&official_gtao_missing,1);return;
    }
    @synchronized(metal_ao_lock){
    int dimensions[2],quality,denoise,frame;
    // The mobile split prepass does not write material normals for terrain.
    // Retaining its attachment preserves undefined tiles there, even after
    // hardware depth has been resolved correctly. Use the shipped GTAO depth
    // normal variant so every sampled surface has a valid normal.
    bool reconstructNormals=true;
    NSArray *gbuffer=objc_getAssociatedObject(source,&managed_gbuffer_source_key);
    id<MTLTexture> normals=gbuffer.count==3 ? gbuffer[0] : nil;
    if(!reconstructNormals && (!normals || normals.pixelFormat!=MTLPixelFormatRGBA8Unorm ||
       normals.storageMode==MTLStorageModeMemoryless || normals.width!=depth.width || normals.height!=depth.height)){
        official_gtao_reason=@"waiting for native material normals";atomic_fetch_add(&official_gtao_missing,1);return;
    }
    memcpy(dimensions,official_gtao_settings+4,8);memcpy(&quality,official_gtao_settings+12,4);
    memcpy(&denoise,official_gtao_settings+16,4);memcpy(&frame,official_gtao_settings+20,4);
    if(dimensions[0]!=depth.width || dimensions[1]!=depth.height || !official_gtao_request_camera ||
       official_gtao_output_size[0]<=0 || official_gtao_output_size[1]<=0){
        official_gtao_reason=@"waiting for native camera request";atomic_fetch_add(&official_gtao_missing,1);return;
    }
    float ratio=MIN((float)dimensions[0]/official_gtao_output_size[0],(float)dimensions[1]/official_gtao_output_size[1]);
    NSUInteger w=dimensions[0]/(ratio<=0.6f ? 1 : 2),h=dimensions[1]/(ratio<=0.6f ? 1 : 2);
    if(w<16 || h<16 || quality<0 || quality>3)return;
    static id<MTLTexture> mips,ao,edges,scratch,result,histories[2];
    static NSArray *views;static NSUInteger parity;static uintptr_t previousCamera;
    static unsigned previousReset;static int previousFrame=-1;static bool previousNormalMode;
    bool valid=mips && mips.width==w && mips.height==h && result.width==depth.width && result.height==depth.height;
    if(!valid){
        mips=official_gtao_texture(command.device,MTLPixelFormatR32Float,w,h,5);
        ao=official_gtao_texture(command.device,MTLPixelFormatR8Unorm,w,h,1);
        edges=official_gtao_texture(command.device,MTLPixelFormatR8Unorm,w,h,1);
        scratch=official_gtao_texture(command.device,MTLPixelFormatR8Unorm,w,h,1);
        result=official_gtao_texture(command.device,MTLPixelFormatR8Unorm,depth.width,depth.height,1);
        for(int i=0;i<2;i++)histories[i]=official_gtao_texture(command.device,MTLPixelFormatRGBA16Float,w,h,1);
        NSMutableArray *levels=[NSMutableArray array];
        for(NSUInteger level=0;level<5;level++){
            id view=[mips newTextureViewWithPixelFormat:MTLPixelFormatR32Float textureType:MTLTextureType2D
                levels:NSMakeRange(level,1) slices:NSMakeRange(0,1)];if(!view)return;[levels addObject:view];
        }
        views=levels;
    }
    if(!mips || !ao || !edges || !scratch || !result || !histories[0] || !histories[1])return;
    valid=valid && previousCamera==official_gtao_request_camera && previousReset==official_gtao_reset &&
        frame==previousFrame+1 && previousNormalMode==reconstructNormals;
    float texelScale=ratio<=0.6f ? 1 : 2;
    float parameters[20]={4,1,1,1,2,6,frame&63,3.3f,denoise ? 1.2f : 10000,valid ? 1 : 0,.05f,1.5f,5,0,0,0,w,h,texelScale/dimensions[0],texelScale/dimensions[1]};
    memcpy(parameters+5,official_gtao_settings+24,4);memcpy(parameters+7,official_gtao_settings+28,4);
    if(official_gtao_settings[32]){
        memcpy(parameters,official_gtao_settings+36,16);memcpy(parameters+4,official_gtao_settings+52,4);
        memcpy(parameters+10,official_gtao_settings+56,8);
    }
    NSMutableDictionary *capture=official_gtao_diagnostic_begin(command,camera,parameters,frame);
    official_gtao_diagnostic_texture(capture,command,@"depth",depth);
    official_gtao_diagnostic_texture(capture,command,@"motion",motion);
    official_gtao_diagnostic_texture(capture,command,@"normals",normals);
    if(valid)official_gtao_diagnostic_texture(capture,command,@"previous",histories[1-parity]);
    id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
    [encoder setComputePipelineState:official_gtao_pipelines[@"GTAOPrefilterDepths16x16"]];
    official_gtao_bind(encoder,camera[1],3328,0);[encoder setBytes:parameters length:80 atIndex:1];
    [encoder setTexture:depth atIndex:0];for(NSUInteger i=0;i<5;i++)[encoder setTexture:views[i] atIndex:i+1];
    official_gtao_dispatch(encoder,w,h,16,16);
    encoder=[command computeCommandEncoder];
    NSString *integrate=@[@"GTAOLow",@"GTAOMedium",@"GTAOHigh",@"GTAOUltra"][quality];
    if(!reconstructNormals)integrate=[integrate stringByAppendingString:@"NativeNormals"];
    [encoder setComputePipelineState:official_gtao_pipelines[integrate]];
    official_gtao_bind(encoder,camera[0],1312,0);[encoder setBytes:parameters length:80 atIndex:1];
    NSUInteger depthSlot=reconstructNormals ? 0 : 1;
    if(!reconstructNormals)[encoder setTexture:normals atIndex:0];
    [encoder setTexture:mips atIndex:depthSlot];[encoder setTexture:ao atIndex:depthSlot+1];[encoder setTexture:edges atIndex:depthSlot+2];
    official_gtao_dispatch(encoder,w,h,8,8);
    official_gtao_diagnostic_texture(capture,command,@"integration",ao);
    official_gtao_diagnostic_texture(capture,command,@"edges",edges);
    official_gtao_diagnostic_texture(capture,command,@"linear-depth",mips);
    encoder=[command computeCommandEncoder];[encoder setComputePipelineState:official_gtao_pipelines[@"GTAOTemporalPass"]];
    [encoder setBytes:parameters length:80 atIndex:0];[encoder setTexture:mips atIndex:0];[encoder setTexture:ao atIndex:1];
    [encoder setTexture:motion atIndex:2];[encoder setTexture:valid ? histories[1-parity] : ao atIndex:3];
    [encoder setTexture:histories[parity] atIndex:4];official_gtao_dispatch(encoder,w,h,8,8);
    for(NSUInteger i=0;i<2;i++){
        encoder=[command computeCommandEncoder];[encoder setComputePipelineState:official_gtao_pipelines[i ? @"GTAODenoiseLastPass" : @"GTAODenoisePass"]];
        [encoder setBytes:parameters length:80 atIndex:0];[encoder setTexture:i ? scratch : histories[parity] atIndex:0];
        [encoder setTexture:edges atIndex:1];[encoder setTexture:i ? ao : scratch atIndex:2];
        official_gtao_dispatch(encoder,w,h,16,8);
    }
    encoder=[command computeCommandEncoder];[encoder setComputePipelineState:official_gtao_pipelines[@"GTAOUpasmplePass"]];
    official_gtao_bind(encoder,camera[1],3328,0);[encoder setTexture:ao atIndex:0];[encoder setTexture:result atIndex:1];
    official_gtao_dispatch(encoder,depth.width,depth.height,8,8);
    official_gtao_diagnostic_texture(capture,command,@"history",histories[parity]);
    official_gtao_diagnostic_texture(capture,command,@"filtered",ao);
    official_gtao_diagnostic_texture(capture,command,@"result",result);
    official_gtao_diagnostic_finish(capture,command);
    parity=1-parity;previousCamera=official_gtao_request_camera;previousReset=official_gtao_reset;previousFrame=frame;
    previousNormalMode=reconstructNormals;
    result.label=@"Original GTAO filtered output";
    objc_setAssociatedObject(command,&official_gtao_result_key,result,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    official_gtao_reason=[NSString stringWithFormat:@"original GTAO %lux%lu -> %lux%lu history=%d quality=%d normals=%@",
        w,h,depth.width,depth.height,valid,quality,reconstructNormals ? @"depth" : @"material"];
    atomic_fetch_add(&official_gtao_encodes,1);
    if(atomic_exchange(&official_gtao_sample_pending,false)){
        NSUInteger row=(result.width+255)&~255UL,width=result.width,height=result.height;
        id<MTLBuffer> pixels=[command.device newBufferWithLength:row*height options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
        [blit copyFromTexture:result sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(width,height,1) toBuffer:pixels destinationOffset:0
            destinationBytesPerRow:row destinationBytesPerImage:row*height];[blit endEncoding];
        [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
            if(completed.status!=MTLCommandBufferStatusCompleted)return;
            const uint8_t *bytes=pixels.contents;unsigned minimum=255,maximum=0;uint64_t total=0,occluded=0;
            for(NSUInteger y=0;y<height;y++)for(NSUInteger x=0;x<width;x++){
                unsigned v=bytes[y*row+x];minimum=MIN(minimum,v);maximum=MAX(maximum,v);total+=v;occluded+=(v<250);
            }
            NSDictionary *sample=@{@"width":@(width),@"height":@(height),@"min":@(minimum),@"max":@(maximum),
                @"mean":@((double)total/(width*height)),@"occludedFraction":@((double)occluded/(width*height))};
            [[NSJSONSerialization dataWithJSONObject:sample options:NSJSONWritingPrettyPrinted error:nil]
                writeToFile:runtime_path(@"endfield-official-gtao-output.json") atomically:YES];
            fprintf(output,"official_gtao=sample_complete min=%u max=%u\n",minimum,maximum);fflush(output);
        }];
    }
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
        if(completed.status==MTLCommandBufferStatusError){
            unsigned count=atomic_fetch_add(&official_gtao_errors,1);
            if(count<3){fprintf(output,"official_gtao=gpu_error %s\n",completed.error.description.UTF8String);fflush(output);}
        }
    }];
    }
}
