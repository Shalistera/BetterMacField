    selector=@selector(setFragmentTexture:atIndex:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,id<MTLTexture> texture,NSUInteger index){
            texture=native_temporal_texture(object,texture);
            native_temporal_input(object,texture,index);
            ((void(*)(id,SEL,id,NSUInteger))original)(object,@selector(setFragmentTexture:atIndex:),texture,index);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(setFragmentTextures:withRange:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,const id<MTLTexture> *textures,NSRange range){
            __unsafe_unretained id<MTLTexture> aliases[32];
            if(atomic_load(&native_temporal_enabled) && range.length<=32) {
                for(NSUInteger i=0;i<range.length;i++)aliases[i]=native_temporal_texture(object,textures[i]);
                textures=aliases;
            }
            if(atomic_load(&native_temporal_enabled))for(NSUInteger i=0;i<range.length;i++)native_temporal_input(object,textures[i],range.location+i);
            ((void(*)(id,SEL,const id<MTLTexture> *,NSRange))original)(object,@selector(setFragmentTextures:withRange:),textures,range);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:),type,start,count);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:),type,start,count,instances);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances,NSUInteger base){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:),type,start,count,instances,base);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:),type,count,indexType,buffer,offset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:),type,count,indexType,buffer,offset,instances);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances,NSInteger baseVertex,NSUInteger baseInstance){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger,NSInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:),type,count,indexType,buffer,offset,instances,baseVertex,baseInstance);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,id<MTLBuffer> buffer,NSUInteger offset){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,id,NSUInteger))original)(object,@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:),type,buffer,offset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(renderClass,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,MTLIndexType indexType,id<MTLBuffer> indices,NSUInteger indexOffset,id<MTLBuffer> arguments,NSUInteger argumentOffset){
            native_temporal_draw(object,objc_getAssociatedObject(object,&bmf_pipeline_key));
            ((void(*)(id,SEL,MTLPrimitiveType,MTLIndexType,id,NSUInteger,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:),type,indexType,indices,indexOffset,arguments,argumentOffset);
        });
        if(!class_addMethod(renderClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
