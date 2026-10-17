static char metal_library_source_key,metal_function_source_key;
static NSMutableSet *metal_library_classes;
static NSString *metal_sha256(NSData *data) {
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,hash);
    NSMutableString *digest=[NSMutableString string];
    for(int i=0;i<CC_SHA256_DIGEST_LENGTH;i++)[digest appendFormat:@"%02x",hash[i]];
    return digest;
}

static void metal_watch_library(id library,NSString *identity) {
    if(!library || !identity)return;
    objc_setAssociatedObject(library,&metal_library_source_key,identity,OBJC_ASSOCIATION_COPY_NONATOMIC);
    Class cls=object_getClass(library);
    @synchronized(metal_library_classes) {
        NSString *name=NSStringFromClass(cls);
        if([metal_library_classes containsObject:name])return;
        [metal_library_classes addObject:name];
        SEL selector=@selector(newFunctionWithName:);
        Method method=class_getInstanceMethod(cls,selector);
        if(!method)return;
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,NSString *functionName){
            id function=((id(*)(id,SEL,id))original)(object,@selector(newFunctionWithName:),functionName);
            NSString *source=objc_getAssociatedObject(object,&metal_library_source_key);
            if(function && source)objc_setAssociatedObject(function,&metal_function_source_key,source,OBJC_ASSOCIATION_COPY_NONATOMIC);
            return function;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
        selector=@selector(newFunctionWithName:constantValues:error:);
        method=class_getInstanceMethod(cls,selector);
        if(method) {
            IMP specialized=method_getImplementation(method);
            IMP wrapper=imp_implementationWithBlock(^id(id object,NSString *functionName,MTLFunctionConstantValues *values,NSError **error){
                id function=((id(*)(id,SEL,id,id,NSError **))specialized)(object,@selector(newFunctionWithName:constantValues:error:),functionName,values,error);
                NSString *source=objc_getAssociatedObject(object,&metal_library_source_key);
                if(function && source)objc_setAssociatedObject(function,&metal_function_source_key,source,OBJC_ASSOCIATION_COPY_NONATOMIC);
                return function;
            });
            if(!class_addMethod(cls,selector,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);
        }
    }
}
