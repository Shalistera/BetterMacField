static NSMutableSet *metal_events;
static atomic_bool metal_tracing;

static void record_metal(NSString *event) {
    if(!metal_tracing || !event)return;
    @synchronized(metal_events) { if(metal_events.count<4000)[metal_events addObject:event]; }
}

#include "metal_library_trace.inc"
#include "terrain_depth_pipeline.inc"
#include "native_gbuffer.inc"
static void official_gtao_capture(id,id<MTLCommandBuffer>,id<MTLTexture>);
static void official_gtao_encode(id,id<MTLCommandBuffer>,id<MTLTexture>);
static void official_gtao_source(id,MTLRenderPassDescriptor *);
static void official_gtao_begin(id);
static void official_contact_begin(id);
static void official_contact_source(id,MTLRenderPassDescriptor *);
static void official_contact_encode(id,id<MTLCommandBuffer>,id<MTLTexture>);
#include "managed_gbuffer_inputs.inc"
#include "official_gtao_inputs.inc"
#include "official_gtao_runtime.inc"
#include "official_contact_runtime.inc"
#include "native_taau_metalfx.inc"
#include "metal_ssr_history.inc"
#include "metal_light_constants.inc"
#include "metal_bindings.inc"
#include "metal_timing.inc"

static void hook_metal_label(Class cls,SEL selector,NSString *prefix) {
    Method method=class_getInstanceMethod(cls,selector);
    if(!method)return;
    IMP original=method_getImplementation(method);
    IMP replacement=imp_implementationWithBlock(^(id object,id argument){
        if(selector==@selector(setRenderPipelineState:))native_gbuffer_check(object,argument);
        if(selector==@selector(setRenderPipelineState:))metal_timing_pipeline(object,argument);
        if(selector==@selector(setRenderPipelineState:) && (metal_tracing || metal_ao_patches.count || character_skin_source || atomic_load(&character_skin_native_compiled) || atomic_load(&native_temporal_enabled)))
            objc_setAssociatedObject(object,&metal_render_pipeline_key,argument,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if(metal_tracing) {
            NSString *label=[argument isKindOfClass:NSString.class] ? argument : ([argument respondsToSelector:@selector(label)] ? [argument label] : nil);
            if(selector==@selector(setComputePipelineState:))
                label=objc_getAssociatedObject(argument,&metal_compute_description_key) ?: label;
            if([argument isKindOfClass:NSString.class])record_metal([prefix stringByAppendingString:label ?: @"<unnamed>"]);
            else record_metal([NSString stringWithFormat:@"%@%p %@",prefix,(__bridge void *)argument,label ?: @"<unnamed>"]);
        }
        ((void(*)(id,SEL,id))original)(object,selector,argument);
    });
    if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
}

static void record_metal_texture(NSString *role,id<MTLTexture> texture,NSUInteger mip) {
    if(!metal_tracing || !texture)return;
    record_metal([NSString stringWithFormat:@"texture:%@ %lux%lu mip=%lu format=%lu hazard=%lu heap=%p %@",role,
        (unsigned long)texture.width,(unsigned long)texture.height,(unsigned long)mip,
        (unsigned long)texture.pixelFormat,(unsigned long)texture.hazardTrackingMode,
        (__bridge void *)texture.heap,texture.label ?: @"<unnamed>"]);
}

static void hook_metal_dimensions(Class bufferClass,Class renderClass,Class computeClass) {
    SEL selector=@selector(renderCommandEncoderWithDescriptor:);
    Method method=class_getInstanceMethod(bufferClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,MTLRenderPassDescriptor *pass){
            pass=managed_gbuffer_pass(object,native_gbuffer_pass(pass));
            if(metal_tracing){
                for(NSUInteger i=0;i<8;i++)record_metal_texture([NSString stringWithFormat:@"color%lu",(unsigned long)i],pass.colorAttachments[i].texture,pass.colorAttachments[i].level);
                record_metal_texture(@"depth",pass.depthAttachment.texture,pass.depthAttachment.level);
                NSMutableArray *attachments=[NSMutableArray array];
                for(NSUInteger i=0;i<8;i++) {
                    MTLRenderPassColorAttachmentDescriptor *a=pass.colorAttachments[i];
                    if(a.texture)[attachments addObject:[NSString stringWithFormat:@"c%lu=%p/%@ load=%lu store=%lu mem=%lu",
                        (unsigned long)i,(__bridge void *)a.texture,a.texture.label ?: @"?",
                        (unsigned long)a.loadAction,(unsigned long)a.storeAction,(unsigned long)a.texture.storageMode]];
                }
                MTLRenderPassDepthAttachmentDescriptor *d=pass.depthAttachment;
                MTLRenderPassStencilAttachmentDescriptor *s=pass.stencilAttachment;
                record_metal([NSString stringWithFormat:@"attachments:{%@} depth=%p load=%lu store=%lu stencil=%p load=%lu store=%lu",
                    [attachments componentsJoinedByString:@"; "],(__bridge void *)d.texture,
                    (unsigned long)d.loadAction,(unsigned long)d.storeAction,(__bridge void *)s.texture,
                    (unsigned long)s.loadAction,(unsigned long)s.storeAction]);
            }
            id encoder=((id(*)(id,SEL,id))original)(object,@selector(renderCommandEncoderWithDescriptor:),pass);
            if(metal_tracing && pass.colorAttachments[0].texture) {
                id<MTLTexture> target=pass.colorAttachments[0].texture;
                objc_setAssociatedObject(encoder,&metal_render_target_description_key,
                    [NSString stringWithFormat:@"%lux%lu format=%lu usage=%lu storage=%lu",
                        (unsigned long)target.width,(unsigned long)target.height,(unsigned long)target.pixelFormat,
                        (unsigned long)target.usage,(unsigned long)target.storageMode],OBJC_ASSOCIATION_COPY_NONATOMIC);
            }
            managed_gbuffer_encoder(encoder,object,pass);
            native_temporal_encoder(encoder,object,pass);
            metal_timing_encoder(encoder,object);
            character_skin_encoder(encoder,object);
            metal_ssr_history_encoder(encoder,object,pass);
            terrain_depth_encoder(encoder,pass);
            native_gbuffer_encoder(encoder,pass);
            return encoder;
        });
        if(!class_addMethod(bufferClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(setViewport:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLViewport viewport){
            if(metal_tracing)record_metal([NSString stringWithFormat:@"viewport:%.0f,%.0f %.0fx%.0f",viewport.originX,viewport.originY,viewport.width,viewport.height]);
            ((void(*)(id,SEL,MTLViewport))original)(object,@selector(setViewport:),viewport);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(computeCommandEncoder);method=class_getInstanceMethod(bufferClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object){
            id encoder=((id(*)(id,SEL))original)(object,@selector(computeCommandEncoder));
            if(atomic_load(&native_temporal_enabled))objc_setAssociatedObject(encoder,&native_temporal_buffer_key,object,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return encoder;
        });
        if(!class_addMethod(bufferClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(setTexture:atIndex:);method=class_getInstanceMethod(computeClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,id<MTLTexture> texture,NSUInteger index){
            texture=native_temporal_texture(object,native_gbuffer_texture(texture));
            if(metal_tracing)record_metal_texture([NSString stringWithFormat:@"compute%lu",(unsigned long)index],texture,0);
            ((void(*)(id,SEL,id,NSUInteger))original)(object,@selector(setTexture:atIndex:),texture,index);
        });
        if(!class_addMethod(computeClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }

    selector=@selector(setTextures:withRange:);method=class_getInstanceMethod(computeClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,const id<MTLTexture> *textures,NSRange range){
            __unsafe_unretained id<MTLTexture> aliases[32];
            if((native_gbuffer_enabled || atomic_load(&native_temporal_enabled)) && range.length<=32) {
                for(NSUInteger i=0;i<range.length;i++)aliases[i]=native_temporal_texture(object,native_gbuffer_texture(textures[i]));
                textures=aliases;
            }
            ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))original)(object,@selector(setTextures:withRange:),textures,range);
        });
        if(!class_addMethod(computeClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }

}

static void install_metal_trace(id<MTLDevice> device) {
    metal_events=[NSMutableSet set];
    install_metal_library_trace(device);
    install_compute_descriptions(object_getClass(device));
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLCommandBuffer> buffer=[queue commandBuffer];
    install_metal_timing(object_getClass(buffer));
    id<MTLComputeCommandEncoder> compute=[buffer computeCommandEncoder];
    Class cls=object_getClass(compute);
    hook_metal_label(cls,@selector(setComputePipelineState:),@"compute:");
    hook_metal_label(cls,@selector(pushDebugGroup:),@"compute-group:");
    hook_metal_label(cls,@selector(setLabel:),@"compute-encoder:");
    [compute endEncoding];
    MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];
    td.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> texture=[device newTextureWithDescriptor:td];
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=texture;
    id<MTLRenderCommandEncoder> render=[buffer renderCommandEncoderWithDescriptor:pass];
    cls=object_getClass(render);
    hook_metal_label(cls,@selector(setRenderPipelineState:),@"render:");
    hook_metal_label(cls,@selector(pushDebugGroup:),@"render-group:");
    hook_metal_label(cls,@selector(setLabel:),@"render-encoder:");
    hook_metal_dimensions(object_getClass(buffer),cls,object_getClass(compute));
    install_metal_light_constants(cls);
    install_official_gtao_vertex_constants(cls);
    install_metal_bindings(object_getClass(device),cls);
    install_metal_ssr_history(cls);
    [render endEncoding];
    fprintf(output,"metal_trace=installed\n");
}

