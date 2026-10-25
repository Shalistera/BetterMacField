static NSString *const core_shadow_key=@"BetterMacFieldShadowResolution";
static bool core_shadow_pending=true;
static int core_shadow_current;
static bool core_comparison_active;
static bool core_vegetation_density(NSNumber *value) {
    double density=value.doubleValue;
    if(density!=-1 && density!=0.5 && density!=0.7 && density!=1)return false;
    bool restore=density==-1;
    bool ok=override_parameter(get_hub(),@"GrassGlobalSparsity|Grass",
                               restore ? nil : value.stringValue,restore);
    NSNumber *effective=display_feature_parameters(@"Grass")[@"GrassGlobalSparsity"];
    ok=ok && effective && (restore || fabs(effective.doubleValue-density)<0.00001);
    fprintf(output,"vegetation_density=%s requested=%.2f effective=%.2f\n",
            ok ? "applied" : "failed",density,effective ? effective.doubleValue : -1);
    return ok;
}
static bool core_shadow_resolution(int resolution,bool remember) {
    if(resolution!=0 && resolution!=1024 && resolution!=2048)return false;
    bool ok=true;
    for(NSString *key in @[@"CSMTileResolution|ShadowMap",@"CharacterShadowMapResolution|ShadowMap"])
        ok=override_parameter(get_hub(),key,resolution ? [NSString stringWithFormat:@"%d",resolution] : nil,resolution==0) && ok;
    if(ok && remember)[NSUserDefaults.standardUserDefaults setInteger:resolution forKey:core_shadow_key];
    if(ok)core_shadow_current=resolution;
    display_last_result=ok ? @"阴影精度已应用" : @"阴影参数未就绪";
    fprintf(output,"shadow_resolution=%s value=%d\n",ok ? "applied" : "failed",resolution);
    return ok;
}
static void core_shadow_startup(void) {
    if(!core_shadow_pending)return;core_shadow_pending=false;
    int saved=(int)[NSUserDefaults.standardUserDefaults integerForKey:core_shadow_key];
    if(saved)core_shadow_resolution(saved,false);
}
static NSDictionary *core_comparison_saved;
static NSDictionary *bmf_lighting_state(void) {
    return @{@"screenSpace":@(atomic_load(&metal_screen_ao_strength)>0),@"strength":@(atomic_load(&metal_screen_ao_strength)),
        @"radius":@(atomic_load(&metal_screen_ao_radius)),@"nativeContact":@(atomic_load(&metal_native_contact_strength)>0),
        @"contactStrength":@(atomic_load(&metal_native_contact_strength)),@"contactLength":@(atomic_load(&metal_native_contact_length)),
        @"reflection":@(atomic_load(&metal_ssr_history_enabled)),@"ssrStrength":@(atomic_load(&metal_ssr_strength)),
        @"ssrSteps":@(atomic_load(&metal_ssr_steps)),@"characterMode":@(atomic_load(&character_skin_mode))};
}
static NSString *bmf_upscaler_mode(void) {
    return !atomic_load(&native_temporal_enabled) ? @"taau" :
        (atomic_load(&native_spatial_enabled) ? @"metalfx-spatial" : @"metalfx-native");
}
static NSMutableDictionary *bmf_comparison_state(bool rendering,bool lod) {
    NSMutableDictionary *state=[@{@"lighting":bmf_lighting_state(),
        @"shadow":@(character_shadow_override_enabled),@"shadowResolution":@(core_shadow_current),
        @"sharpen":display_state()[@"sharpen"]} mutableCopy];
    if(rendering){
        NSNumber *scale=display_feature_parameters(@"RenderTargetSettings")[@"RenderingScale"];
        if(!scale)return nil;
        state[@"upscaler"]=bmf_upscaler_mode();state[@"renderScale"]=scale;
    }
    if(lod){
        if(!scene_lod_is_ready())return nil;
        state[@"lod"]=scene_lod_selected_profile();
    }
    return state;
}
static bool bmf_comparison_apply(NSDictionary *state) {
    NSDictionary *lighting=state[@"lighting"];
    bool ok=native_lighting_control(lighting);
    ok=override_volume(@"HGCharacterVolume",@{@"enabled":state[@"shadow"]},false) && ok;
    ok=core_shadow_resolution([state[@"shadowResolution"] intValue],false) && ok;
    character_skin_control(@{@"mode":lighting[@"characterMode"]});
    metal_ssr_history_control(@{@"strength":lighting[@"ssrStrength"],@"steps":lighting[@"ssrSteps"]});
    for(NSString *key in @[@"1KSharpenStrength",@"2KSharpenStrength",@"4KSharpenStrength",@"postProcess"]){
        NSNumber *value=state[@"sharpen"][key];
        NSString *parameter=[key isEqual:@"postProcess"] ? @"SharpenEnabled|PostProcess" : [key stringByAppendingString:@"|TAAU"];
        ok=override_parameter(get_hub(),parameter,value.stringValue,value==nil) && ok;
    }
    if(state[@"upscaler"]){
        ok=display_upscaler(@{@"mode":state[@"upscaler"],@"remember":@NO,@"reapplyPreset":@NO}) && ok;
        // Comparison uses only a process-local override, leaving the saved game tier intact.
        ok=override_parameter(get_hub(),@"RenderingScale|RenderTargetSettings",[state[@"renderScale"] stringValue],false) && ok;
        NSNumber *actual=display_feature_parameters(@"RenderTargetSettings")[@"RenderingScale"];
        ok=actual && fabs(actual.doubleValue-[state[@"renderScale"] doubleValue])<0.001 && ok;
    }
    if(state[@"lod"])ok=scene_lod_control(@{@"profile":state[@"lod"],@"remember":@NO}) && ok;
    return ok;
}
static bool bmf_compare(void) {
    if(!input_scene_is_loaded())return false;
    bool restoring=core_comparison_active;
    bool rendering=restoring ? core_comparison_saved[@"upscaler"]!=nil : [bmf_settings[@"compareRendering"] boolValue];
    bool lod=restoring ? core_comparison_saved[@"lod"]!=nil : [bmf_settings[@"compareLOD"] boolValue];
    NSDictionary *before=bmf_comparison_state(rendering,lod);
    if(!before){display_last_result=@"画面参数未就绪";return false;}
    NSMutableDictionary *target=[(restoring ? core_comparison_saved : before) mutableCopy];
    if(!restoring){
        NSMutableDictionary *lighting=[before[@"lighting"] mutableCopy];
        [lighting addEntriesFromDictionary:@{@"screenSpace":@NO,@"nativeContact":@NO,@"reflection":@NO,@"characterMode":@0}];
        target[@"lighting"]=lighting;target[@"shadow"]=@NO;target[@"shadowResolution"]=@0;
        target[@"sharpen"]=@{};
        if(rendering){target[@"upscaler"]=@"taau";target[@"renderScale"]=@(display_parameter_for_ratio(0.5,display_state()));}
        if(lod)target[@"lod"]=@"restore";
    }
    bool ok=bmf_comparison_apply(target);
    if(ok){
        if(!restoring)core_comparison_saved=before;
        core_comparison_active=!restoring;
        display_last_result=restoring ? @"已恢复增强设置" : @"已切回原生设置";
    }else{
        bool rolledBack=bmf_comparison_apply(before);
        display_last_result=rolledBack ? @"切换失败，已恢复原设置" : @"切换未完成，请重新应用设置";
    }
    fprintf(output,"effects_comparison=%s baseline=%d rendering=%d lod=%d\n",ok ? "applied" : "failed",core_comparison_active,rendering,lod);return ok;
}
static void bmf_apply_settings(void) {
    if(!bmf_settings_pending || !input_scene_is_loaded())return;
    if(core_comparison_active && !bmf_compare())return;
    bmf_settings_pending=false;
    NSDictionary *v=bmf_settings;
    int h=[v[@"height"] intValue];
    if(h){
        void *screen=find_class("UnityEngine","Screen");void *height=input_static(screen,"get_height",0,NULL);
        if(height && *(int *)object_unbox(height)!=h)display_control(@{@"width":@(h*16/9),@"height":@(h)});
    }
    if(v[@"quality"])display_control(@{@"preset":v[@"quality"]});
    if(v[@"upscaler"])display_upscaler(@{@"mode":v[@"upscaler"],@"remember":@YES});
    if(v[@"sharpen"])display_sharpen(v[@"sharpen"]);
    if(v[@"lod"])scene_lod_control(@{@"profile":v[@"lod"],@"remember":@YES});
    bool ok=true;
    if(v[@"vegetationDensity"])ok=core_vegetation_density(v[@"vegetationDensity"]) && ok;
    if(v[@"keyboardBindings"]){
        NSString *result=game_settings_keyboard_control(find_class("Beyond","GameSetting"),[v[@"keyboardBindings"] boolValue]);
        ok=([@[@"enabled",@"already_enabled",@"disabled"] containsObject:result]) && ok;
        fprintf(output,"keyboard_settings=%s\n",result.UTF8String);
    }
    if(v[@"upscaler"]){
        BOOL expected=![v[@"upscaler"] isEqual:@"taau"];
        ok=(atomic_load(&native_temporal_enabled)==expected) && ok;
        if(expected)ok=ok && (atomic_load(&native_spatial_enabled)==[v[@"upscaler"] isEqual:@"metalfx-spatial"]);
    }
    if(v[@"selfShadow"])ok=override_volume(@"HGCharacterVolume",@{@"enabled":v[@"selfShadow"],@"remember":@YES},false) && ok;
    if(v[@"shadowResolution"])ok=core_shadow_resolution([v[@"shadowResolution"] intValue],true) && ok;
    if(v[@"characterLighting"]){
        unsigned mode=([v[@"characterLighting"] boolValue] ? 3u : 0u) | ([v[@"characterSpecular"] boolValue] ? 4u : 0u);
        character_skin_control(@{@"mode":@(mode)});
        if(mode && !character_skin_source)ok=false;
    }
    if(v[@"gtao"] || v[@"contact"] || v[@"ssr"])
        ok=native_lighting_control(@{@"screenSpace":v[@"gtao"] ?: @NO,@"nativeContact":v[@"contact"] ?: @NO,
            @"reflection":v[@"ssr"] ?: @NO,@"strength":@1,@"radius":@1,@"contactStrength":@1,@"contactLength":@0.5}) && ok;
    if(v[@"keyboard"]){
        bool enabled=[v[@"keyboard"] boolValue];
        if(!enabled){input_buttons(false);input_move(false);input_bridge(false,false);input_capture(false);select_input_mode(@"touch");}
        input_auto_configure(@{@"enabled":@(enabled)});
    }
    if(v[@"backgroundLimit"])bmf_background_limit=[v[@"backgroundLimit"] boolValue];
    bmf_settings_result=ok ? @"设置已应用" : @"部分设置未就绪，查看运行状态与日志";
    fprintf(output,"launcher_settings=%s\n",ok ? "applied" : "partial");
}
@interface BMFMenu : NSObject<NSMenuDelegate>
@end
@implementation BMFMenu
- (void)compare:(id)sender {bmf_compare();}
- (void)feature:(NSMenuItem *)item {
    NSDictionary *entry=item.representedObject;NSString *key=entry[@"key"];
    id value=entry[@"value"] ?: @(![bmf_settings[key] boolValue]);
    bmf_request_settings(@{key:value});
}
- (void)lighting:(id)sender {
    bool enable=!(native_lighting_any() || character_shadow_override_enabled);
    bmf_request_settings(@{@"gtao":@(enable),@"contact":@(enable),@"selfShadow":@(enable),@"characterLighting":@(enable),@"ssr":@(enable && [bmf_settings[@"ssr"] boolValue])});
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    for(NSMenuItem *item in menu.itemArray){
        if(item.tag==1){item.enabled=input_scene_is_loaded();item.state=(native_lighting_any() || character_shadow_override_enabled) ? NSControlStateValueOn : NSControlStateValueOff;}
        if(item.tag==3)item.title=display_last_result;
        if(item.tag==4){item.enabled=input_scene_is_loaded();item.title=core_comparison_active ? @"画面对比：恢复增强设置" : @"画面对比：切回原生设置";}
        if(item.action==@selector(feature:)){
            NSDictionary *entry=item.representedObject;id selected=bmf_settings[entry[@"key"]];
            item.state=(entry[@"value"] ? [entry[@"value"] isEqual:selected] : [selected boolValue]) ? NSControlStateValueOn : NSControlStateValueOff;
        }
    }
}
@end
static BMFMenu *core_menu;
static id core_comparison_shortcut;
static void install_core_menu(void) {
    core_menu=[BMFMenu new];NSMenu *menu=[[NSMenu alloc] initWithTitle:@"BetterMacField"];
    menu.delegate=core_menu;menu.autoenablesItems=NO;
    NSMenuItem *master=[menu addItemWithTitle:@"增强光影总开关" action:@selector(lighting:) keyEquivalent:@""];
    master.target=core_menu;master.tag=1;
    NSMutableDictionary *pages=[NSMutableDictionary dictionary];
    for(NSDictionary *field in bmf_field_catalog[@"fields"]){
        if([field[@"key"] isEqual:@"metalHUD"])continue;
        NSMenu *page=pages[field[@"page"]];
        if(!page){page=[[NSMenu alloc] initWithTitle:field[@"page"]];page.delegate=core_menu;page.autoenablesItems=NO;pages[field[@"page"]]=page;
            [menu addItemWithTitle:field[@"page"] action:NULL keyEquivalent:@""].submenu=page;}
        if(field[@"choices"]){
            NSMenu *choices=[[NSMenu alloc] initWithTitle:field[@"title"]];choices.delegate=core_menu;choices.autoenablesItems=NO;
            [page addItemWithTitle:field[@"title"] action:NULL keyEquivalent:@""].submenu=choices;
            for(NSDictionary *option in field[@"choices"]){
                NSMenuItem *item=[choices addItemWithTitle:option[@"title"] action:@selector(feature:) keyEquivalent:@""];item.target=core_menu;
                item.representedObject=@{@"key":field[@"key"],@"value":option[@"value"]};item.toolTip=field[@"help"];
            }
        }else{
            NSMenuItem *item=[page addItemWithTitle:field[@"title"] action:@selector(feature:) keyEquivalent:@""];item.target=core_menu;
            item.representedObject=@{@"key":field[@"key"]};item.toolTip=field[@"help"];
        }
    }
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *comparison=[menu addItemWithTitle:@"画面对比：切回原生设置" action:@selector(compare:) keyEquivalent:@"g"];
    comparison.target=core_menu;comparison.tag=4;comparison.keyEquivalentModifierMask=NSEventModifierFlagCommand;
    comparison.toolTip=@"再次按下恢复切换前的设置。";
    [menu addItemWithTitle:display_last_result action:NULL keyEquivalent:@""].tag=3;
    [NSApp.mainMenu addItemWithTitle:@"BetterMacField" action:NULL keyEquivalent:@""].submenu=menu;
    core_comparison_shortcut=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event){
        NSEventModifierFlags flags=event.modifierFlags & (NSEventModifierFlagCommand|NSEventModifierFlagShift|NSEventModifierFlagOption|NSEventModifierFlagControl);
        if(NSApp.isActive && event.keyCode==5 && flags==NSEventModifierFlagCommand){
            if(!event.isARepeat)bmf_compare();return nil;
        }return event;
    }];
    fprintf(output,"menu=installed\n");
}
static void core_status(NSString *identifier) {
    NSDictionary *state=@{@"id":identifier ?: @"",@"pid":@(getpid()),@"updatedAt":@(NSDate.date.timeIntervalSince1970),
        @"sceneReady":@(input_scene_is_loaded()),@"selfShadow":@(character_shadow_override_enabled),@"shadowResolution":@(core_shadow_current),
        @"comparisonBaseline":@(core_comparison_active),
        @"frameRate":frame_rate_state(),
        @"sceneVegetation":vegetation_state(),
        @"keyboardBindings":game_settings_keyboard_status(find_class("Beyond","GameSetting"),nil),
        @"effective":@{@"upscaler":bmf_upscaler_mode(),@"renderScale":display_feature_parameters(@"RenderTargetSettings")[@"RenderingScale"] ?: @0,@"lod":scene_lod_selected_profile(),
            @"vegetationDensity":display_feature_parameters(@"Grass")[@"GrassGlobalSparsity"] ?: @(-1)},
        @"metalFX":native_temporal_state(),@"lighting":bmf_lighting_state(),
        @"gtaoEncodes":@(atomic_load(&official_gtao_encodes)),@"gtaoErrors":@(atomic_load(&official_gtao_errors)),
        @"contactEncodes":@(atomic_load(&official_contact_encodes)),@"contactErrors":@(atomic_load(&official_contact_errors)),
        @"lightingPrograms":@(atomic_load(&metal_ao_compiled)),@"characterPrograms":@(atomic_load(&character_skin_compiled)),
        @"keyboard":@(input_auto_monitor!=nil),@"settingsPending":@(bmf_settings_pending),@"settingsResult":bmf_settings_result,
        @"message":display_last_result,@"settings":bmf_settings ?: @{}};
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:runtime_path(@"bettermacfield-status.json") atomically:YES];
}
static void core_command(void) {
    static NSString *last;static int ticks;
    NSData *data=[NSData dataWithContentsOfFile:runtime_path(@"bettermacfield-command.json")];
    NSDictionary *command=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if(![command isKindOfClass:NSDictionary.class] || ![command[@"id"] isKindOfClass:NSString.class] || [last isEqual:command[@"id"]]){
        if(++ticks%3==0)core_status(last);return;
    }
    last=command[@"id"];NSString *op=command[@"op"];
    if([op isEqual:@"configure"])bmf_request_settings(command[@"settings"] ?: @{});
    else if([op isEqual:@"upscaler"])display_upscaler(command);
    else if([op isEqual:@"display"])display_control(command);
    else if([op isEqual:@"scene-lod"])scene_lod_control(command);
    else if([op isEqual:@"shadow"])override_volume(@"HGCharacterVolume",command,false);
    else if([op isEqual:@"shadow-resolution"])core_shadow_resolution([command[@"value"] intValue],true);
    else if([op isEqual:@"settings"])game_settings(command);
    else if([op isEqual:@"gpu-timing"])capture_metal_timing(command);
    else if([op isEqual:@"compare"])bmf_compare();
    else if(![op isEqual:@"status"])return;
    core_status(last);
}
