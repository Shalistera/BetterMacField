// Reconnect the verified scene shaders and original GTAO/contact kernels to the
// managed OnePass split. Shader hashes and bindings come from the installed build.
#import <simd/simd.h>
static NSObject *metal_ao_lock;
static id<MTLTexture> metal_ao_white;
static _Atomic(float) metal_screen_ao_strength, metal_screen_ao_radius=1;
static _Atomic(float) metal_native_contact_strength, metal_native_contact_length=1;
static atomic_uint metal_screen_ao_debug, metal_native_contact_debug;
static char metal_function_source_key, sceneLibraryKey172, scenePipelineKey172, scenePatchKey172;
static NSDictionary *sceneCatalog172, *sceneTemplates172;
static atomic_uint sceneLibraries172, sceneRejected172, sceneDraws172, sceneBoundAO172, sceneBoundContact172;
static NSString *sceneProblem172;
static BOOL sceneInstalled172;

static NSString *metal_sha256(NSData *data) { return data ? wardHash172(data.bytes,data.length) : nil; }
static NSString *runtime_path(NSString *name) {
    if([name isEqual:@"endfield-official-contact"])return graphicsResource172(@"scene172/official-contact");
    return runtimeFile172(name);
}
static void metal_watch_library(id library,NSString *identity) {
    objc_setAssociatedObject(library,&sceneLibraryKey172,identity,OBJC_ASSOCIATION_COPY_NONATOMIC);
}
static void official_gtao_encode(id,id<MTLCommandBuffer>,id<MTLTexture>);
static void official_gtao_source(id,MTLRenderPassDescriptor *);
static void official_gtao_begin(id);
static void official_contact_begin(id);
static void official_contact_source(id,MTLRenderPassDescriptor *);
static void official_contact_encode(id,id<MTLCommandBuffer>,id<MTLTexture>);
#include "native_temporal172.inc"
#define output stderr
#include "managed_gbuffer_inputs.inc"
#include "official_gtao_inputs.inc"
#include "official_gtao_runtime.inc"
#include "official_contact_runtime.inc"
#include "metal_ssr_history.inc"
#undef output

static void sceneSwizzle172(Class cls,SEL selector,id block) {
    Method method=class_getInstanceMethod(cls,selector);
    if(!method)return;
    IMP replacement=imp_implementationWithBlock(block);
    if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
}

#include "native_metal172_hooks.inc"

static void sceneFunctions172(Class cls) {
    @synchronized(metal_ao_lock){
    static NSMutableSet *classes;
    if(!classes)classes=[NSMutableSet set];
    NSString *key=NSStringFromClass(cls);
    if(!cls || [classes containsObject:key])return;
    [classes addObject:key];
    SEL selector=@selector(newFunctionWithName:);
    IMP original=class_getMethodImplementation(cls,selector);
    sceneSwizzle172(cls,selector,^id(id library,NSString *name){
        id function=((id(*)(id,SEL,id))original)(library,selector,name);
        objc_setAssociatedObject(function,&metal_function_source_key,objc_getAssociatedObject(library,&sceneLibraryKey172),OBJC_ASSOCIATION_COPY_NONATOMIC);
        return function;
    });
    SEL constants=@selector(newFunctionWithName:constantValues:error:);
    IMP specialized=class_getMethodImplementation(cls,constants);
    sceneSwizzle172(cls,constants,^id(id library,NSString *name,MTLFunctionConstantValues *values,NSError **error){
        id function=((id(*)(id,SEL,id,id,NSError **))specialized)(library,constants,name,values,error);
        objc_setAssociatedObject(function,&metal_function_source_key,objc_getAssociatedObject(library,&sceneLibraryKey172),OBJC_ASSOCIATION_COPY_NONATOMIC);
        return function;
    });
    }
}

static void scenePipeline172(id pipeline,MTLRenderPipelineDescriptor *descriptor) {
    if(!pipeline)return;
    NSString *identity=objc_getAssociatedObject(descriptor.fragmentFunction,&metal_function_source_key);
    NSString *digest=[identity hasPrefix:@"sha256="] ? [identity substringFromIndex:7] : nil;
    NSDictionary *patch=digest ? sceneCatalog172[@"sources"][digest] : nil;
    if(patch)objc_setAssociatedObject(pipeline,&scenePatchKey172,patch,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    native_temporal_classify(pipeline,identity);
    managed_gbuffer_classify(pipeline,identity);
    NSNumber *depthConsumer=digest ? native_temporal_catalog172[@"depthConsumers"][digest] : nil;
    if(depthConsumer)objc_setAssociatedObject(pipeline,&native_decal_depth_pipeline_key,depthConsumer,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    official_gtao_classify(pipeline,descriptor);
}

static void sceneDraw172(id<MTLRenderCommandEncoder> encoder) {
    id pipeline=objc_getAssociatedObject(encoder,&scenePipelineKey172);
    native_temporal_draw(encoder,pipeline);
    managed_gbuffer_draw(encoder,pipeline);
    official_gtao_camera(encoder,pipeline,nil);
    NSDictionary *patch=objc_getAssociatedObject(pipeline,&scenePatchKey172);
    if(!patch)return;
    sceneGIObserve172(encoder,patch);
    id command=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
    NSArray *gbuffer=objc_getAssociatedObject(command,&managed_gbuffer_ready_key);
    NSArray *slots=patch[@"gbufferTextureIndices"];
    if(gbuffer.count==3 && slots.count==3){
        for(NSUInteger i=0;i<3;i++){
            NSUInteger index=[slots[i] unsignedIntegerValue];
            [encoder setFragmentTexture:gbuffer[i] atIndex:index];
            metal_ssr_history_input(encoder,gbuffer[i],index);
        }
    }
    id<MTLTexture> ao=objc_getAssociatedObject(command,&official_gtao_result_key) ?: metal_ao_white;
    id<MTLTexture> contact=objc_getAssociatedObject(command,&official_contact_result_key) ?: metal_ao_white;
    float settings[]={atomic_load(&metal_screen_ao_strength),1,atomic_load(&metal_native_contact_strength),1};
    uint32_t debug=0;
    [encoder setFragmentBytes:&debug length:4 atIndex:28];
    [encoder setFragmentBytes:settings length:sizeof settings atIndex:29];
    [encoder setFragmentTexture:metal_ao_white atIndex:24];
    [encoder setFragmentTexture:contact atIndex:30];
    [encoder setFragmentTexture:ao atIndex:31];
    metal_ssr_history_draw(encoder,patch[@"depthTextureIndex"]);
    atomic_fetch_add(&sceneDraws172,1);
    if(ao!=metal_ao_white)atomic_fetch_add(&sceneBoundAO172,1);
    if(contact!=metal_ao_white)atomic_fetch_add(&sceneBoundContact172,1);
}

static void sceneEncoderHooks172(Class cls) {
    SEL pso=@selector(setRenderPipelineState:);
    IMP pipeline=class_getMethodImplementation(cls,pso);
    sceneSwizzle172(cls,pso,^(id encoder,id value){
        objc_setAssociatedObject(encoder,&scenePipelineKey172,value,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        ((void(*)(id,SEL,id))pipeline)(encoder,pso,value);
    });
    // The original kernels consume the exact camera buffers bound by this pass.
    for(int stage=0;stage<2;stage++){
        BOOL vertex=stage!=0;
        SEL buffer=vertex ? @selector(setVertexBuffer:offset:atIndex:) : @selector(setFragmentBuffer:offset:atIndex:);
        IMP setBuffer=class_getMethodImplementation(cls,buffer);
        sceneSwizzle172(cls,buffer,^(id encoder,id value,NSUInteger offset,NSUInteger index){
            if(index<16)official_gtao_buffer(encoder,value,offset,index+(vertex?16:0));
            if(!vertex){metal_ssr_camera_buffer(encoder,value,offset,index);native_temporal_buffer(encoder,value,offset,index);}
            ((void(*)(id,SEL,id,NSUInteger,NSUInteger))setBuffer)(encoder,buffer,value,offset,index);
        });
        SEL bytes=vertex ? @selector(setVertexBytes:length:atIndex:) : @selector(setFragmentBytes:length:atIndex:);
        IMP setBytes=class_getMethodImplementation(cls,bytes);
        sceneSwizzle172(cls,bytes,^(id encoder,const void *value,NSUInteger length,NSUInteger index){
            if(index<16)official_gtao_bytes(encoder,value,length,index+(vertex?16:0));
            if(!vertex){metal_ssr_camera_bytes(encoder,value,length,index);native_temporal_bytes(encoder,value,length,index);}
            ((void(*)(id,SEL,const void *,NSUInteger,NSUInteger))setBytes)(encoder,bytes,value,length,index);
        });
        SEL offset=vertex ? @selector(setVertexBufferOffset:atIndex:) : @selector(setFragmentBufferOffset:atIndex:);
        IMP setOffset=class_getMethodImplementation(cls,offset);
        sceneSwizzle172(cls,offset,^(id encoder,NSUInteger value,NSUInteger index){
            if(index<16)official_gtao_offset(encoder,value,index+(vertex?16:0));
            if(!vertex){metal_ssr_camera_offset(encoder,value,index);native_temporal_offset(encoder,value,index);}
            ((void(*)(id,SEL,NSUInteger,NSUInteger))setOffset)(encoder,offset,value,index);
        });
        SEL buffers=vertex ? @selector(setVertexBuffers:offsets:withRange:) : @selector(setFragmentBuffers:offsets:withRange:);
        IMP setBuffers=class_getMethodImplementation(cls,buffers);
        sceneSwizzle172(cls,buffers,^(id encoder,const id<MTLBuffer> *values,const NSUInteger *offsets,NSRange range){
            for(NSUInteger i=0;i<range.length;i++){
                NSUInteger index=range.location+i;
                if(index<16)official_gtao_buffer(encoder,values[i],offsets[i],index+(vertex?16:0));
                if(!vertex){metal_ssr_camera_buffer(encoder,values[i],offsets[i],index);native_temporal_buffer(encoder,values[i],offsets[i],index);}
            }
            ((void(*)(id,SEL,const id<MTLBuffer> *,const NSUInteger *,NSRange))setBuffers)(encoder,buffers,values,offsets,range);
        });
    }
    SEL texture=@selector(setFragmentTexture:atIndex:);
    IMP setTexture=class_getMethodImplementation(cls,texture);
    sceneSwizzle172(cls,texture,^(id encoder,id value,NSUInteger index){
        value=sceneTextureAlias172(encoder,value);
        native_temporal_input(encoder,value,index);
        sceneGIInput172(encoder,value,index);
        metal_ssr_history_input(encoder,value,index);
        ((void(*)(id,SEL,id,NSUInteger))setTexture)(encoder,texture,value,index);
    });
    SEL textures=@selector(setFragmentTextures:withRange:);
    IMP setTextures=class_getMethodImplementation(cls,textures);
    sceneSwizzle172(cls,textures,^(id encoder,const id<MTLTexture> *values,NSRange range){
        __unsafe_unretained id<MTLTexture> aliases[128];
        if(range.length<=128) {
            for(NSUInteger i=0;i<range.length;i++){
                NSUInteger index=range.location+i;
                aliases[i]=sceneTextureAlias172(encoder,values[i]);
                native_temporal_input(encoder,aliases[i],index);
                sceneGIInput172(encoder,aliases[i],index);
                metal_ssr_history_input(encoder,aliases[i],index);
            }
            values=aliases;
        }
        ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))setTextures)(encoder,textures,values,range);
    });
    SEL store=@selector(setColorStoreAction:atIndex:);
    IMP setStore=class_getMethodImplementation(cls,store);
    sceneSwizzle172(cls,store,^(id encoder,MTLStoreAction action,NSUInteger index){
        NSMutableDictionary *state=objc_getAssociatedObject(encoder,&metal_ssr_history_encoder_key);
        if(state && index==0)state[@"store"]=@(action);
        ((void(*)(id,SEL,MTLStoreAction,NSUInteger))setStore)(encoder,store,action,index);
    });
    SEL end=@selector(endEncoding);
    IMP finish=class_getMethodImplementation(cls,end);
    sceneSwizzle172(cls,end,^(id encoder){
        NSDictionary *history=objc_getAssociatedObject(encoder,&metal_ssr_history_encoder_key);
        managed_gbuffer_finish(encoder);
        id<MTLTexture> hardware=objc_getAssociatedObject(encoder,&managed_gbuffer_sample_depth_key);
        if(atomic_load(&native_decal_depth_enabled) &&
           [objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue] && hardware &&
           hardware.storageMode!=MTLStorageModeMemoryless)
            [encoder setDepthStoreAction:MTLStoreActionStore];
        ((void(*)(id,SEL))finish)(encoder,end);
        native_temporal_ended(encoder);
        NSArray *textures=objc_getAssociatedObject(encoder,&managed_gbuffer_source_key);
        if(textures.count==3){
            id<MTLTexture> depth=textures[2];
            @synchronized(metal_ao_lock){
                int size[]={(int)depth.width,(int)depth.height};
                memcpy(official_gtao_settings+4,size,8);
                int frame;memcpy(&frame,official_gtao_settings+20,4);frame++;
                memcpy(official_gtao_settings+20,&frame,4);
            }
        }
        managed_gbuffer_ended(encoder);
        if(history)metal_ssr_history_copy(history);
        objc_setAssociatedObject(encoder,&metal_ssr_history_encoder_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    });
#include "scene172_draws.inc"
}

static void installScene172(void) {
    if(!native_temporal_prepare172()) {sceneProblem172=@"原生超分资源校验失败";return;}
    NSString *directory=graphicsResource172(@"scene172");
    NSData *raw=[NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"manifest.json"]];
    sceneCatalog172=raw ? [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil] : nil;
    if(![sceneCatalog172[@"version"] isEqual:@"1.7.2"] || ![sceneCatalog172[@"route"] isEqual:@"library"]){
        sceneProblem172=@"场景光照资源清单不匹配";return;
    }
    NSMutableDictionary *templates=[NSMutableDictionary dictionary];
    for(NSString *digest in sceneCatalog172[@"sources"]){
        NSDictionary *entry=sceneCatalog172[@"sources"][digest];
        NSData *data=[NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:entry[@"file"]]];
        if(![metal_sha256(data) isEqual:entry[@"sha256"]]){sceneProblem172=@"场景光照源码校验失败";return;}
        templates[digest]=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    }
    sceneTemplates172=[templates copy];
    for(NSDictionary *entry in [sceneCatalog172[@"kernels"] allValues]){
        NSData *data=[NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:entry[@"file"]]];
        if(![metal_sha256(data) isEqual:entry[@"sha256"]]){sceneProblem172=@"场景光照计算程序清单校验失败";return;}
    }
    metal_ao_lock=[NSObject new];metal_ssr_history_lock=[NSObject new];
    NSString *gtao=[directory stringByAppendingPathComponent:@"official-gtao"];
    setenv("ENDFIELD_OFFICIAL_GTAO_PATH",gtao.UTF8String,1);
    NSData *bindings=[NSData dataWithContentsOfFile:[gtao stringByAppendingPathComponent:@"camera-bindings.json"]];
    if(![metal_sha256(bindings) isEqual:sceneCatalog172[@"cameraBindings"][@"sha256"]]){
        sceneProblem172=@"场景相机绑定校验失败";return;
    }
    official_gtao_camera_bindings=bindings ? [NSJSONSerialization JSONObjectWithData:bindings options:0 error:nil] : nil;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!device || !official_gtao_camera_bindings.count){sceneProblem172=@"场景光照相机接口不可用";return;}
    MTLTextureDescriptor *white=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
    white.usage=MTLTextureUsageShaderRead;white.storageMode=MTLStorageModeShared;
    metal_ao_white=[device newTextureWithDescriptor:white];
    uint32_t pixel=UINT32_MAX;
    [metal_ao_white replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&pixel bytesPerRow:4];
    if(!official_gtao_prepare(device) || !official_contact_prepare(device)){
        sceneProblem172=[NSString stringWithFormat:@"%@ / %@",official_gtao_reason,official_contact_reason];return;
    }
    Class dc=[device class];SEL load=@selector(newLibraryWithData:error:);
    IMP original=class_getMethodImplementation(dc,load);
    sceneSwizzle172(dc,load,^id(id owner,dispatch_data_t data,NSError **error){
        const void *bytes=NULL;size_t length=0;
        __attribute__((objc_precise_lifetime)) dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&length);
        NSString *digest=mapped && length ? wardHash172(bytes,length) : nil;
        NSString *source=digest ? sceneTemplates172[digest] : nil;
        id library=nil;
        if(source){
            MTLCompileOptions *options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_3;
            NSError *failure=nil;library=[owner newLibraryWithSource:source options:options error:&failure];
            if(library){atomic_fetch_add(&sceneLibraries172,1);CFRetain((__bridge CFTypeRef)library);}
            else{atomic_fetch_add(&sceneRejected172,1);sceneProblem172=failure.localizedDescription;}
        }
        if(!library)library=((id(*)(id,SEL,dispatch_data_t,NSError **))original)(owner,load,data,error);
        if(library && digest){
            metal_watch_library(library,[@"sha256=" stringByAppendingString:digest]);
            sceneFunctions172([library class]);
        }
        return library;
    });
    SEL pipeline=@selector(newRenderPipelineStateWithDescriptor:error:);
    IMP build=class_getMethodImplementation(dc,pipeline);
    sceneSwizzle172(dc,pipeline,^id(id owner,MTLRenderPipelineDescriptor *desc,NSError **error){
        id state=((id(*)(id,SEL,id,NSError **))build)(owner,pipeline,desc,error);
        scenePipeline172(state,desc);return state;
    });
    SEL reflected=@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:);
    IMP buildReflected=class_getMethodImplementation(dc,reflected);
    sceneSwizzle172(dc,reflected,^id(id owner,MTLRenderPipelineDescriptor *desc,MTLPipelineOption options,MTLAutoreleasedRenderPipelineReflection *reflection,NSError **error){
        id state=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedRenderPipelineReflection *,NSError **))buildReflected)(owner,reflected,desc,options,reflection,error);
        scenePipeline172(state,desc);return state;
    });
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> buffer=[queue commandBuffer];
    MTLTextureDescriptor *target=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
    target.usage=MTLTextureUsageRenderTarget;target.storageMode=MTLStorageModePrivate;
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=[device newTextureWithDescriptor:target];
    id<MTLRenderCommandEncoder> encoder=[buffer renderCommandEncoderWithDescriptor:pass];
    Class rc=[encoder class],bc=[buffer class];[encoder endEncoding];
    id<MTLComputeCommandEncoder> compute=[buffer computeCommandEncoder];Class cc=[compute class];[compute endEncoding];
    sceneEncoderHooks172(rc);
    nativeComputeHooks172(bc,cc);
    SEL begin=@selector(renderCommandEncoderWithDescriptor:);
    IMP beginOriginal=class_getMethodImplementation(bc,begin);
    sceneSwizzle172(bc,begin,^id(id command,MTLRenderPassDescriptor *descriptor){
        MTLRenderPassDescriptor *retained=managed_gbuffer_pass(command,descriptor);
        if(atomic_load(&native_decal_depth_enabled) && retained!=descriptor && retained.depthAttachment.texture &&
           retained.depthAttachment.texture.storageMode!=MTLStorageModeMemoryless)
            retained.depthAttachment.storeAction=MTLStoreActionStore;
        id next=((id(*)(id,SEL,id))beginOriginal)(command,begin,retained);
        managed_gbuffer_encoder(next,command,retained);
        native_temporal_encoder(next,command,retained);
        metal_ssr_history_encoder(next,command,retained);
        return next;
    });
    SEL commit=@selector(commit);
    IMP commitOriginal=class_getMethodImplementation(bc,commit);
    sceneSwizzle172(bc,commit,^(id command){
        // Publish history only together with submission on the producer queue.
        @synchronized(metal_ssr_history_lock){
            metal_ssr_history_commit(command);
            ((void(*)(id,SEL))commitOriginal)(command,commit);
        }
    });
    sceneInstalled172=YES;
}

static NSDictionary *sceneStatus172(void) {
    NSMutableDictionary *state=[@{@"installed":@(sceneInstalled172),@"libraries":@(atomic_load(&sceneLibraries172)),
        @"rejected":@(atomic_load(&sceneRejected172)),@"draws":@(atomic_load(&sceneDraws172)),
        @"gtaoEncodes":@(atomic_load(&official_gtao_encodes)),@"gtaoBound":@(atomic_load(&sceneBoundAO172)),
        @"gtaoErrors":@(atomic_load(&official_gtao_errors)),@"gtaoReason":official_gtao_reason,
        @"contactEncodes":@(atomic_load(&official_contact_encodes)),@"contactBound":@(atomic_load(&sceneBoundContact172)),
        @"contactErrors":@(atomic_load(&official_contact_errors)),@"contactReason":official_contact_reason,
        @"gbufferCaptures":@(atomic_load(&managed_gbuffer_captures)),
        @"depthResolves":@(atomic_load(&native_decal_depth_resolves)),
        @"depthConsumerDraws":@(atomic_load(&native_decal_depth_draws)),
        @"depthConsumerMissing":@(atomic_load(&native_decal_depth_missing)),
        @"reflectionDraws":@(atomic_load(&metal_ssr_draws)),@"reflectionCompleted":@(atomic_load(&metal_ssr_history_completed)),
        @"reflectionErrors":@(atomic_load(&metal_ssr_history_errors))} mutableCopy];
    if(sceneProblem172)state[@"error"]=sceneProblem172;
    @synchronized(metal_ao_lock){if(sceneLastGI172)state[@"nativeGI"]=sceneLastGI172;}
    state[@"nativeGIDraws"]=@(atomic_load(&sceneGIDraws172));
    state[@"nativeGIValidDraws"]=@(atomic_load(&sceneGIValidDraws172));
    return state;
}

#include "scene172_control.inc"
#include "metalfx_native172.inc"

static NSDictionary *updateScene172(NSDictionary *settings,BOOL scene,BOOL baseline) {
    BOOL wanted=scene && !baseline && ([settings[@"gtao"] boolValue] || [settings[@"contact"] boolValue] || [settings[@"ssr"] boolValue]);
    BOOL native=scene && !baseline && atomic_load(&fxNativeRequested172);
    BOOL failed=atomic_load(&sceneRejected172) || atomic_load(&official_gtao_errors) ||
        atomic_load(&official_contact_errors) || atomic_load(&metal_ssr_history_errors);
    NSDictionary *control=updateSceneControl172((wanted || native) && sceneInstalled172 && !failed);
    BOOL ready=(wanted || native) && sceneInstalled172 && !failed && [control[@"ready"] boolValue];
    BOOL retain=ready || (native && sceneInstalled172 && [control[@"retentionReady"] boolValue]);
    BOOL ao=ready && [settings[@"gtao"] boolValue];
    BOOL contact=ready && [settings[@"contact"] boolValue] && [control[@"contact"][@"valid"] boolValue];
    BOOL reflection=ready && [settings[@"ssr"] boolValue];
    BOOL changed=ao!=(atomic_load(&metal_screen_ao_strength)>0) || contact!=(atomic_load(&metal_native_contact_strength)>0);
    @synchronized(metal_ao_lock){
        if(changed)official_gtao_reset++;
        atomic_store(&metal_screen_ao_strength,ao ? 1.f : 0.f);
        atomic_store(&metal_native_contact_strength,contact ? 1.f : 0.f);
    }
    managed_gbuffer_set_enabled(retain);
    // Native projected decals and character lighting consume the retained
    // current-frame device depth even when the optional AO/SSR switches are off.
    atomic_store(&native_decal_depth_enabled,retain && native);
    static NSString *previousCamera;
    NSString *camera=control[@"camera"];
    if(![camera isEqual:previousCamera]){
        @synchronized(metal_ssr_history_lock){metal_ssr_history_valid=false;}
        previousCamera=camera;
    }
    if(reflection!=atomic_load(&metal_ssr_history_enabled))
        metal_ssr_history_control(@{@"enabled":@(reflection),@"strength":@(reflection ? 0.65 : 0),@"steps":@32});
    NSMutableDictionary *state=[sceneStatus172() mutableCopy];
    state[@"control"]=control;state[@"ready"]=@(ready);
    state[@"retentionReady"]=@(retain);
    static NSMutableDictionary *counts,*lastProgress;
    if(!counts){counts=[NSMutableDictionary dictionary];lastProgress=[NSMutableDictionary dictionary];}
    NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
    NSDictionary *progress=@{@"gtao":@(atomic_load(&sceneBoundAO172)),@"contact":@(atomic_load(&sceneBoundContact172)),
        @"ssr":@(atomic_load(&metal_ssr_draws))};
    NSMutableDictionary *features=[NSMutableDictionary dictionary];
    for(NSString *key in progress){
        if(![counts[key] isEqual:progress[key]])lastProgress[key]=@(now);
        counts[key]=progress[key];
        BOOL requested=scene && !baseline && [settings[key] boolValue];
        BOOL active=ready && requested && [progress[key] unsignedIntValue]>0 && now-[lastProgress[key] doubleValue]<2;
        NSString *status=!requested ? @"off" : failed ? @"error" : active ? @"active" : @"waitingForRender";
        NSString *reason=!requested ? @"关闭" : failed ? @"光照处理失败，已停用本次增强" : active ? @"已接入当前场景光照合成" :
            !sceneInstalled172 ? (sceneProblem172 ?: @"场景光照组件未就绪") :
            !ready ? [NSString stringWithFormat:@"等待场景输入：%@",control[@"status"]] : @"等待计算结果进入光照合成";
        features[key]=@{@"status":status,@"reason":reason,@"available":@(sceneInstalled172 && !failed),@"requested":@(requested),@"draws":progress[key]};
    }
    state[@"features"]=features;
    return state;
}
