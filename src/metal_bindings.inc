/* Capture actual fragment bindings at draw time during the explicit Metal trace. */
static char metal_fragment_bindings_key,metal_render_pipeline_key,metal_pipeline_description_key;
static char metal_compute_description_key;
static char metal_render_target_description_key;
static char metal_character_pipeline_key;

static void metal_describe_compute(id pipeline,id<MTLFunction> function) {
    if(pipeline && function)objc_setAssociatedObject(pipeline,&metal_compute_description_key,
        [NSString stringWithFormat:@"kernel=%@ source=%@",function.name ?: @"<unnamed>",
            objc_getAssociatedObject(function,&metal_function_source_key) ?: @"<unattributed>"],OBJC_ASSOCIATION_COPY_NONATOMIC);
}

static void install_compute_descriptions(Class cls) {
    SEL selector=@selector(newComputePipelineStateWithFunction:error:);
    Method method=class_getInstanceMethod(cls,selector);
    if(method) {
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,id<MTLFunction> function,NSError **error){
            id pipeline=((id(*)(id,SEL,id,NSError **))original)(object,@selector(newComputePipelineStateWithFunction:error:),function,error);
            metal_describe_compute(pipeline,function);return pipeline;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(newComputePipelineStateWithFunction:options:reflection:error:);
    method=class_getInstanceMethod(cls,selector);
    if(method) {
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,id<MTLFunction> function,MTLPipelineOption options,MTLAutoreleasedComputePipelineReflection *reflection,NSError **error){
            id pipeline=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedComputePipelineReflection *,NSError **))original)(object,@selector(newComputePipelineStateWithFunction:options:reflection:error:),function,options,reflection,error);
            metal_describe_compute(pipeline,function);return pipeline;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(newComputePipelineStateWithDescriptor:options:reflection:error:);
    method=class_getInstanceMethod(cls,selector);
    if(method) {
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,MTLComputePipelineDescriptor *descriptor,MTLPipelineOption options,MTLAutoreleasedComputePipelineReflection *reflection,NSError **error){
            id pipeline=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedComputePipelineReflection *,NSError **))original)(object,@selector(newComputePipelineStateWithDescriptor:options:reflection:error:),descriptor,options,reflection,error);
            metal_describe_compute(pipeline,descriptor.computeFunction);return pipeline;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
}

static void metal_fragment_binding(id encoder,id<MTLTexture> texture,NSUInteger index) {
    native_temporal_input(encoder,texture,index);
    metal_ssr_history_input(encoder,texture,index);
    if(metal_ao_patches.count && [texture.label hasPrefix:@"GBuffer_"])
        objc_setAssociatedObject(encoder,&metal_ao_dimensions_key,metal_ao_size_key(texture),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if(!metal_tracing || index>=32)return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&metal_fragment_bindings_key);
    if(!bindings){bindings=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&metal_fragment_bindings_key,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(texture)bindings[@(index)]=[NSString stringWithFormat:@"%p %lux%lu format=%lu storage=%lu hazard=%lu heap=%p %@",
        (__bridge void *)texture,(unsigned long)texture.width,(unsigned long)texture.height,
        (unsigned long)texture.pixelFormat,(unsigned long)texture.storageMode,
        (unsigned long)texture.hazardTrackingMode,(__bridge void *)texture.heap,texture.label ?: @"<unnamed>"];
    else [bindings removeObjectForKey:@(index)];
}

static void metal_fragment_draw(id encoder) {
    if(!metal_tracing && !metal_ao_patches.count && !character_skin_source && !atomic_load(&character_skin_native_compiled) && !atomic_load(&native_temporal_enabled))return;
    id pipeline=objc_getAssociatedObject(encoder,&metal_render_pipeline_key);
    if(official_gtao_active())official_gtao_camera(encoder,pipeline,objc_getAssociatedObject(pipeline,&metal_pipeline_description_key));
    native_temporal_draw(encoder,pipeline);
    unsigned characterKind=[objc_getAssociatedObject(pipeline,&metal_character_pipeline_key) unsignedIntValue];
    if(characterKind)character_skin_draw(encoder,(characterKind & 1)!=0);
    if(terrain_depth_enabled)terrain_depth_draw(encoder,objc_getAssociatedObject(pipeline,&metal_pipeline_description_key));
    managed_gbuffer_draw(encoder,pipeline);
    if(characterKind & 2)managed_character_depth_draw(encoder);
    if([objc_getAssociatedObject(pipeline,&metal_ao_pipeline_key) boolValue]) {
        metal_ssr_history_draw(encoder,objc_getAssociatedObject(pipeline,&metal_ssr_depth_index_key));
        uint32_t contactDebug=atomic_load(&metal_native_contact_debug) | (atomic_load(&metal_screen_ao_debug)<<8) |
            (atomic_load(&metal_screen_ao_legacy_pattern) ? 65536u : 0u);
        [encoder setFragmentBytes:&contactDebug length:sizeof contactDebug atIndex:28];
        float screenAOSettings[]={atomic_load(&metal_screen_ao_strength),atomic_load(&metal_screen_ao_radius),
            atomic_load(&metal_native_contact_strength),atomic_load(&metal_native_contact_length)};
        [encoder setFragmentBytes:screenAOSettings length:sizeof screenAOSettings atIndex:29];
        if(screenAOSettings[0]>0)atomic_fetch_add(&metal_screen_ao_draws,1);
        if(screenAOSettings[2]>0)atomic_fetch_add(&metal_native_contact_draws,1);
        id<MTLTexture> texture,contact;
        @synchronized(metal_ao_lock){
            texture=metal_ao_texture ?: metal_ao_white;
            NSNumber *size=objc_getAssociatedObject(encoder,&metal_ao_dimensions_key);
            if(texture!=metal_ao_white && ![size isEqual:metal_ao_size_key(texture)])texture=metal_ao_white;
            contact=metal_contact_texture ?: metal_ao_white;
            if(contact!=metal_ao_white && ![size isEqual:metal_ao_size_key(contact)])contact=metal_ao_white;
        }
        if(screenAOSettings[0]>0){
            id command=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
            texture=objc_getAssociatedObject(command,&official_gtao_result_key) ?: metal_ao_white;
        }
        if(screenAOSettings[2]>0){
            id command=[objc_getAssociatedObject(encoder,&managed_gbuffer_encoder_key) nonretainedObjectValue];
            contact=objc_getAssociatedObject(command,&official_contact_result_key) ?: metal_ao_white;
        }
        [encoder setFragmentTexture:contact atIndex:30];
        if(contact!=metal_ao_white)atomic_fetch_add(&metal_contact_draws,1);
        [encoder setFragmentTexture:texture atIndex:31];
        atomic_fetch_add(&metal_ao_draws,1);
        if(texture!=metal_ao_white)atomic_fetch_add(&metal_ao_bound_draws,1);
    }
    if(!metal_tracing)return;
    NSDictionary *bindings=objc_getAssociatedObject(encoder,&metal_fragment_bindings_key);
    NSString *description=objc_getAssociatedObject(pipeline,&metal_pipeline_description_key);
    metal_light_constants_draw(encoder,description);
    if([description containsString:@"d6af24c94a2dd3ab6c038fae09d80f379a09de5eecabfdfe25922484bbbe9c87"])
        record_metal([NSString stringWithFormat:@"native-taau:target %@",objc_getAssociatedObject(encoder,&metal_render_target_description_key) ?: @"unavailable"]);
    NSMutableArray *parts=[NSMutableArray array];
    for(NSNumber *key in [[bindings allKeys] sortedArrayUsingSelector:@selector(compare:)])
        [parts addObject:[NSString stringWithFormat:@"%@=%@",key,bindings[key]]];
    record_metal([NSString stringWithFormat:@"fragment-draw:pso=%p %@ encoder=%@ target=%@ {%@}",
        (__bridge void *)pipeline,description ?: @"<descriptor unavailable>",[encoder label] ?: @"<unnamed>",
        objc_getAssociatedObject(encoder,&metal_render_target_description_key) ?: @"unavailable",[parts componentsJoinedByString:@"; "]]);
}

static void metal_describe_pipeline(id pipeline,MTLRenderPipelineDescriptor *descriptor) {
    if(!pipeline || !descriptor)return;
    NSMutableArray *formats=[NSMutableArray array];
    for(NSUInteger i=0;i<8;i++)[formats addObject:@(descriptor.colorAttachments[i].pixelFormat)];
    objc_setAssociatedObject(pipeline,&native_gbuffer_formats_key,formats,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSString *description=[NSString stringWithFormat:@"vertex=%@ fragment=%@ colors=%@ label=%@ source=%@",
        descriptor.vertexFunction.name ?: @"<none>",descriptor.fragmentFunction.name ?: @"<none>",formats,descriptor.label ?: @"<unnamed>",
        objc_getAssociatedObject(descriptor.fragmentFunction,&metal_function_source_key) ?: @"<unattributed>"];
    objc_setAssociatedObject(pipeline,&metal_pipeline_description_key,description,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    native_temporal_classify(pipeline,description);
    managed_gbuffer_classify(pipeline,description);
    official_gtao_classify(pipeline,descriptor);
    // Pipeline identity is immutable; classify once instead of searching it on every draw.
    unsigned characterKind=([description containsString:@"SKIN_NATIVE"] ? 1 : 0) |
        ([description containsString:@"SKIN_PARITY"] ? 2 : 0);
    if(characterKind)objc_setAssociatedObject(pipeline,&metal_character_pipeline_key,@(characterKind),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if([description containsString:@"AO_PATCH"])
        objc_setAssociatedObject(pipeline,&metal_ao_pipeline_key,@YES,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSRange digestRange=[description rangeOfString:@"sha256="];
    if(digestRange.location!=NSNotFound && description.length>=NSMaxRange(digestRange)+64) {
        NSString *digest=[description substringWithRange:NSMakeRange(NSMaxRange(digestRange),64)];
        NSNumber *index=metal_ao_patches[digest][@"depthTextureIndex"];
        if(index)objc_setAssociatedObject(pipeline,&metal_ssr_depth_index_key,index,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        NSArray *indices=metal_lighting_gbuffer_bindings[digest] ?: metal_ao_patches[digest][@"gbufferTextureIndices"];
        if(indices.count==1)objc_setAssociatedObject(pipeline,&native_decal_depth_pipeline_key,indices[0],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        else if(indices.count==3)objc_setAssociatedObject(pipeline,&managed_gbuffer_indices_key,indices,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

static void install_metal_bindings(Class deviceClass,Class renderClass) {
    const char *gtaoPath=getenv("ENDFIELD_OFFICIAL_GTAO_PATH");
    if(gtaoPath){
        NSData *data=[NSData dataWithContentsOfFile:[@(gtaoPath) stringByAppendingPathComponent:@"camera-bindings.json"]];
        official_gtao_camera_bindings=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    }
    terrain_depth_enabled=getenv("ENDFIELD_TERRAIN_DEPTH_FIX")!=NULL;
    SEL selector=@selector(newRenderPipelineStateWithDescriptor:error:);
    Method method=class_getInstanceMethod(deviceClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,MTLRenderPipelineDescriptor *descriptor,NSError **error){
            descriptor=native_gbuffer_pipeline(terrain_depth_pipeline(object,descriptor));
            id pipeline=((id(*)(id,SEL,id,NSError **))original)(object,@selector(newRenderPipelineStateWithDescriptor:error:),descriptor,error);
            metal_describe_pipeline(pipeline,descriptor);return pipeline;
        });
        if(!class_addMethod(deviceClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:);method=class_getInstanceMethod(deviceClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,MTLRenderPipelineDescriptor *descriptor,MTLPipelineOption options,MTLAutoreleasedRenderPipelineReflection *reflection,NSError **error){
            descriptor=native_gbuffer_pipeline(terrain_depth_pipeline(object,descriptor));
            id pipeline=((id(*)(id,SEL,id,MTLPipelineOption,MTLAutoreleasedRenderPipelineReflection *,NSError **))original)(object,@selector(newRenderPipelineStateWithDescriptor:options:reflection:error:),descriptor,options,reflection,error);
            metal_describe_pipeline(pipeline,descriptor);return pipeline;
        });
        if(!class_addMethod(deviceClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(setFragmentTexture:atIndex:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,id<MTLTexture> texture,NSUInteger index){
            texture=native_temporal_texture(object,native_gbuffer_texture(texture));
            metal_fragment_binding(object,texture,index);
            ((void(*)(id,SEL,id,NSUInteger))original)(object,@selector(setFragmentTexture:atIndex:),texture,index);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(setFragmentTextures:withRange:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,const id<MTLTexture> *textures,NSRange range){
            __unsafe_unretained id<MTLTexture> aliases[32];
            if((native_gbuffer_enabled || atomic_load(&native_temporal_enabled)) && range.length<=32) {
                for(NSUInteger i=0;i<range.length;i++)aliases[i]=native_temporal_texture(object,native_gbuffer_texture(textures[i]));
                textures=aliases;
            }
            if(metal_tracing || metal_ao_patches.count || atomic_load(&native_temporal_enabled))for(NSUInteger i=0;i<range.length;i++)metal_fragment_binding(object,textures[i],range.location+i);
            ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))original)(object,@selector(setFragmentTextures:withRange:),textures,range);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:),type,start,count);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:),type,start,count,instances);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances,NSUInteger base){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:),type,start,count,instances,base);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:),type,count,indexType,buffer,offset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:),type,count,indexType,buffer,offset,instances);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances,NSInteger baseVertex,NSUInteger baseInstance){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger,NSInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:),type,count,indexType,buffer,offset,instances,baseVertex,baseInstance);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,id<MTLBuffer> buffer,NSUInteger offset){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,id,NSUInteger))original)(object,@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:),type,buffer,offset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,MTLIndexType indexType,id<MTLBuffer> indices,NSUInteger indexOffset,id<MTLBuffer> arguments,NSUInteger argumentOffset){
            metal_fragment_draw(object);
            ((void(*)(id,SEL,MTLPrimitiveType,MTLIndexType,id,NSUInteger,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:),type,indexType,indices,indexOffset,arguments,argumentOffset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
}
