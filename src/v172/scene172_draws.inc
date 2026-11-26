// Draw entry points used by Unity; preserve each original Metal signature.
    SEL selector; Method method;
    selector=@selector(drawPrimitives:vertexStart:vertexCount:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:),type,start,count);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:),type,start,count,instances);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger start,NSUInteger count,NSUInteger instances,NSUInteger base){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,NSUInteger,NSUInteger,NSUInteger))original)(object,@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:),type,start,count,instances,base);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:),type,count,indexType,buffer,offset);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:),type,count,indexType,buffer,offset,instances);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,NSUInteger count,MTLIndexType indexType,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger instances,NSInteger baseVertex,NSUInteger baseInstance){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,NSUInteger,MTLIndexType,id,NSUInteger,NSUInteger,NSInteger,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:),type,count,indexType,buffer,offset,instances,baseVertex,baseInstance);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,id<MTLBuffer> buffer,NSUInteger offset){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,id,NSUInteger))original)(object,@selector(drawPrimitives:indirectBuffer:indirectBufferOffset:),type,buffer,offset);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:);method=class_getInstanceMethod(cls,selector);
    if(method){
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^(id object,MTLPrimitiveType type,MTLIndexType indexType,id<MTLBuffer> indices,NSUInteger indexOffset,id<MTLBuffer> arguments,NSUInteger argumentOffset){
            sceneDraw172(object);
            ((void(*)(id,SEL,MTLPrimitiveType,MTLIndexType,id,NSUInteger,id,NSUInteger))original)(object,@selector(drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:),type,indexType,indices,indexOffset,arguments,argumentOffset);
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
