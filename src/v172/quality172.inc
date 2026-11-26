// The game sizes its scene targets as the final size times RenderingScale.
static NSDictionary *quality172(NSDictionary *fx, NSDictionary *effective, BOOL compare) {
    NSString *mode = compare ? @"taau" : selectedUpscaler172(selected);
    NSString *quality = compare ? @"性能" : selected[@"quality"];
    NSString *key = @"RenderTargetSettings/RenderingScale";
    if ([mode isEqual:@"default"]) {
        if (qualityScaleOverride) { qualityScaleOverride = nil; needsApply = YES; }
        return @{@"status":@"gameDefault"};
    }
    NSNumber *target = [quality isEqual:@"质量"] ? @0.6667f : ([quality isEqual:@"平衡"] ? @0.588f : @0.5f);
    NSMutableDictionary *state = [@{@"targetRatio":target} mutableCopy];
    NSNumber *scale = effective[@"RenderTargetSettings"][@"RenderingScale"];
    if (scale) state[@"parameterReadback"] = scale;
    BOOL failed = [@[@"unsupported", @"requestFailed", @"restoreFailed", @"changedInGame", @"unavailable"] containsObject:fx[@"status"]];
    if (failed || [changedInGame containsObject:key]) {
        if (qualityScaleOverride) { qualityScaleOverride = nil; needsApply = YES; }
        state[@"status"] = ownedValues[key] ? @"restoring" : @"gameValue";
        state[@"reason"] = failed ? @"当前相机未能使用所选超分模式" : @"游戏中已调整渲染比例";
        return state;
    }
    if (![qualityScaleOverride isEqualToNumber:target]) { qualityScaleOverride = target; needsApply = YES; }
    NSArray *input = fx[@"sceneSize"], *output = fx[@"finalSize"];
    if (input.count != 2 || output.count != 2 || [output[0] intValue] <= 0 || [output[1] intValue] <= 0) {
        state[@"status"] = @"waitingForCamera"; return state;
    }
    double x = [input[0] doubleValue] / [output[0] doubleValue];
    double y = [input[1] doubleValue] / [output[1] doubleValue];
    state[@"measuredRatio"] = @[@(x), @(y)];
    BOOL matched = fabs(x - target.doubleValue) <= 0.012 && fabs(y - target.doubleValue) <= 0.012;
    state[@"status"] = matched ? @"verified" : @"waitingForSize";
    return state;
}
