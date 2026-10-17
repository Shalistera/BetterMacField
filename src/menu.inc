static NSString *const core_shadow_key=@"BetterMacFieldShadowResolution";
static bool core_shadow_pending=true;
static void core_shadow_resolution(int resolution,bool remember) {
    if(resolution!=0 && resolution!=1024 && resolution!=2048)return;
    bool ok=true;
    for(NSString *key in @[@"CSMTileResolution|ShadowMap",@"CharacterShadowMapResolution|ShadowMap"])
        ok=override_parameter(get_hub(),key,resolution ? [NSString stringWithFormat:@"%d",resolution] : nil,resolution==0) && ok;
    if(ok && remember)[NSUserDefaults.standardUserDefaults setInteger:resolution forKey:core_shadow_key];
    display_last_result=ok ? @"阴影精度已应用" : @"阴影参数未就绪";
    fprintf(output,"shadow_resolution=%s value=%d\n",ok ? "applied" : "failed",resolution);
}
static void core_shadow_startup(void) {
    if(!core_shadow_pending)return;core_shadow_pending=false;
    int saved=(int)[NSUserDefaults.standardUserDefaults integerForKey:core_shadow_key];
    if(saved)core_shadow_resolution(saved,false);
}
@interface BMFMenu : NSObject<NSMenuDelegate>
@end
@implementation BMFMenu
- (void)selfShadow:(NSMenuItem *)item {
    override_volume(@"HGCharacterVolume",@{@"enabled":@(!character_shadow_override_enabled),@"remember":@YES},false);
}
- (void)resolution:(NSMenuItem *)item {display_control(item.representedObject);}
- (void)quality:(NSMenuItem *)item {display_control(@{@"preset":item.representedObject});}
- (void)upscaler:(NSMenuItem *)item {display_upscaler(@{@"mode":item.representedObject,@"remember":@YES});}
- (void)lod:(NSMenuItem *)item {scene_lod_control(@{@"profile":item.representedObject,@"remember":@YES});}
- (void)shadowResolution:(NSMenuItem *)item {core_shadow_resolution([item.representedObject intValue],true);}
- (void)keyboard:(NSMenuItem *)item {
    bool enable=input_auto_monitor==nil;
    if(!enable){
        input_buttons(false);input_move(false);input_bridge(false,false);input_capture(false);
        select_input_mode(@"touch");
    }
    input_auto_configure(@{@"enabled":@(enable)});
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    bool ready=input_scene_is_ready();
    for(NSMenuItem *item in menu.itemArray){
        if(item.tag==1){item.enabled=ready;item.state=character_shadow_override_enabled ? NSControlStateValueOn : NSControlStateValueOff;}
        if(item.tag==2)item.state=input_auto_monitor ? NSControlStateValueOn : NSControlStateValueOff;
        if(item.tag==3)item.title=display_last_result;
        if(item.action==@selector(upscaler:)){
            NSString *selected=atomic_load(&native_temporal_enabled) ? (atomic_load(&native_spatial_enabled) ? @"metalfx-spatial" : @"metalfx-native") : @"taau";
            item.state=[item.representedObject isEqual:selected] ? NSControlStateValueOn : NSControlStateValueOff;item.enabled=ready;
        }
        if(item.action==@selector(lod:)){
            item.enabled=scene_lod_is_ready();item.state=[item.representedObject isEqual:scene_lod_selected_profile()] ? NSControlStateValueOn : NSControlStateValueOff;
        }
    }
}
@end
static BMFMenu *core_menu;
static NSMenu *core_submenu(NSMenu *root,NSString *title) {
    NSMenu *menu=[[NSMenu alloc] initWithTitle:title];menu.autoenablesItems=NO;menu.delegate=core_menu;
    [root addItemWithTitle:title action:NULL keyEquivalent:@""].submenu=menu;return menu;
}
static void core_item(NSMenu *menu,NSString *title,SEL action,id value) {
    NSMenuItem *item=[menu addItemWithTitle:title action:action keyEquivalent:@""];item.target=core_menu;item.representedObject=value;
}
static void install_core_menu(void) {
    core_menu=[BMFMenu new];NSMenu *menu=[[NSMenu alloc] initWithTitle:@"画面"];
    menu.delegate=core_menu;menu.autoenablesItems=NO;
    core_item(menu,@"角色自阴影",@selector(selfShadow:),nil);menu.itemArray.lastObject.tag=1;
    NSMenu *shadows=core_submenu(menu,@"阴影精度");
    core_item(shadows,@"游戏默认",@selector(shadowResolution:),@0);
    core_item(shadows,@"1024",@selector(shadowResolution:),@1024);
    core_item(shadows,@"2048",@selector(shadowResolution:),@2048);
    NSMenu *resolution=core_submenu(menu,@"输出分辨率");
    for(NSNumber *n in @[@720,@1080,@1440,@2160]){
        int h=n.intValue,w=h*16/9;core_item(resolution,[NSString stringWithFormat:@"%d × %d",w,h],@selector(resolution:),@{@"width":@(w),@"height":@(h)});
    }
    NSMenu *quality=core_submenu(menu,@"超分质量");
    for(NSArray *p in display_quality_presets())core_item(quality,p[0],@selector(quality:),p[0]);
    NSMenu *aa=core_submenu(menu,@"画质提升");
    core_item(aa,@"TAAU（原版）",@selector(upscaler:),@"taau");
    core_item(aa,@"MetalFX Temporal＋原生环境光",@selector(upscaler:),@"metalfx-native");
    core_item(aa,@"MetalFX Spatial＋原生环境光（备选）",@selector(upscaler:),@"metalfx-spatial");
    aa.itemArray.lastObject.toolTip=@"沿用原版 TAAU 抗锯齿，已观察到移动拖影；建议优先 Temporal。";
    NSMenu *lod=core_submenu(menu,@"LOD 加载距离");
    for(NSArray *p in @[@[@"手机默认",@"restore"],@[@"PC 中档",@"pc-medium"],@[@"PC 高档",@"pc-high"]])core_item(lod,p[0],@selector(lod:),p[1]);
    [menu addItem:NSMenuItem.separatorItem];
    core_item(menu,@"自动键鼠模式",@selector(keyboard:),nil);menu.itemArray.lastObject.tag=2;
    core_item(menu,display_last_result,NULL,nil);menu.itemArray.lastObject.tag=3;
    [NSApp.mainMenu addItemWithTitle:@"BetterMacField" action:NULL keyEquivalent:@""].submenu=menu;
    fprintf(output,"menu=installed\n");
}

/* Local diagnostics/control is limited to the same published menu features. */
static void core_command(void) {
    static NSString *last;
    NSData *data=[NSData dataWithContentsOfFile:@"/private/tmp/bettermacfield-command.json"];
    NSDictionary *command=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if(![command isKindOfClass:NSDictionary.class] || ![command[@"id"] isKindOfClass:NSString.class] || [last isEqual:command[@"id"]])return;
    last=command[@"id"];NSString *op=command[@"op"];
    if([op isEqual:@"upscaler"])display_upscaler(command);
    else if([op isEqual:@"display"])display_control(command);
    else if([op isEqual:@"scene-lod"])scene_lod_control(command);
    else if([op isEqual:@"shadow"])override_volume(@"HGCharacterVolume",command,false);
    else if([op isEqual:@"shadow-resolution"])core_shadow_resolution([command[@"value"] intValue],true);
    else if([op isEqual:@"settings"])game_settings(command);
    else if(![op isEqual:@"status"])return;
    NSDictionary *state=@{@"id":last,@"pid":@(getpid()),@"sceneReady":@(input_scene_is_ready()),
        @"selfShadow":@(character_shadow_override_enabled),@"metalFX":native_temporal_state(),
        @"keyboard":@(input_auto_monitor!=nil),@"display":display_state()};
    [[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:@"/private/tmp/bettermacfield-status.json" atomically:YES];
}
