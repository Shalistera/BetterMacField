// The tracked values cover only per-process graphics overrides made by this launcher.
static NSMutableDictionary<NSString *, NSDictionary *> *ownedValues;
static NSMutableSet<NSString *> *changedInGame;
static NSMutableDictionary *baselineChanges;
static NSMutableDictionary *selected;
static NSString *session, *lastCommandID, *lastFileID, *commandProblem;
static BOOL wantsBaseline, confirmedBaseline, needsApply, newPlayableScene;

static NSDictionary *liveChoices(void) {
    return @{@"lod":@[@"restore", @"pc-medium", @"pc-high"],
             @"sharpen":@[@"original", @"no-taau", @"off"],
             @"vegetationDensity":@[@-1, @0.5, @0.7, @1],
             @"shadowResolution":@[@0, @1024, @2048],
             @"backgroundLimit":@[@NO, @YES], @"keyboard":@[@NO,@YES],
             @"upscaler":@[@"taau", @"metalfx-spatial", @"metalfx-native"],
             @"quality":@[@"质量", @"平衡", @"性能"],
             @"compareRendering":@[@NO,@YES], @"compareLOD":@[@NO,@YES],
             @"height":@[@0,@720,@1080,@1440,@2160],
             @"selfShadow":@[@NO, @YES], @"characterLighting":@[@NO, @YES], @"characterSpecular":@[@NO, @YES],
             @"gtao":@[@NO,@YES], @"contact":@[@NO,@YES], @"ssr":@[@NO,@YES]};
}

static NSString *selectedUpscaler172(NSDictionary *settings) {
    NSString *mode = settings[@"upscaler"];
    return [mode isEqual:@"metalfx-native"] ? @"metalfx-temporal" : mode;
}

static NSDictionary *featureTypes(void) {
    return @{@"RenderTargetSettings":@{@"RenderingScale":@1.f, @"BackBufferResolutionHeight":@0,
                                       @"TAAUResolveResolutionHeight":@0, @"TransientGBuffer":@NO},
             @"OnePassDeferred":@{@"shouldSplitOnePass":@NO},
             @"ShadowMap":@{@"CSMTileResolution":@0, @"CharacterShadowMapResolution":@0, @"ASMShadowMapTileResolution":@0},
             @"Streaming":radiiForLOD172(@"pc-high"),
             @"Grass":@{@"GrassGlobalSparsity":@0.f},
             @"TAAU":@{@"1KSharpenStrength":@0.f, @"2KSharpenStrength":@0.f, @"4KSharpenStrength":@0.f},
             @"PostProcess":@{@"SharpenEnabled":@NO}};
}

static NSDictionary *registeredParameters(void *hub, NSString *feature, NSDictionary *types) {
    bool ok;
    void *impl = call(hub, "get_settingImpl", 0, NULL, &ok);
    void *table = call(impl, "get_registeredParameters", 0, NULL, &ok);
    void *key[] = {newString(feature.UTF8String)};
    void *list = call(table, "get_Item", 1, key, &ok);
    NSNumber *count = integerValue(call(list, "get_Count", 0, NULL, &ok));
    NSMutableDictionary *params = [NSMutableDictionary dictionary];
    for (int i = 0; i < count.intValue; i++) {
        void *index[] = {&i};
        void *param = call(list, "get_Item", 1, index, &ok);
        NSString *name = textValue(call(param, "get_paramName", 0, NULL, &ok));
        if (name && types[name]) params[name] = [NSValue valueWithPointer:param];
    }
    return params;
}

static NSNumber *parameterValue(void *param, NSNumber *type) {
    bool ok;
    return renderNumber(call(param, "get_paramValue", 0, NULL, &ok), type);
}

static NSNumber *overrideFlag(void *param) {
    bool ok;
    return renderNumber(call(param, "get_overrided", 0, NULL, &ok), @YES);
}

static BOOL writeOverride(void *param, NSNumber *value) {
    bool ok;
    if (value) {
        void *args[] = {newString(value.stringValue.UTF8String)};
        call(param, "OverrideWithString", 1, args, &ok);
    } else {
        const char *reset = lookupMethod(objectClass(param), "CancelOverride", 0) ? "CancelOverride" : "Reset";
        call(param, reset, 0, NULL, &ok);
    }
    if (ok) call(param, "MarkFeatureDirty", 0, NULL, &ok);
    return ok;
}

// Read and change each feature as one unit. Never take back a value the game
// or user has changed since our previous successful write.
static NSDictionary *applyFeature(void *hub, NSString *feature, NSDictionary *types, NSDictionary *desired,
                                  NSString **error) {
    NSDictionary *params = registeredParameters(hub, feature, types);
    NSMutableDictionary *before = [NSMutableDictionary dictionary], *flags = [NSMutableDictionary dictionary];
    NSMutableDictionary *writes = [NSMutableDictionary dictionary], *acquired = [NSMutableDictionary dictionary];
    for (NSString *name in types) {
        NSString *key = [feature stringByAppendingFormat:@"/%@", name];
        void *param = [params[name] pointerValue];
        NSNumber *current = param ? parameterValue(param, types[name]) : nil;
        NSNumber *flag = param ? overrideFlag(param) : nil;
        NSDictionary *record = ownedValues[key];
        if (!current || (!flag && (record || desired[name]))) {
            if (record || desired[name]) {
                *error = [NSString stringWithFormat:@"%@.%@ 正在等待游戏注册参数", feature, name];
                return nil;
            }
            continue;
        }
        before[name] = current;
        flags[name] = flag;
        if (record && (![current isEqualToNumber:record[@"applied"]] || !flag.boolValue)) {
            [ownedValues removeObjectForKey:key];
            if (!newPlayableScene) [changedInGame addObject:key];
            record = nil;
        }
        if ([changedInGame containsObject:key]) continue;
        if (desired[name]) {
            if ([current isEqualToNumber:desired[name]]) continue;
            writes[name] = desired[name];
            acquired[name] = record ?: @{@"original":current, @"overridden":flag};
        } else if (record) {
            writes[name] = [record[@"overridden"] boolValue] ? record[@"original"] : NSNull.null;
        }
    }
    if (!writes.count) return before;

    bool ok = true;
    NSMutableArray *changed = [NSMutableArray array];
    for (NSString *name in writes) {
        [changed addObject:name];
        id value = writes[name];
        if (!writeOverride([params[name] pointerValue], value == NSNull.null ? nil : value)) { ok = false; break; }
    }
    if (ok) call(hub, "RefreshDirtySettings", 0, NULL, &ok);
    NSMutableDictionary *after = [before mutableCopy];
    for (NSString *name in changed) {
        void *param = [params[name] pointerValue];
        NSNumber *value = parameterValue(param, types[name]), *flag = overrideFlag(param);
        if (value) after[name] = value;
        id target = writes[name];
        if (!value || !flag || (target == NSNull.null ? flag.boolValue :
                                (!flag.boolValue || ![value isEqualToNumber:target]))) ok = false;
    }
    if (!ok) {
        for (NSString *name in changed)
            writeOverride([params[name] pointerValue], [flags[name] boolValue] ? before[name] : nil);
        call(hub, "RefreshDirtySettings", 0, NULL, &ok);
        *error = [NSString stringWithFormat:@"%@：设置或读回失败", feature];
        return nil;
    }
    for (NSString *name in changed) {
        NSString *key = [feature stringByAppendingFormat:@"/%@", name];
        if (desired[name]) {
            NSMutableDictionary *record = [acquired[name] mutableCopy];
            record[@"applied"] = after[name];
            ownedValues[key] = record;
            [baselineChanges removeObjectForKey:key];
        } else {
            NSNumber *beforeOverride = ownedValues[key][@"original"];
            if (beforeOverride && ![after[name] isEqualToNumber:beforeOverride])
                baselineChanges[key] = @{@"beforeOverride":beforeOverride, @"currentGameValue":after[name]};
            [ownedValues removeObjectForKey:key];
        }
    }
    return after;
}

static NSString *updateRenderSettings(NSMutableDictionary *state) {
    void *hub = renderHub();
    NSDictionary *types = featureTypes();
    NSDictionary *requests = requestedGraphics(selected);
    if (wantsBaseline) {
        NSMutableDictionary *comparison = [NSMutableDictionary dictionary];
        if (![selected[@"compareLOD"] boolValue] && requests[@"Streaming"])
            comparison[@"Streaming"] = requests[@"Streaming"];
        NSMutableDictionary *targets = [requests[@"RenderTargetSettings"] mutableCopy] ?: [NSMutableDictionary dictionary];
        if (qualityScaleOverride) targets[@"RenderingScale"] = qualityScaleOverride;
        else [targets removeObjectForKey:@"RenderingScale"];
        if (targets.count) comparison[@"RenderTargetSettings"] = targets;
        requests = comparison;
    }
    NSDictionary *labels = @{@"RenderTargetSettings":@"renderScale", @"ShadowMap":@"shadow", @"Streaming":@"lod", @"Grass":@"grass",
                             @"TAAU":@"taauSharpen", @"PostProcess":@"postSharpen", @"OnePassDeferred":@"sceneSplit"};
    NSMutableDictionary *effective = [NSMutableDictionary dictionary];
    NSMutableArray *errors = [NSMutableArray array];
    for (NSString *feature in types) {
        NSString *failure = nil;
        NSDictionary *actual = applyFeature(hub, feature, types[feature], requests[feature], &failure);
        if (actual.count) {
            effective[feature] = actual;
            state[[labels[feature] stringByAppendingString:@"Effective"]] = actual;
        }
        if (failure) {
            [errors addObject:failure];
            state[[labels[feature] stringByAppendingString:@"Error"]] = failure;
        }
    }
    state[@"effective"] = effective;
    state[@"overriddenParameters"] = [ownedValues.allKeys sortedArrayUsingSelector:@selector(compare:)];
    if (baselineChanges.count) state[@"baselineChanges"] = [baselineChanges copy];
    if (changedInGame.count) state[@"changedInGame"] = [changedInGame.allObjects sortedArrayUsingSelector:@selector(compare:)];
    if (errors.count) return [errors componentsJoinedByString:@"；"];
    confirmedBaseline = wantsBaseline;
    return nil;
}

static BOOL validValue(NSString *key, id value) {
    NSArray *choices = liveChoices()[key];
    if (!choices || !value) return NO;
    BOOL numeric = [choices.firstObject isKindOfClass:NSNumber.class];
    if (numeric != [value isKindOfClass:NSNumber.class]) return NO;
    if (numeric && (CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) != (CFGetTypeID((__bridge CFTypeRef)choices.firstObject) == CFBooleanGetTypeID())) return NO;
    return [choices containsObject:value];
}

static void handleLiveCommand(NSDictionary *command) {
    NSString *op = command[@"op"];
    lastCommandID = command[@"id"];
    needsApply = YES;
    commandProblem = nil;
    if ([op isEqual:@"compare"]) {
        id enabled = command[@"enabled"];
        if (![enabled isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)enabled) != CFBooleanGetTypeID()) {
            commandProblem = @"无效的画面对比状态"; return;
        }
        if (wantsBaseline && ![enabled boolValue]) [changedInGame removeAllObjects];
        wantsBaseline = [enabled boolValue];
    } else if ([op isEqual:@"configure"] && [command[@"settings"] isKindOfClass:NSDictionary.class]) {
        NSDictionary *values = command[@"settings"];
        for (NSString *key in values)
            if (!validValue(key, values[key])) { commandProblem = @"不支持的即时设置"; return; }
        NSDictionary *mapping = @{@"quality":@[@"RenderTargetSettings"], @"upscaler":@[@"RenderTargetSettings"], @"height":@[@"RenderTargetSettings"], @"lod":@[@"Streaming"], @"vegetationDensity":@[@"Grass"],
                                  @"shadowResolution":@[@"ShadowMap"], @"sharpen":@[@"TAAU", @"PostProcess"]};
        for (NSString *key in values) {
            if (values.count > 1 && [selected[key] isEqual:values[key]]) continue;
            for (NSString *feature in mapping[key]) for (NSString *name in featureTypes()[feature])
                [changedInGame removeObject:[feature stringByAppendingFormat:@"/%@", name]];
        }
        [selected addEntriesFromDictionary:values];
    } else commandProblem = @"未知的实时画质命令";
}

static void readLiveCommand(void) {
    NSString *file = runtimeFile172(@"graphics172-command.json", YES);
    NSData *data = [NSData dataWithContentsOfFile:file];
    if (!data.length || data.length > 8192) return;
    NSDictionary *command = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if (![command isKindOfClass:NSDictionary.class] || ![session isEqual:command[@"session"]]) return;
    NSString *identifier = command[@"id"];
    if (![identifier isKindOfClass:NSString.class] || !identifier.length ||
        identifier.length > 128 || [lastFileID isEqual:identifier]) return;
    lastFileID = identifier;
    handleLiveCommand(command);
}
