/* Bounded same-command-buffer snapshots of the real GTAO stage inputs/outputs. */
static atomic_uint official_gtao_capture_frames;
static unsigned official_gtao_capture_index;

static void official_gtao_diagnostic_texture(NSMutableDictionary *capture,id<MTLCommandBuffer> command,
                                            NSString *name,id<MTLTexture> texture) {
    if(!capture || !texture)return;
    NSUInteger bytes=0;
    switch(texture.pixelFormat){
        case MTLPixelFormatR8Unorm:bytes=1;break;
        case MTLPixelFormatR32Float:case MTLPixelFormatRGBA8Unorm:case MTLPixelFormatRGB10A2Unorm:bytes=4;break;
        case MTLPixelFormatRGBA16Float:bytes=8;break;
        default:return;
    }
    NSUInteger row=(texture.width*bytes+255)&~255UL;
    id<MTLBuffer> readback=[command.device newBufferWithLength:row*texture.height options:MTLResourceStorageModeShared];
    if(!readback)return;
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(texture.width,texture.height,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:row destinationBytesPerImage:row*texture.height];
    [blit endEncoding];
    capture[name]=@{@"buffer":readback,@"width":@(texture.width),@"height":@(texture.height),
                    @"rowBytes":@(row),@"pixelBytes":@(bytes),@"format":@(texture.pixelFormat)};
}

static NSMutableDictionary *official_gtao_diagnostic_begin(id<MTLCommandBuffer> command,NSArray *camera,
                                                           const float *parameters,int frame) {
    unsigned left=atomic_load(&official_gtao_capture_frames);if(!left)return nil;
    atomic_fetch_sub(&official_gtao_capture_frames,1);
    id<MTLBuffer> constants=[command.device newBufferWithLength:4640 options:MTLResourceStorageModeShared];
    if(!constants)return nil;
    id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    for(NSUInteger i=0;i<2;i++){
        NSUInteger offset=i ? 1312 : 0,length=i ? 3328 : 1312;id entry=camera[i];
        if([entry isKindOfClass:NSData.class])memcpy((char *)constants.contents+offset,[entry bytes],length);
        else [blit copyFromBuffer:entry[0] sourceOffset:[entry[1] unsignedIntegerValue]
                         toBuffer:constants destinationOffset:offset size:length];
    }
    [blit endEncoding];
    return [@{@"index":@(official_gtao_capture_index++),@"frame":@(frame),@"constants":constants,
               @"parameters":[NSData dataWithBytes:parameters length:80]} mutableCopy];
}

static void official_gtao_diagnostic_finish(NSMutableDictionary *capture,id<MTLCommandBuffer> command) {
    if(!capture)return;
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed){
        if(completed.status!=MTLCommandBufferStatusCompleted)return;
        NSString *prefix=[NSString stringWithFormat:@"endfield-official-gtao-frame-%@",capture[@"index"]];
        NSMutableDictionary *info=[@{@"frame":capture[@"frame"],@"textures":[NSMutableDictionary dictionary]} mutableCopy];
        for(NSString *name in capture){
            id entry=capture[name];
            if(![entry isKindOfClass:NSDictionary.class])continue;
            id<MTLBuffer> buffer=entry[@"buffer"];
            NSString *file=runtime_path([NSString stringWithFormat:@"%@-%@.raw",prefix,name]);
            [[NSData dataWithBytes:buffer.contents length:buffer.length] writeToFile:file atomically:YES];
            NSMutableDictionary *metadata=[entry mutableCopy];[metadata removeObjectForKey:@"buffer"];
            metadata[@"file"]=file;info[@"textures"][name]=metadata;
        }
        id<MTLBuffer> constants=capture[@"constants"];
        [[NSData dataWithBytes:constants.contents length:constants.length]
            writeToFile:runtime_path([prefix stringByAppendingString:@"-constants.raw"]) atomically:YES];
        [capture[@"parameters"] writeToFile:runtime_path([prefix stringByAppendingString:@"-parameters.raw"]) atomically:YES];
        [[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted error:nil]
            writeToFile:runtime_path([prefix stringByAppendingString:@".json"]) atomically:YES];
        fprintf(output,"official_gtao_frame_capture=complete index=%s frame=%s\n",
                [capture[@"index"] description].UTF8String,[capture[@"frame"] description].UTF8String);fflush(output);
    }];
}
