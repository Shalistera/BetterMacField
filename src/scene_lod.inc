/* Geometry streaming distances from DesktopSettings.ini tiers 3000/4000.
   Prefab mesh chains and shading remain independent of these overrides. */
static NSString *const scene_lod_saved_key=@"BetterMacFieldSceneLODProfile";
static bool scene_lod_startup_pending=true;
static NSDictionary *scene_lod_profiles(void) {
    return @{@"pc-medium":@{@"chunkLoadRadius":@128,@"defaultLayerLoadRadius":@128,
                            @"hlod0LayerLoadRadius":@384,@"tinyLayerLoadRadius":@128},
             @"pc-high":@{@"chunkLoadRadius":@160,@"defaultLayerLoadRadius":@160,
                          @"hlod0LayerLoadRadius":@512,@"tinyLayerLoadRadius":@128}};
}

static NSDictionary *scene_lod_values(void) {
    NSDictionary *streaming=display_feature_parameters(@"Streaming");
    NSMutableDictionary *values=[NSMutableDictionary dictionary];
    for(NSString *key in scene_lod_profiles()[@"pc-medium"])
        if(streaming[key])values[key]=streaming[key];
    return values;
}

static NSString *scene_lod_selected_profile(void) {
    NSDictionary *values=scene_lod_values(),*profiles=scene_lod_profiles();
    for(NSString *name in profiles)if([values isEqual:profiles[name]])return name;
    return @"restore";
}

static void *scene_lod_streaming_scene(void) {
    bool ok=false;
    void *game=input_static(find_class("Beyond.Gameplay","GameInstance"),"get_instance",0,NULL);
    void *manager=field_reference(field_reference(game,"m_world"),"sceneManager");
    void *scene=manager ? invoke(manager,"get_curScene",0,NULL,&ok) : NULL;
    void *streaming=scene ? invoke(scene,"get_streamingScene",0,NULL,&ok) : NULL;
    return ok ? streaming : NULL;
}

static bool scene_lod_is_ready(void) {
    // Streaming remains available while menus release the gameplay cursor.
    return scene_lod_streaming_scene() && scene_lod_values().count==4;
}

static bool scene_lod_refresh(void) {
    void *streaming=scene_lod_streaming_scene();
    if(!streaming)return false;
    bool ok=false;
    invoke(streaming,"UpdateNeedProcessChunkGridSet",0,NULL,&ok);
    return ok;
}

static void scene_lod_control(NSDictionary *command) {
    NSString *profile=command[@"profile"] ?: @"status";
    NSString *result=@"status";
    bool restore=[profile isEqual:@"restore"];
    NSDictionary *values=scene_lod_profiles()[profile];
    if(![profile isEqual:@"status"]){
        if(!restore && !values)result=@"invalid_profile";
        else if(!scene_lod_is_ready())result=@"scene_not_ready";
        else {
            NSDictionary *previous=scene_lod_values();bool ok=previous.count==4;
            for(NSString *key in previous){
                if(!ok)break;
                ok=override_parameter(get_hub(),[key stringByAppendingString:@"|Streaming"],
                                      restore ? nil : [values[key] stringValue],restore);
            }
            if(ok)ok=scene_lod_refresh();
            if(ok && values)ok=[scene_lod_values() isEqual:values];
            if(!ok){
                for(NSString *key in previous)
                    override_parameter(get_hub(),[key stringByAppendingString:@"|Streaming"],[previous[key] stringValue],false);
                scene_lod_refresh();
            }
            result=ok ? @"applied" : @"failed";
            if(ok && [command[@"remember"] boolValue]){
                [NSUserDefaults.standardUserDefaults setObject:profile forKey:scene_lod_saved_key];
                scene_lod_startup_pending=false;
            }
        }
        display_last_result=[result isEqual:@"applied"] ? @"LOD 加载距离已应用" :
            ([result isEqual:@"scene_not_ready"] ? @"请进入场景后调整 LOD 距离" : @"LOD 加载距离未应用");
    }
    NSDictionary *state=@{@"requested":profile,@"profile":scene_lod_selected_profile(),
                           @"result":result,@"effective":scene_lod_values(),
                           @"savedProfile":[NSUserDefaults.standardUserDefaults stringForKey:scene_lod_saved_key] ?: @"restore"};
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:@"/private/tmp/bettermacfield-scene-lod.json" atomically:YES];
    fprintf(output,"scene_lod=%s profile=%s\n",result.UTF8String,[state[@"profile"] UTF8String]);
}

static void scene_lod_startup_tick(void) {
    if(!scene_lod_startup_pending)return;
    NSString *profile=[NSUserDefaults.standardUserDefaults stringForKey:scene_lod_saved_key];
    if(!scene_lod_profiles()[profile ?: @""]){scene_lod_startup_pending=false;return;}
    if(!scene_lod_is_ready())return;
    scene_lod_startup_pending=false;
    scene_lod_control(@{@"profile":profile});
}
