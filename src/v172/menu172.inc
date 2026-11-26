#import <AppKit/AppKit.h>
#include <dlfcn.h>

@interface BMFMenuHint172 : NSView
@end
@implementation BMFMenuHint172
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
@end

static BOOL menu172HintShown;
static void showMenu172Hint(void) {
    if(menu172HintShown || !NSApp.isActive)return;
    NSWindow *window=NSApp.keyWindow ?: NSApp.mainWindow;
    NSView *view=window.contentView;
    if(!window.visible || [window isKindOfClass:NSPanel.class] || NSWidth(view.bounds)<400 || NSHeight(view.bounds)<250)return;
    NSTextField *text=[NSTextField labelWithString:@"⌘⌥P 画质菜单　·　⌘G 画面对比"];
    text.font=[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];text.textColor=NSColor.whiteColor;
    [text sizeToFit];
    NSSize size=NSMakeSize(NSWidth(text.frame)+28,NSHeight(text.frame)+16);
    CGFloat y=view.isFlipped ? NSMaxY(view.bounds)-size.height-28 : NSMinY(view.bounds)+28;
    BMFMenuHint172 *hint=[[BMFMenuHint172 alloc] initWithFrame:NSMakeRect(NSMidX(view.bounds)-size.width/2,y,size.width,size.height)];
    hint.autoresizingMask=NSViewMinXMargin|NSViewMaxXMargin|(view.isFlipped ? NSViewMinYMargin : NSViewMaxYMargin);
    hint.wantsLayer=YES;hint.layer.cornerRadius=8;hint.layer.backgroundColor=[NSColor colorWithWhite:0.07 alpha:0.85].CGColor;
    [text setFrameOrigin:NSMakePoint(14,8)];[hint addSubview:text];[view addSubview:hint positioned:NSWindowAbove relativeTo:nil];
    menu172HintShown=YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{[hint removeFromSuperview];});
}

static NSArray *menu172Fields(void) {
    NSData *data = [NSData dataWithContentsOfFile:graphicsResource172(@"features.json")];
    NSDictionary *catalog = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    return catalog[@"fields"] ?: @[];
}

@interface BMFMenu172 : NSObject <NSMenuDelegate>
@property(nonatomic, strong) NSMenuItem *entry;
@property(nonatomic, strong) NSMenuItem *status;
@property(nonatomic, strong) NSMenuItem *compare;
@property(nonatomic, strong) NSDictionary<NSString *, NSMenuItem *> *groups;
@property(nonatomic, strong) id shortcuts;
@property(nonatomic, copy) NSDictionary *selected;
@property(nonatomic, copy) NSDictionary *runtime;
@property(nonatomic) BOOL comparing;
@property(nonatomic, copy) void (^perform)(NSString *, id);
@end

@implementation BMFMenu172
- (instancetype)init {
    self = [super init];
    if (!self) return nil;
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"BetterMacField"];
    menu.autoenablesItems = NO;
    menu.delegate = self;
    _entry = [[NSMenuItem alloc] initWithTitle:menu.title action:NULL keyEquivalent:@""];
    _entry.submenu = menu;
    _status = [menu addItemWithTitle:@"等待游戏图形设置" action:NULL keyEquivalent:@""];
    _status.enabled = NO;
    _compare = [menu addItemWithTitle:@"临时查看原画面" action:@selector(toggleComparison:) keyEquivalent:@"g"];
    _compare.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    _compare.target = self;
    [menu addItem:NSMenuItem.separatorItem];

    NSDictionary *titles = @{@"gtao":@"GTAO 环境遮蔽", @"contact":@"接触阴影", @"ssr":@"屏幕空间反射",
                             @"shadowResolution":@"阴影精度", @"lod":@"场景 LOD", @"vegetationDensity":@"草地密度",
                             @"sharpen":@"锐化", @"backgroundLimit":@"后台节能", @"upscaler":@"超分模式",
                             @"selfShadow":@"角色自阴影", @"keyboard":@"自动键鼠模式",
                             @"quality":@"超分质量",
                             @"characterLighting":@"角色面光与阴影滤波",
                             @"characterSpecular":@"角色 PC 高光", @"height":@"输出分辨率",
                             @"compareLOD":@"对比时恢复手机 LOD", @"compareRendering":@"对比时切换 TAAU · 目标 50%"};
    NSMutableDictionary *groups = [NSMutableDictionary dictionary];
    for (NSDictionary *field in menu172Fields()) {
        NSString *key = field[@"key"], *title = titles[key];
        if (!title || !liveChoices()[key]) continue;
        NSMenuItem *group = [menu addItemWithTitle:title action:NULL keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        submenu.autoenablesItems = NO;
        group.submenu = submenu;
        group.representedObject = title;
        groups[key] = group;
        NSString *onTitle = [key isEqual:@"backgroundLimit"] ? @"开启 · 后台 15 FPS" : @"开启";
        NSArray *choices = field[@"choices"] ?: @[@{@"title":onTitle, @"value":@YES},
                                                 @{@"title":@"关闭", @"value":@NO}];
        for (NSDictionary *choice in choices) {
            NSMenuItem *item = [submenu addItemWithTitle:choice[@"title"] action:@selector(choose:) keyEquivalent:@""];
            item.target = self;
            item.representedObject = @[key, choice[@"value"]];
        }
        [submenu addItem:NSMenuItem.separatorItem];
        [submenu addItemWithTitle:@"等待游戏读回" action:NULL keyEquivalent:@""].enabled = NO;
    }
    _groups = [groups copy];
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *popup = [menu addItemWithTitle:@"在游戏窗口呼出菜单" action:@selector(popup:) keyEquivalent:@"p"];
    popup.target = self;
    popup.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    __weak BMFMenu172 *weakSelf = self;
    _shortcuts = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
        NSEventModifierFlags modifiers = event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption |
                                                                NSEventModifierFlagShift | NSEventModifierFlagControl);
        NSString *key = event.charactersIgnoringModifiers.lowercaseString;
        if (modifiers == (NSEventModifierFlagCommand | NSEventModifierFlagOption) && [key isEqual:@"p"]) {
            if (!event.isARepeat) [weakSelf popup:nil];
            return nil;
        }
        if (modifiers == NSEventModifierFlagCommand && [key isEqual:@"g"]) {
            if (!event.isARepeat) [weakSelf toggleComparison:nil];
            return nil;
        }
        return event;
    }];
    return self;
}

- (void)choose:(NSMenuItem *)item {
    if (!self.comparing && self.perform) self.perform(item.representedObject[0], item.representedObject[1]);
}

- (void)toggleComparison:(id)sender {
    if (self.perform) self.perform(@"compare", @(!self.comparing));
}

- (void)popup:(id)sender {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSWindow *window = NSApp.keyWindow ?: NSApp.mainWindow;
        NSPoint point = NSEvent.mouseLocation;
        if (window && !NSPointInRect(point, window.frame))
            point = NSMakePoint(NSMidX(window.frame), NSMidY(window.frame));
        [self.entry.submenu popUpMenuPositioningItem:nil atLocation:point inView:nil];
    });
}

- (NSString *)readback:(NSString *)key {
    NSDictionary *state = self.runtime, *effective = state[@"effective"];
    if([@[@"gtao",@"contact",@"ssr"] containsObject:key])
        return state[@"nativeLighting172"][@"features"][key][@"reason"] ?: @"等待场景光照读回";
    if ([key isEqual:@"height"]) {
        NSDictionary *output = state[@"output172"];
        NSString *message = output[@"message"] ?: @"等待读取游戏输出尺寸";
        if ([output[@"actualWidth"] intValue] > 0 && [output[@"actualHeight"] intValue] > 0)
            return [NSString stringWithFormat:@"%@ · %@ × %@", message, output[@"actualWidth"], output[@"actualHeight"]];
        return message;
    }
    if ([key isEqual:@"compareLOD"] || [key isEqual:@"compareRendering"])
        return [self.selected[key] boolValue] ? @"⌘G 对比时切换此项" : @"⌘G 对比时保留此项";
    if ([key isEqual:@"characterLighting"] || [key isEqual:@"characterSpecular"]) {
        NSDictionary *character=state[@"character172"];
        NSString *status=character[@"status"];
        if(![character[@"mode"] isEqual:character[@"requestedMode"]])return @"已保存 · 下次启动游戏时生效";
        if([status isEqual:@"active"])return [NSString stringWithFormat:@"已替换 %@ 个角色着色库",character[@"libraries"]];
        if([status isEqual:@"compileFailed"])return character[@"error"] ?: @"角色光照源码未能编译，已沿用游戏着色器";
        return [status isEqual:@"off"] ? @"沿用游戏着色器" : ([status isEqual:@"unavailable"] ? @"缺少 1.7.2 角色光照源码" : @"等待角色出现");
    }
    if ([key isEqual:@"upscaler"]) {
        NSDictionary *fx=state[@"metalFX"];
        if([fx[@"status"] isEqual:@"active"])
            return [NSString stringWithFormat:@"MetalFX 正在编码 · %@ × %@ → %@ × %@",fx[@"sceneSize"][0],fx[@"sceneSize"][1],fx[@"finalSize"][0],fx[@"finalSize"][1]];
        return fx[@"reason"] ?: ([fx[@"status"] isEqual:@"waitingForRender"] ? @"等待游戏提交 MetalFX 编码" : fx[@"status"]) ?: @"等待游戏报告超分状态";
    }
    if ([key isEqual:@"selfShadow"]) {
        NSDictionary *shadow=state[@"nativeSelfShadow"], *pass=state[@"lightingPasses"][@"selfShadow"];
        if(shadow[@"reason"])return shadow[@"reason"];
        if([pass[@"gpuBlocks"] integerValue]>0)return [NSString stringWithFormat:@"GPU 自阴影：%@ 个采样块",pass[@"gpuBlocks"]];
        if(shadow[@"volumeGate"])return [NSString stringWithFormat:@"游戏自阴影开关：%@",[shadow[@"volumeGate"] boolValue] ? @"开" : @"关"];
        return @"等待角色自阴影读回";
    }
    if ([key isEqual:@"backgroundLimit"]) {
        if (state[@"backgroundError"]) return state[@"backgroundError"];
        NSNumber *fps = state[@"backgroundTargetFPS"];
        if (!fps) return @"等待游戏读回";
        if (state[@"backgroundPending"] && [state[@"backgroundPending"] boolValue])
            return [NSString stringWithFormat:@"请求 %@ FPS，游戏当前 %@", state[@"backgroundRequestedFPS"], fps];
        return fps.intValue > 0 ? [NSString stringWithFormat:@"游戏目标帧率：%@ FPS", fps] : @"游戏使用默认目标帧率";
    }
    NSDictionary *features = @{@"shadowResolution":@[@"ShadowMap"], @"lod":@[@"Streaming"], @"vegetationDensity":@[@"Grass"],
                               @"quality":@[@"RenderTargetSettings"],
                               @"sharpen":@[@"TAAU", @"PostProcess"]};
    NSDictionary *names = @{@"CSMTileResolution":@"CSM", @"CharacterShadowMapResolution":@"角色", @"ASMShadowMapTileResolution":@"ASM",
                            @"chunkLoadRadius":@"地块", @"defaultLayerLoadRadius":@"默认层", @"hlod0LayerLoadRadius":@"HLOD",
                            @"tinyLayerLoadRadius":@"小物件", @"GrassGlobalSparsity":@"草地密度", @"1KSharpenStrength":@"1K",
                            @"2KSharpenStrength":@"2K", @"4KSharpenStrength":@"4K", @"SharpenEnabled":@"后处理锐化",
                            @"RenderingScale":@"渲染比例"};
    NSMutableArray *values = [NSMutableArray array];
    for (NSString *feature in features[key]) {
        NSDictionary *current = effective[feature];
        for (NSString *name in [current.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
            id value = current[name];
            if ([name isEqual:@"SharpenEnabled"]) value = [value boolValue] ? @"开" : @"关";
            [values addObject:[NSString stringWithFormat:@"%@ %@", names[name] ?: name, value]];
        }
    }
    return values.count ? [@"游戏读回：" stringByAppendingString:[values componentsJoinedByString:@" · "]] : @"等待游戏读回";
}

- (void)menuWillOpen:(NSMenu *)menu {
    self.status.title = self.runtime[@"error"] ?: self.runtime[@"waiting"] ?: self.runtime[@"settingsResult"] ?:
        (self.comparing ? @"临时原画面 · 已保留画质设置" : @"所选画面 · 选择后保存");
    self.compare.title = self.comparing ? @"返回已选所选画面" : @"临时查看原画面";
    self.compare.state = self.comparing ? NSControlStateValueOn : NSControlStateValueOff;
    for (NSString *key in self.groups) {
        NSMenuItem *group = self.groups[key];
        NSString *choice = nil;
        for (NSMenuItem *item in group.submenu.itemArray) {
            if (!item.representedObject) continue;
            BOOL selected = [self.selected[key] isEqual:item.representedObject[1]];
            item.state = selected ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled = !self.comparing;
            if (selected) choice = item.title;
        }
        group.title = choice ? [NSString stringWithFormat:@"%@ · %@", group.representedObject, choice] : group.representedObject;
        group.submenu.itemArray.lastObject.title = [self readback:key];
    }
}
@end

// The graphics component supplies current readbacks and owns all persistence and comparisons.
static BOOL updateMenu172(NSDictionary *selected, NSDictionary *state, BOOL comparing, void (^perform)(NSString *, id)) {
    static BMFMenu172 *controller;
    if (!controller) controller = [BMFMenu172 new];
    controller.selected = selected;
    controller.runtime = state;
    controller.comparing = comparing;
    controller.perform = perform;
    NSMenu *main = NSApp.mainMenu;
    if (main && controller.entry.menu != main) {
        [controller.entry.menu removeItem:controller.entry];
        [main addItem:controller.entry];
    }
    BOOL attached=main && controller.entry.menu == main && controller.groups[@"shadowResolution"] && controller.groups[@"lod"] &&
        controller.groups[@"vegetationDensity"] && controller.groups[@"sharpen"] && controller.groups[@"backgroundLimit"];
    if(attached)showMenu172Hint();
    return attached;
}
