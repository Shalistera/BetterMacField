/* One-frame inputs for running the installed client's unmodified GTAO kernels.
   The camera buffers and depth are copied on the same command buffer. */
static atomic_bool official_gtao_capture_pending;
static char official_gtao_bindings_key,official_gtao_camera_key,official_gtao_slots_key;
static NSDictionary *official_gtao_camera_bindings;

static bool official_gtao_active(void) {
    return atomic_load(&metal_screen_ao_strength)>0 || atomic_load(&metal_native_contact_strength)>0 || atomic_load(&official_gtao_capture_pending);
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
    // One-pass diagnostic: current CharacterNPR_Skin and cloth reflection.
    if(![description containsString:@"sha256=de1c8596f6dd3970809b4676f8fd0dd16c6d8f6145cc4d313a8e3702a3f6a5cb"] &&
       ![description containsString:@"sha256=477eaeacd45e756d4f71a243d0f7ad069360c290e48a49939df6f0a81f2d6453"])return;
    if(bindings[@0] && bindings[@1])objc_setAssociatedObject(encoder,&official_gtao_camera_key,
        @[bindings[@0],bindings[@1]],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void official_gtao_capture(id encoder,id<MTLCommandBuffer> command,id<MTLTexture> depth) {
    NSArray *camera=objc_getAssociatedObject(encoder,&official_gtao_camera_key);
    if(!atomic_load(&official_gtao_capture_pending) || camera.count!=2 || !depth)return;
    const NSUInteger lengths[]={1312,3328};
    for(NSUInteger i=0;i<2;i++) {
        id entry=camera[i];
        if([entry isKindOfClass:NSData.class]){if([entry length]<lengths[i])return;}
        else {id<MTLBuffer> source=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(offset>source.length || source.length-offset<lengths[i])return;}
    }
    if(!atomic_exchange(&official_gtao_capture_pending,false))return;
    NSUInteger rowBytes=(depth.width*4+255)&~255UL;
    id<MTLBuffer> pixels=[command.device newBufferWithLength:rowBytes*depth.height options:MTLResourceStorageModeShared];
    id<MTLBuffer> constants=[command.device newBufferWithLength:1312+3328 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    [blit copyFromTexture:depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(depth.width,depth.height,1) toBuffer:pixels destinationOffset:0
        destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes*depth.height];
    for(NSUInteger i=0;i<2;i++) {
        id entry=camera[i];NSUInteger destination=i ? 1312 : 0;
        if([entry isKindOfClass:NSData.class])memcpy((char *)constants.contents+destination,[entry bytes],lengths[i]);
        else [blit copyFromBuffer:entry[0] sourceOffset:[entry[1] unsignedIntegerValue]
            toBuffer:constants destinationOffset:destination size:lengths[i]];
    }
    [blit endEncoding];
    NSDictionary *metadata=@{@"width":@(depth.width),@"height":@(depth.height),@"rowBytes":@(rowBytes),
        @"pixelFormat":@(depth.pixelFormat),@"transformBytes":@1312,@"globalBytes":@3328};
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
        if(completed.status==MTLCommandBufferStatusCompleted){
            [[NSData dataWithBytes:pixels.contents length:pixels.length] writeToFile:runtime_path(@"endfield-official-gtao-depth.raw") atomically:YES];
            [[NSData dataWithBytes:constants.contents length:constants.length] writeToFile:runtime_path(@"endfield-official-gtao-constants.raw") atomically:YES];
            [[NSJSONSerialization dataWithJSONObject:metadata options:NSJSONWritingPrettyPrinted error:nil]
                writeToFile:runtime_path(@"endfield-official-gtao-inputs.json") atomically:YES];
        }
        fprintf(output,"official_gtao_capture=complete status=%lu\n",(unsigned long)completed.status);fflush(output);
    }];
}
