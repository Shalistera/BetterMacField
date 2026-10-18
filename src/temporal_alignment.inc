/* Bounded, explicit GPU readback for matching raster motion to temporal jitter.
   Captures sixteen frames only when requested; inactive during ordinary play. */
static atomic_uint temporal_alignment_remaining;
static NSMutableArray *temporal_alignment_rows;

static void temporal_alignment_capture(EFNativeTemporalFrame *frame,id<MTLTexture> resolved) {
    unsigned remaining=atomic_load(&temporal_alignment_remaining);
    if(!remaining || !frame.color || !resolved)return;
    if(frame.color.pixelFormat!=MTLPixelFormatRG11B10Float || resolved.pixelFormat!=MTLPixelFormatRG11B10Float)return;
    if(!atomic_compare_exchange_strong(&temporal_alignment_remaining,&remaining,remaining-1))return;
    unsigned index=16-remaining;
    NSMutableArray *copies=[NSMutableArray array];
    id<MTLBlitCommandEncoder> blit=[frame.commandBuffer blitCommandEncoder];
    if(!blit){atomic_store(&temporal_alignment_remaining,0);return;}
    // Capture the original TAAU result in the same frame as MetalFX. This avoids
    // attributing changes in pose, camera or lighting between separate bursts.
    NSArray *textures=frame.motion && frame.target ? @[frame.color,resolved,frame.motion,frame.target] : @[frame.color,resolved];
    const float centers[][2]={{.62f,.28f},{.50f,.78f},{.30f,.55f}};
    for(unsigned stage=0;stage<textures.count;stage++)for(unsigned patch=0;patch<3;patch++){
        id<MTLTexture> texture=textures[stage];
        NSUInteger width=MIN(192,texture.width),height=MIN(192,texture.height),row=(width*4+255)&~255UL;
        NSUInteger x=MIN(texture.width-width,(NSUInteger)MAX(0,texture.width*centers[patch][0]-width/2));
        NSUInteger y=MIN(texture.height-height,(NSUInteger)MAX(0,texture.height*centers[patch][1]-height/2));
        id<MTLBuffer> buffer=[frame.commandBuffer.device newBufferWithLength:row*height options:MTLResourceStorageModeShared];
        if(!buffer)continue;
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x,y,0)
            sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
            destinationBytesPerRow:row destinationBytesPerImage:row*height];
        [copies addObject:@{@"buffer":buffer,@"stage":@(stage),@"patch":@(patch),@"origin":@[@(x),@(y)],
            @"size":@[@(width),@(height)],@"textureSize":@[@(texture.width),@(texture.height)],@"rowBytes":@(row),@"format":@(texture.pixelFormat)}];
    }
    [blit endEncoding];
    NSArray *jitter=@[@(frame.jitterX),@(frame.jitterY)];
    NSString *mode=atomic_load(&native_temporal_bypass) ? @"taau" : (atomic_load(&native_spatial_enabled) ? @"spatial" : @"metalfx");
    unsigned motionMode=atomic_load(&native_temporal_motion_mode);
    [frame.commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> command){
        @autoreleasepool {
            NSMutableArray *patches=[NSMutableArray array];
            if(command.status==MTLCommandBufferStatusCompleted)for(NSDictionary *copy in copies){
                id<MTLBuffer> buffer=copy[@"buffer"];
                NSString *path=[NSString stringWithFormat:runtime_path(@"endfield-temporal-%02u-%@-%@.bin"),index,copy[@"stage"],copy[@"patch"]];
                [[NSData dataWithBytes:buffer.contents length:buffer.length] writeToFile:path atomically:YES];
                NSMutableDictionary *entry=[copy mutableCopy];[entry removeObjectForKey:@"buffer"];entry[@"path"]=path;[patches addObject:entry];
            }
            @synchronized(native_temporal_lock()){
                [temporal_alignment_rows addObject:@{@"index":@(index),@"jitter":jitter,@"mode":mode,@"motionMode":@(motionMode),
                    @"status":@(command.status),@"patches":patches}];
                if(temporal_alignment_rows.count==16){
                    [[NSJSONSerialization dataWithJSONObject:temporal_alignment_rows options:NSJSONWritingPrettyPrinted error:nil]
                        writeToFile:runtime_path(@"endfield-temporal-alignment.json") atomically:YES];
                    fprintf(output,"temporal_alignment=complete frames=16\n");
                }
            }
        }
    }];
}
