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

static NSDictionary *menu172Catalog(void) {
    static NSDictionary *catalog;
    if (!catalog) {
        NSData *data = [NSData dataWithContentsOfFile:graphicsResource172(@"features.json")];
        catalog = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    }
    return catalog;
}
static NSDictionary *menu172Field(NSString *key) {
    for (NSDictionary *field in menu172Catalog()[@"fields"])
        if ([field[@"key"] isEqual:key]) return field;
    return nil;
}

@interface BMFMenu172 : NSObject <NSMenuDelegate>
@property(nonatomic, strong) NSMenuItem *entry;
@property(nonatomic, strong) NSMenuItem *status;
@property(nonatomic, strong) NSMenuItem *compare;
@property(nonatomic, strong) NSDictionary<NSString *, NSMenuItem *> *groups;
@property(nonatomic, strong) NSMenuItem *preset;
@property(nonatomic, strong) NSMenu *menu;
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
    _menu = menu;
    _groups = @{};
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

- (NSMenuItem *)header:(NSString *)title in:(NSMenu *)menu {
    NSMenuItem *item;
    if (@available(macOS 14.0, *)) item = [NSMenuItem sectionHeaderWithTitle:title];
    else { item = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""]; item.enabled = NO; }
    [menu addItem:item];
    return item;
}

// One entry per live setting: a checkable item for switches, a submenu of choices otherwise. Readbacks go into the
// tooltip (switches) or the last line of the submenu (choices).
- (void)addField:(NSString *)key to:(NSMenu *)menu into:(NSMutableDictionary *)groups {
    NSDictionary *field = menu172Field(key);
    if (!field || !liveChoices()[key] || !self.selected[key]) return;
    NSString *title = [field[@"restart"] boolValue] ? [field[@"title"] stringByAppendingString:@" · 下次启动"] : field[@"title"];
    NSMenuItem *group;
    if (field[@"choices"]) {
        group = [menu addItemWithTitle:title action:NULL keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        submenu.autoenablesItems = NO;
        group.submenu = submenu;
        for (NSDictionary *choice in field[@"choices"]) {
            NSMenuItem *item = [submenu addItemWithTitle:choice[@"title"] action:@selector(choose:) keyEquivalent:@""];
            item.target = self;
            item.representedObject = @[key, choice[@"value"]];
        }
        [submenu addItem:NSMenuItem.separatorItem];
        [submenu addItemWithTitle:@"等待游戏读回" action:NULL keyEquivalent:@""].enabled = NO;
    } else {
        group = [menu addItemWithTitle:title action:@selector(toggleField:) keyEquivalent:@""];
        group.target = self;
    }
    group.representedObject = @{@"key":key, @"title":title};
    groups[key] = group;
}

// Pages and sections follow features.json "ui172", the same layout as the launcher window.
- (void)rebuild {
    NSMenu *menu = self.menu;
    while (menu.numberOfItems > 2) [menu removeItemAtIndex:2];
    NSMutableDictionary *groups = [NSMutableDictionary dictionary];
    NSDictionary *ui = menu172Catalog()[@"ui172"];
    self.preset = nil;
    for (NSDictionary *page in ui[@"pages"]) {
        BOOL advanced = [page[@"title"] isEqual:@"高级"];
        NSMenu *target = menu;
        [menu addItem:NSMenuItem.separatorItem];
        if (advanced) {
            NSMenuItem *more = [menu addItemWithTitle:@"高级" action:NULL keyEquivalent:@""];
            target = [[NSMenu alloc] initWithTitle:@"高级"]; target.autoenablesItems = NO; more.submenu = target;
        } else [self header:page[@"title"] in:menu];
        if ([page[@"title"] isEqual:@"画质"] && [ui[@"presets"] count]) {
            self.preset = [menu addItemWithTitle:@"画质预设" action:NULL keyEquivalent:@""];
            NSMenu *presets = [[NSMenu alloc] initWithTitle:@"画质预设"]; presets.autoenablesItems = NO;
            for (NSDictionary *preset in ui[@"presets"]) {
                NSMenuItem *item = [presets addItemWithTitle:preset[@"title"] action:@selector(choosePreset:) keyEquivalent:@""];
                item.target = self; item.representedObject = preset[@"values"];
            }
            self.preset.submenu = presets;
        }
        for (NSDictionary *section in page[@"sections"]) {
            if ([section[@"maintenance"] boolValue] || ![section[@"keys"] count]) continue;
            NSMenu *sectionMenu = target;
            if (section[@"menu"]) {
                NSMenuItem *more = [target addItemWithTitle:section[@"menu"] action:NULL keyEquivalent:@""];
                sectionMenu = [[NSMenu alloc] initWithTitle:section[@"menu"]]; sectionMenu.autoenablesItems = NO; more.submenu = sectionMenu;
            } else if (advanced && [section[@"title"] length]) {
                if (target.numberOfItems) [target addItem:NSMenuItem.separatorItem];
                [self header:section[@"title"] in:target];
            }
            for (NSString *key in section[@"keys"]) [self addField:key to:sectionMenu into:groups];
        }
    }
    _groups = [groups copy];
    [menu addItem:NSMenuItem.separatorItem];
    NSMenuItem *popup = [menu addItemWithTitle:@"在游戏窗口呼出菜单" action:@selector(popup:) keyEquivalent:@"p"];
    popup.target = self;
    popup.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
}

- (void)choose:(NSMenuItem *)item {
    if (!self.comparing && self.perform) self.perform(item.representedObject[0], item.representedObject[1]);
}

- (void)toggleField:(NSMenuItem *)item {
    NSString *key = item.representedObject[@"key"];
    if (!self.comparing && self.perform) self.perform(key, @(![self.selected[key] boolValue]));
}

- (void)choosePreset:(NSMenuItem *)item {
    if (!self.comparing && self.perform) self.perform(@"settings", item.representedObject);
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
        (self.comparing ? @"正在看原画面 · 你的设置都还在" : @"改动立即生效并保存");
    self.compare.title = self.comparing ? @"返回增强画面" : @"临时查看原画面";
    self.compare.state = self.comparing ? NSControlStateValueOn : NSControlStateValueOff;
    for (NSString *key in self.groups) {
        NSMenuItem *group = self.groups[key];
        NSString *title = group.representedObject[@"title"];
        if (!group.submenu) {
            group.state = [self.selected[key] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
            group.enabled = !self.comparing;
            group.toolTip = [self readback:key];
            continue;
        }
        NSString *choice = nil;
        for (NSMenuItem *item in group.submenu.itemArray) {
            if (!item.representedObject) continue;
            BOOL selected = [self.selected[key] isEqual:item.representedObject[1]];
            item.state = selected ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled = !self.comparing;
            if (selected) choice = item.title;
        }
        group.title = choice ? [NSString stringWithFormat:@"%@　%@", title, choice] : title;
        group.submenu.itemArray.lastObject.title = [self readback:key];
    }
    NSString *presetName = nil;
    for (NSMenuItem *item in self.preset.submenu.itemArray) {
        BOOL match = YES;
        NSDictionary *values = item.representedObject;
        for (NSString *key in values) if (![self.selected[key] isEqual:values[key]]) match = NO;
        item.state = match ? NSControlStateValueOn : NSControlStateValueOff;
        item.enabled = !self.comparing;
        if (match) presetName = item.title;
    }
    self.preset.title = [NSString stringWithFormat:@"画质预设　%@", presetName ?: @"自定义"];
}
@end

// The graphics component supplies current readbacks and owns all persistence and comparisons.
static BOOL updateMenu172(NSDictionary *selected, NSDictionary *state, BOOL comparing, void (^perform)(NSString *, id)) {
    static BMFMenu172 *controller;
    static NSSet *builtKeys;
    if (!controller) controller = [BMFMenu172 new];
    controller.selected = selected;
    NSSet *keys = [NSSet setWithArray:selected.allKeys];
    if (![keys isEqual:builtKeys]) { [controller rebuild]; builtKeys = keys; }
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
