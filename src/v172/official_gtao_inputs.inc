/* One-frame inputs for running the installed client's unmodified GTAO kernels.
   The camera buffers and depth are copied on the same command buffer. */
static char official_gtao_bindings_key,official_gtao_camera_key,official_gtao_slots_key;
static NSDictionary *official_gtao_camera_bindings;

static bool official_gtao_active(void) {
    return atomic_load(&metal_screen_ao_strength)>0 || atomic_load(&metal_native_contact_strength)>0;
}

static bool official_gtao_wants_bindings(id encoder) {
    return official_gtao_active() && [objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue] &&
        !objc_getAssociatedObject(encoder,&official_gtao_camera_key);
}

static void official_gtao_classify(id pipeline,MTLRenderPipelineDescriptor *descriptor) {
    NSString *identity=objc_getAssociatedObject(descriptor.vertexFunction,&metal_function_source_key);
    NSRange range=[identity rangeOfString:@"sha256="];
    if(!identity || range.location==NSNotFound || identity.length<NSMaxRange(range)+64)return;
    NSString *digest=[identity substringWithRange:NSMakeRange(NSMaxRange(range),64)];
    NSArray *slots=official_gtao_camera_bindings[digest];
    if(slots)objc_setAssociatedObject(pipeline,&official_gtao_slots_key,slots,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void official_gtao_buffer(id encoder,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger index) {
    if(!official_gtao_wants_bindings(encoder) || index>31)return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&official_gtao_bindings_key);
    if(!bindings){bindings=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&official_gtao_bindings_key,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(buffer)bindings[@(index)]=@[buffer,@(offset)];else [bindings removeObjectForKey:@(index)];
}

static void official_gtao_bytes(id encoder,const void *bytes,NSUInteger length,NSUInteger index) {
    if(!official_gtao_wants_bindings(encoder) || index>31)return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&official_gtao_bindings_key);
    if(!bindings){bindings=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&official_gtao_bindings_key,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(bytes)bindings[@(index)]=[NSData dataWithBytes:bytes length:MIN(length,3328)];
    else [bindings removeObjectForKey:@(index)];
}

static void official_gtao_offset(id encoder,NSUInteger offset,NSUInteger index) {
    if(!official_gtao_wants_bindings(encoder) || index>31)return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&official_gtao_bindings_key);
    id entry=bindings[@(index)];
    if([entry isKindOfClass:NSArray.class])bindings[@(index)]=@[entry[0],@(offset)];
}

static void official_gtao_camera(id encoder,id pipeline,NSString *description) {
    if(!official_gtao_active() ||
       ![objc_getAssociatedObject(encoder,&managed_gbuffer_store_key) boolValue] ||
       objc_getAssociatedObject(encoder,&official_gtao_camera_key))return;
    NSDictionary *bindings=objc_getAssociatedObject(encoder,&official_gtao_bindings_key);
    NSArray *slots=objc_getAssociatedObject(pipeline,&official_gtao_slots_key);
    if(slots.count==2){
        NSMutableArray *camera=[NSMutableArray array];
        for(NSDictionary *slot in slots){
            NSUInteger index=[slot[@"index"] unsignedIntegerValue]+([slot[@"stage"] isEqual:@"vertex"] ? 16 : 0);
            id entry=bindings[@(index)];if(!entry)return;[camera addObject:entry];
        }
        objc_setAssociatedObject(encoder,&official_gtao_camera_key,camera,OBJC_ASSOCIATION_RETAIN_NONATOMIC);return;
    }
    // One-pass fallback: verified CharacterNPR_Skin and cloth camera bindings.
    if(![description containsString:@"sha256=de1c8596f6dd3970809b4676f8fd0dd16c6d8f6145cc4d313a8e3702a3f6a5cb"] &&
       ![description containsString:@"sha256=477eaeacd45e756d4f71a243d0f7ad069360c290e48a49939df6f0a81f2d6453"])return;
    if(bindings[@0] && bindings[@1])objc_setAssociatedObject(encoder,&official_gtao_camera_key,
        @[bindings[@0],bindings[@1]],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
