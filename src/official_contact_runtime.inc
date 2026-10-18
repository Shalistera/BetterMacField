/* Original ContactShadowCS programs, with current native scene configuration. */
#include "official_contact_dispatch.h"
static NSDictionary *official_contact_pipelines;
static char official_contact_stencil_key,official_contact_result_key;
static uint8_t official_contact_config[20];
static float official_contact_direction[4];
static bool official_contact_request_valid;
static atomic_uint official_contact_encodes,official_contact_missing,official_contact_errors;
static atomic_bool official_contact_sample_pending;
static NSString *official_contact_reason=@"disabled";

static void official_contact_begin(id command) {
    objc_setAssociatedObject(command,&official_contact_result_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void official_contact_source(id encoder,MTLRenderPassDescriptor *pass) {
    if(atomic_load(&metal_native_contact_strength)>0)
        objc_setAssociatedObject(encoder,&official_contact_stencil_key,pass.stencilAttachment.texture,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static bool official_contact_prepare(id<MTLDevice> device) {
    if(official_contact_pipelines)return true;
    NSString *root=runtime_path(@"endfield-official-contact");NSError *error=nil;
    NSData *data=[NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"kernels.json"]];
    NSDictionary *manifest=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:&error] : nil;
    if(![manifest[@"bundleMD5"] isEqual:@"2cd7b0dc8813eb2553bce0635d6ea76a"]){official_contact_reason=@"kernel manifest mismatched";return false;}
    NSMutableDictionary *pipelines=[NSMutableDictionary dictionary];
    for(NSDictionary *kernel in manifest[@"kernels"]){
        NSString *name=kernel[@"name"];
        if(![@[@"RayTracingV2",@"RayTracingV2NoTerrain",@"InitWhite"] containsObject:name])continue;
        NSURL *url=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:[name stringByAppendingString:@".metallib"]]];
        if(![metal_sha256([NSData dataWithContentsOfURL:url]) isEqual:kernel[@"sha256"]])return false;
        id<MTLLibrary> library=[device newLibraryWithURL:url error:&error];
        id<MTLComputePipelineState> pipeline=library ? [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"computeMain"] error:&error] : nil;
        if(!pipeline){official_contact_reason=error.localizedDescription ?: @"kernel unavailable";return false;}
        pipelines[name]=pipeline;
    }
    if(pipelines.count!=3)return false;
    official_contact_pipelines=pipelines;official_contact_reason=@"ready";return true;
}

static void official_contact_encode(id source,id<MTLCommandBuffer> command,id<MTLTexture> depth) {
    if(atomic_load(&metal_native_contact_strength)<=0)return;
    NSArray *camera=objc_getAssociatedObject(source,&official_gtao_camera_key);
    id<MTLTexture> hardware=objc_getAssociatedObject(source,&official_contact_stencil_key);
    id<MTLTexture> stencil=hardware.pixelFormat==MTLPixelFormatDepth32Float_Stencil8 ?
        [hardware newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8] : nil;
    if(!official_contact_pipelines || !official_gtao_camera_valid(camera) || !stencil ||
       stencil.storageMode==MTLStorageModeMemoryless || stencil.width!=depth.width || stencil.height!=depth.height){
        official_contact_reason=@"waiting for same-pass camera/depth/stencil";
        atomic_fetch_add(&official_contact_missing,1);return;
    }
    @synchronized(metal_ao_lock){
        if(!official_contact_request_valid){official_contact_reason=@"waiting for native scene configuration";return;}
        id entry=camera[0];const uint8_t *bytes=NULL;
        if([entry isKindOfClass:NSData.class])bytes=[entry bytes];
        else{ id<MTLBuffer> buffer=entry[0];if(buffer.storageMode!=MTLStorageModePrivate)bytes=(const uint8_t *)buffer.contents+[entry[1] unsignedIntegerValue]; }
        if(!bytes)return;
        // Current Lit declarations place _ViewNoTransProjMatrix at byte 320.
        // Direction w=0 makes it identical to the constructor's viewProjMatrix
        // multiplication without introducing a second matrix inversion.
        simd_float4x4 viewProjection;memcpy(&viewProjection,bytes+320,sizeof viewProjection);
        // The original selector already returns the direction toward the light.
        simd_float4 light={official_contact_direction[0],official_contact_direction[1],official_contact_direction[2],0};
        simd_float4 projected=simd_mul(viewProjection,light);
        float divisor=projected.w;
        if(divisor<0 && divisor>-0.000128f)divisor=-0.000128f;
        if(divisor>=0 && divisor<0.000128f)divisor=0.000128f;
        float intensity,thickness,threshold;int contrast;
        memcpy(&intensity,official_contact_config+4,4);memcpy(&thickness,official_contact_config+8,4);
        memcpy(&threshold,official_contact_config+12,4);memcpy(&contrast,official_contact_config+16,4);
        int frame;memcpy(&frame,official_gtao_settings+20,4);
        float parameters[20]={thickness*0.01f,threshold*0.01f,contrast,intensity,
            0,0,official_contact_config[0] ? 1 : 0,frame&7,
            (projected.x/divisor*0.5f+0.5f)*depth.width,(0.5f-projected.y/divisor*0.5f)*depth.height,
            projected.w ? projected.z/projected.w : 0,projected.w>0 ? 1 : -1,
            depth.width,depth.height,1.f/depth.width,1.f/depth.height,0,0,0,0};
        for(int i=0;i<20;i++)if(!isfinite(parameters[i]))return;
        static id<MTLTexture> result;
        if(result.width!=depth.width || result.height!=depth.height)
            result=official_gtao_texture(command.device,MTLPixelFormatRG8Unorm,depth.width,depth.height,1);
        if(!result)return;
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        [encoder setComputePipelineState:official_contact_pipelines[@"InitWhite"]];[encoder setTexture:result atIndex:0];
        [encoder dispatchThreadgroups:MTLSizeMake((depth.width+31)/32,(depth.height+31)/32,1) threadsPerThreadgroup:MTLSizeMake(32,32,1)];
        [encoder endEncoding];
        EFContactDispatch groups[8];int count=official_contact_dispatch(parameters[8],parameters[9],(int)depth.width,(int)depth.height,groups);
        encoder=[command computeCommandEncoder];
        [encoder setComputePipelineState:official_contact_pipelines[official_contact_config[1] ? @"RayTracingV2NoTerrain" : @"RayTracingV2"]];
        official_gtao_bind(encoder,camera[0],1312,0);
        [encoder setTexture:depth atIndex:0];[encoder setTexture:stencil atIndex:1];[encoder setTexture:result atIndex:2];
        for(int i=0;i<count;i++){
            parameters[16]=groups[i].offsetX;parameters[17]=groups[i].offsetY;
            [encoder setBytes:parameters length:sizeof parameters atIndex:1];
            [encoder dispatchThreadgroups:MTLSizeMake(groups[i].x,groups[i].y,groups[i].z) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        }
        [encoder endEncoding];
        result.label=@"Original contact shadow visibility and distance";
        objc_setAssociatedObject(command,&official_contact_result_key,result,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        atomic_fetch_add(&official_contact_encodes,1);
        official_contact_reason=[NSString stringWithFormat:@"original contact %lux%lu groups=%d intensity=%.3g thickness=%.3g",depth.width,depth.height,count,intensity,thickness*0.01f];
        if(atomic_exchange(&official_contact_sample_pending,false)){
            NSUInteger width=result.width,height=result.height,row=(width*2+255)&~255UL;
            id<MTLBuffer> pixels=[command.device newBufferWithLength:row*height options:MTLResourceStorageModeShared];
            id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
            [blit copyFromTexture:result sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:pixels destinationOffset:0
                destinationBytesPerRow:row destinationBytesPerImage:row*height];[blit endEncoding];
            NSString *reason=[official_contact_reason copy];
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
                if(completed.status!=MTLCommandBufferStatusCompleted)return;
                const uint8_t *bytes=pixels.contents;unsigned minimum=255,maximum=0,distanceMax=0;
                uint64_t total=0,occluded=0;
                for(NSUInteger y=0;y<height;y++)for(NSUInteger x=0;x<width;x++){
                    unsigned r=bytes[y*row+x*2],g=bytes[y*row+x*2+1];
                    minimum=MIN(minimum,r);maximum=MAX(maximum,r);distanceMax=MAX(distanceMax,g);
                    total+=r;occluded+=(r<250);
                }
                NSDictionary *sample=@{@"width":@(width),@"height":@(height),@"min":@(minimum),@"max":@(maximum),
                    @"mean":@((double)total/(width*height)),@"occludedFraction":@((double)occluded/(width*height)),
                    @"distanceMax":@(distanceMax),@"source":reason,
                    @"lightSource":@"original LightClustering selector",@"lightDirection":@[@(light.x),@(light.y),@(light.z)]};
                [[NSJSONSerialization dataWithJSONObject:sample options:NSJSONWritingPrettyPrinted error:nil]
                    writeToFile:runtime_path(@"endfield-official-contact-output.json") atomically:YES];
                fprintf(output,"official_contact=sample_complete min=%u max=%u distanceMax=%u\n",minimum,maximum,distanceMax);fflush(output);
            }];
        }
        [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
            if(completed.status==MTLCommandBufferStatusError)atomic_fetch_add(&official_contact_errors,1);
        }];
    }
}
