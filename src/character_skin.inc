/* Matched-source desktop skin equations for validated current-build sources. */
static atomic_uint character_skin_mode,character_skin_compiled,character_skin_draws;
static atomic_uint character_skin_native_compiled,character_skin_native_draws;
static atomic_bool character_skin_sample_requested;
static NSString *character_skin_sample_original;
static char character_skin_command_key,character_skin_sample_key;
static NSString *character_skin_source;
static NSDictionary<NSString *,NSString *> *character_skin_sources;
static NSString *character_skin_original=@"7205b70807e797c9bf4a04b02381a8df2940d8bea3e50b2f0a05a42cdd40437f";

static void character_skin_load(void) {
    const char *manifest=getenv("ENDFIELD_CHARACTER_SKIN_PATH");
    if(!manifest)return;
    NSData *data=[NSData dataWithContentsOfFile:@(manifest)];
    NSDictionary *item=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    NSData *source=item ? [NSData dataWithContentsOfFile:item[@"source"]] : nil;
    NSString *original=item[@"original"];
    bool known=[original isEqual:character_skin_original] ||
        [original isEqual:@"ab7ad018484d94554fd335dc1f2a3938e5f8f8be444a6fa81460a3caebee815e"];
    if(source && known && [metal_sha256(source) isEqual:item[@"sha256"]]) {
        character_skin_original=original;
        character_skin_source=[[NSString alloc] initWithData:source encoding:NSUTF8StringEncoding];
        NSMutableDictionary *sources=[NSMutableDictionary dictionaryWithObject:character_skin_source forKey:original];
        for(NSDictionary *variant in item[@"variants"]) {
            NSString *digest=variant[@"original"];
            NSData *bytes=[NSData dataWithContentsOfFile:variant[@"source"]];
            bool supported=[@[@"b8f0ad0ab71a5ad7b3bdeb36c917b0bbe8e7fa2c44ad653091e0acf8a43a5531",
                @"0ab94eb8366d12fef2805cfdedb479efe4d37e02cad35f89d0ab1af69948ca0d",
                @"622e9cf263bcd70a25dca31f950aff08821b7b21647978f476cdb9b43230b587",
                @"de1c8596f6dd3970809b4676f8fd0dd16c6d8f6145cc4d313a8e3702a3f6a5cb",
                @"477eaeacd45e756d4f71a243d0f7ad069360c290e48a49939df6f0a81f2d6453",
                @"3aba00c6034b183a0a59c08c0c38b66141a1a5287da880a5cbb68fcbbe5383c2"] containsObject:digest];
            if(supported &&
                bytes && [metal_sha256(bytes) isEqual:variant[@"sha256"]])
                sources[digest]=[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
        }
        character_skin_sources=[sources copy];
        // Current 1.6.3 sources have been checked in character menus and the
        // world: PC character-shadow filtering and authored face lighting.
        // Dry specular remains a separate experimental option (bit 4).
        if([original isEqual:@"ab7ad018484d94554fd335dc1f2a3938e5f8f8be444a6fa81460a3caebee815e"])
            atomic_store(&character_skin_mode,3u);
    }
    fprintf(output,"character_skin_source=ready:%d manifest:%d bytes:%lu\n",character_skin_source!=nil,item!=nil,(unsigned long)source.length);
}

static void character_skin_draw(id encoder,bool native) {
    if(native){atomic_fetch_add(&character_skin_native_draws,1);return;}
    uint32_t mode=atomic_load(&character_skin_mode);
    [encoder setFragmentBytes:&mode length:sizeof mode atIndex:24];
    atomic_fetch_add(&character_skin_draws,1);
}

static void character_skin_encoder(id encoder,id commandBuffer) {
    if(character_skin_source || atomic_load(&character_skin_native_compiled))objc_setAssociatedObject(encoder,&character_skin_command_key,
        [NSValue valueWithNonretainedObject:commandBuffer],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void character_skin_sample_private(id encoder,id entry,NSString *description) {
    if(!atomic_load(&character_skin_sample_requested) || ![entry isKindOfClass:NSArray.class])return;
    if(character_skin_sample_original && ![description containsString:character_skin_sample_original])return;
    id<MTLBuffer> source=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
    id commandBuffer=[objc_getAssociatedObject(encoder,&character_skin_command_key) nonretainedObjectValue];
    NSUInteger size=12;
    if(!commandBuffer || source.storageMode!=MTLStorageModePrivate || offset>source.length || source.length-offset<size)return;
    if(!atomic_exchange(&character_skin_sample_requested,false))return;
    objc_setAssociatedObject(commandBuffer,&character_skin_sample_key,@[source,@(offset),@(size),description ?: @"unknown"],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void character_skin_commit(id<MTLCommandBuffer> buffer) {
    NSArray *entry=objc_getAssociatedObject(buffer,&character_skin_sample_key);
    if(!entry)return;
    objc_setAssociatedObject(buffer,&character_skin_sample_key,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    id<MTLBuffer> source=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
    NSUInteger size=[entry[2] unsignedIntegerValue];
    id<MTLBuffer> staging=[buffer.device newBufferWithLength:size options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit=staging ? [buffer blitCommandEncoder] : nil;
    if(!blit){fprintf(output,"character_skin_readback=allocation_failed\n");return;}
    [blit copyFromBuffer:source sourceOffset:offset toBuffer:staging destinationOffset:0 size:size];
    [blit endEncoding];
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){
        NSMutableDictionary *result=[@{@"status":@(completed.status),@"completedAt":@(NSDate.date.timeIntervalSince1970),@"offset":@(offset),@"source":entry[3]} mutableCopy];
        if(completed.status==MTLCommandBufferStatusCompleted){
            float v[3];memcpy(v,staging.contents,sizeof v);
            result[@"smoothness"]=@(v[0]);result[@"specular"]=@(v[1]);result[@"metallic"]=@(v[2]);
            if(size==384){float mode;memcpy(&mode,(const char *)staging.contents+368,4);result[@"nativeMode"]=@(mode);}
        }
        [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:runtime_path(@"endfield-ios-character-skin-material.json") atomically:YES];
        fprintf(output,"character_skin_readback=complete status=%lu\n",(unsigned long)completed.status);
    }];
}

static void character_skin_control(NSDictionary *command) {
    if([command[@"sampleMaterial"] boolValue]){
        [NSFileManager.defaultManager removeItemAtPath:runtime_path(@"endfield-ios-character-skin-material.json") error:nil];
        character_skin_sample_original=command[@"sampleOriginal"];
        atomic_store(&character_skin_sample_requested,true);
    }
    if(command[@"mode"])atomic_store(&character_skin_mode,[command[@"mode"] unsignedIntValue]&15u);
    fprintf(output,"character_skin=ready source=%d compiled=%u draws=%u mode=%u\n",character_skin_source!=nil,
        atomic_load(&character_skin_compiled),atomic_load(&character_skin_draws),atomic_load(&character_skin_mode));
    fprintf(output,"character_skin_native=compiled:%u draws:%u\n",atomic_load(&character_skin_native_compiled),atomic_load(&character_skin_native_draws));
}
