/* Observe existing shader identities and wrap only the upscaling boundary.
   No game shader source or binary is replaced or stored on disk. */
static char bmf_pipeline_key;
static NSObject *bmf_metal_lock;
#include "gbuffer_retention.inc"

static void bmf_hook(Class cls,SEL selector,IMP implementation) {
    Method method=class_getInstanceMethod(cls,selector);
    if(!class_addMethod(cls,selector,implementation,method_getTypeEncoding(method)))
        method_setImplementation(method,implementation);
}
static void bmf_pipeline(id pipeline,MTLRenderPipelineDescriptor *descriptor) {
    NSString *identity=objc_getAssociatedObject(descriptor.fragmentFunction,&metal_function_source_key);
    if(identity)native_temporal_classify(pipeline,identity);
}
static void install_core_metal(id<MTLDevice> device) {
    bmf_metal_lock=[NSObject new];metal_library_classes=[NSMutableSet set];
    Class dc=object_getClass(device);SEL selector=@selector(newLibraryWithData:error:);
    Method method=class_getInstanceMethod(dc,selector);
    if(method){
        IMP orig=method_getImplementation(method);
        bmf_hook(dc,selector,imp_implementationWithBlock(^id(id self,dispatch_data_t data,NSError **error){
            id library=((id(*)(id,SEL,dispatch_data_t,NSError **))orig)(self,@selector(newLibraryWithData:error:),data,error);
            const void *bytes=NULL;size_t size=0;dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&size);
            if(mapped && bytes && size<=16*1024*1024){
                NSString *hash=metal_sha256([NSData dataWithBytesNoCopy:(void *)bytes length:size freeWhenDone:NO]);
                metal_watch_library(library,[@"sha256=" stringByAppendingString:hash]);
            }
            return library;
        }));
    }
    selector=@selector(newRenderPipelineStateWithDescriptor:error:);method=class_getInstanceMethod(dc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(dc,selector,imp_implementationWithBlock(^id(id self,MTLRenderPipelineDescriptor *d,NSError **e){
            id p=((id(*)(id,SEL,id,NSError **))orig)(self,@selector(newRenderPipelineStateWithDescriptor:error:),d,e);bmf_pipeline(p,d);return p;
        }));}
    selector=@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:);method=class_getInstanceMethod(dc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(dc,selector,imp_implementationWithBlock(^id(id self,MTLRenderPipelineDescriptor *d,MTLPipelineOption options,MTLAutoreleasedRenderPipelineReflection *r,NSError **e){
            id p=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedRenderPipelineReflection *,NSError **))orig)(self,@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:),d,options,r,e);bmf_pipeline(p,d);return p;
        }));}
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> buffer=[queue commandBuffer];
    id<MTLComputeCommandEncoder> compute=[buffer computeCommandEncoder];Class cc=object_getClass(compute);[compute endEncoding];
    MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];td.usage=MTLTextureUsageRenderTarget;
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=[device newTextureWithDescriptor:td];
    id<MTLRenderCommandEncoder> render=[buffer renderCommandEncoderWithDescriptor:pass];Class renderClass=object_getClass(render);[render endEncoding];
    Class bc=object_getClass(buffer);
    selector=@selector(renderCommandEncoderWithDescriptor:);method=class_getInstanceMethod(bc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(bc,selector,imp_implementationWithBlock(^id(id self,MTLRenderPassDescriptor *p){
            p=managed_gbuffer_pass(self,p);
            id enc=((id(*)(id,SEL,id))orig)(self,@selector(renderCommandEncoderWithDescriptor:),p);
            native_temporal_encoder(enc,self,p);return enc;
        }));}
    selector=@selector(computeCommandEncoder);method=class_getInstanceMethod(bc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(bc,selector,imp_implementationWithBlock(^id(id self){
            id enc=((id(*)(id,SEL))orig)(self,@selector(computeCommandEncoder));
            if(atomic_load(&native_temporal_enabled))objc_setAssociatedObject(enc,&native_temporal_buffer_key,self,OBJC_ASSOCIATION_RETAIN_NONATOMIC);return enc;
        }));}
    selector=@selector(setTexture:atIndex:);method=class_getInstanceMethod(cc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(cc,selector,imp_implementationWithBlock(^(id self,id<MTLTexture> tex,NSUInteger i){
            ((void(*)(id,SEL,id,NSUInteger))orig)(self,@selector(setTexture:atIndex:),native_temporal_texture(self,tex),i);
        }));}
    selector=@selector(setTextures:withRange:);method=class_getInstanceMethod(cc,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(cc,selector,imp_implementationWithBlock(^(id self,const id<MTLTexture> *tex,NSRange range){
            __unsafe_unretained id<MTLTexture> aliases[32];
            if(atomic_load(&native_temporal_enabled) && range.length<=32){for(NSUInteger i=0;i<range.length;i++)aliases[i]=native_temporal_texture(self,tex[i]);tex=aliases;}
            ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))orig)(self,@selector(setTextures:withRange:),tex,range);
        }));}
    selector=@selector(setRenderPipelineState:);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self,id p){
            objc_setAssociatedObject(self,&bmf_pipeline_key,p,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            ((void(*)(id,SEL,id))orig)(self,@selector(setRenderPipelineState:),p);
        }));}
    selector=@selector(endEncoding);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self){
            ((void(*)(id,SEL))orig)(self,@selector(endEncoding));native_temporal_ended(self);
        }));}
    selector=@selector(setFragmentBuffer:offset:atIndex:);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self,id<MTLBuffer> b,NSUInteger offset,NSUInteger i){
            native_temporal_buffer(self,b,offset,i);((void(*)(id,SEL,id,NSUInteger,NSUInteger))orig)(self,@selector(setFragmentBuffer:offset:atIndex:),b,offset,i);
        }));}
    selector=@selector(setFragmentBuffers:offsets:withRange:);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self,const id<MTLBuffer> *b,const NSUInteger *o,NSRange range){
            for(NSUInteger i=0;i<range.length;i++)native_temporal_buffer(self,b[i],o[i],range.location+i);
            ((void(*)(id,SEL,const id<MTLBuffer> *,const NSUInteger *,NSRange))orig)(self,@selector(setFragmentBuffers:offsets:withRange:),b,o,range);
        }));}
    selector=@selector(setFragmentBytes:length:atIndex:);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self,const void *b,NSUInteger n,NSUInteger i){
            native_temporal_bytes(self,b,n,i);((void(*)(id,SEL,const void *,NSUInteger,NSUInteger))orig)(self,@selector(setFragmentBytes:length:atIndex:),b,n,i);
        }));}
    selector=@selector(setFragmentBufferOffset:atIndex:);method=class_getInstanceMethod(renderClass,selector);
    if(method){IMP orig=method_getImplementation(method);
        bmf_hook(renderClass,selector,imp_implementationWithBlock(^(id self,NSUInteger offset,NSUInteger i){
            native_temporal_offset(self,offset,i);((void(*)(id,SEL,NSUInteger,NSUInteger))orig)(self,@selector(setFragmentBufferOffset:atIndex:),offset,i);
        }));}
#include "metal_render_bindings.inc"
    fprintf(output,"metal_bridge=installed\n");
}
