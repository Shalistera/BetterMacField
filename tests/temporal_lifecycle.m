#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <objc/runtime.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
static FILE *output;
static NSString *runtime_path(NSString *name) { return [@"/private/tmp" stringByAppendingPathComponent:name]; }
#include "../src/native_taau_metalfx.inc"

static id<MTLTexture> input_texture(id<MTLDevice> device,MTLPixelFormat format) {
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:320 height:180 mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
    return [device newTextureWithDescriptor:d];
}

int main(void) {
    @autoreleasepool {
        output=stderr;setbuf(stdout,NULL);
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device || ![MTLFXTemporalScalerDescriptor supportsDevice:device])return 2;
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLTexture> color=input_texture(device,MTLPixelFormatRG11B10Float);
        id<MTLTexture> depth=input_texture(device,MTLPixelFormatDepth32Float_Stencil8);
        id<MTLTexture> motion=input_texture(device,MTLPixelFormatRGB10A2Unorm);
        if(!color || !depth || !motion)return 3;
        native_temporal_configure(true,640,360);
        for(unsigned i=0;i<24;i++) @autoreleasepool {
            if(i==6)atomic_fetch_add(&native_temporal_generation,1);
            if(i==12){native_temporal_configure(false,640,360);native_temporal_configure(true,640,360);}
            id<MTLCommandBuffer> cb=[queue commandBuffer];
            MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture=color;pass.colorAttachments[0].loadAction=MTLLoadActionClear;
            pass.colorAttachments[0].storeAction=MTLStoreActionStore;pass.colorAttachments[0].clearColor=MTLClearColorMake(.2,.3,.4,1);
            pass.colorAttachments[1].texture=motion;pass.colorAttachments[1].loadAction=MTLLoadActionClear;
            pass.colorAttachments[1].storeAction=MTLStoreActionStore;pass.colorAttachments[1].clearColor=MTLClearColorMake(.5,.5,0,0);
            pass.depthAttachment.texture=depth;pass.depthAttachment.loadAction=MTLLoadActionClear;
            pass.depthAttachment.storeAction=MTLStoreActionStore;pass.depthAttachment.clearDepth=.5;
            [[cb renderCommandEncoderWithDescriptor:pass] endEncoding];
            EFNativeTemporalFrame *frame=[EFNativeTemporalFrame new];
            frame.commandBuffer=cb;frame.color=color;frame.depth=depth;frame.motion=motion;frame.target=color;frame.matched=YES;
            NSObject *encoder=[NSObject new];
            objc_setAssociatedObject(encoder,&native_temporal_encoder_key,frame,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            native_temporal_ended(encoder);
            [cb commit];[cb waitUntilCompleted];
            if(cb.status!=MTLCommandBufferStatusCompleted){fprintf(stderr,"frame=%u error=%s\n",i,cb.error.description.UTF8String);return 4;}
        }
        NSData *state=[NSJSONSerialization dataWithJSONObject:native_temporal_state() options:NSJSONWritingPrettyPrinted error:nil];
        puts([[NSString alloc] initWithData:state encoding:NSUTF8StringEncoding].UTF8String);
        return atomic_load(&native_temporal_encodes)==24 && atomic_load(&native_temporal_scaler_creations)==1 &&
            atomic_load(&native_temporal_history_resets)>=3 && !atomic_load(&native_temporal_errors) ? 0 : 5;
    }
}
