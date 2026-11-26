// Values from the validated desktop streaming profiles.
static NSDictionary *radiiForLOD172(NSString *profile) {
    if ([profile isEqual:@"pc-medium"])
        return @{@"chunkLoadRadius":@128.f, @"defaultLayerLoadRadius":@128.f,
                 @"hlod0LayerLoadRadius":@384.f, @"tinyLayerLoadRadius":@128.f};
    if ([profile isEqual:@"pc-high"])
        return @{@"chunkLoadRadius":@160.f, @"defaultLayerLoadRadius":@160.f,
                 @"hlod0LayerLoadRadius":@512.f, @"tinyLayerLoadRadius":@128.f};
    return nil;
}
