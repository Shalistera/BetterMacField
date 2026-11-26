#import <AppKit/AppKit.h>
#import <CommonCrypto/CommonDigest.h>

static void fail(NSString *message) {
    @throw [NSException exceptionWithName:@"BetterMacField" reason:message userInfo:nil];
}
static NSDictionary *readJSON(NSString *path) {
    NSData *data=[NSData dataWithContentsOfFile:path];
    id value=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}
static NSString *sha256(NSString *path) {
    NSInputStream *stream=[NSInputStream inputStreamWithFileAtPath:path];[stream open];
    CC_SHA256_CTX context;CC_SHA256_Init(&context);uint8_t buffer[65536];NSInteger size;
    while((size=[stream read:buffer maxLength:sizeof buffer])>0)CC_SHA256_Update(&context,buffer,(CC_LONG)size);
    [stream close];if(size<0)fail([@"无法读取文件：" stringByAppendingString:path]);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(digest,&context);
    NSMutableString *result=[NSMutableString string];for(int i=0;i<sizeof digest;i++)[result appendFormat:@"%02x",digest[i]];
    return result;
}
static void writeJSON(NSDictionary *value,NSString *path) {
    NSError *error=nil;NSData *data=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingPrettyPrinted error:&error];
    if(!data || ![data writeToFile:path options:NSDataWritingAtomic error:&error])fail(error.localizedDescription ?: @"无法保存设置");
}
static void directory(NSString *path) {
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:&error])fail(error.localizedDescription);
}
static void displaySettings(NSString *appPath,NSString *record,NSDictionary *desired,BOOL restore) {
    NSString *path=[[appPath.stringByDeletingLastPathComponent.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"App Settings"]
        stringByAppendingPathComponent:[appPath.lastPathComponent.stringByDeletingPathExtension stringByAppendingString:@".plist"]];
    NSMutableDictionary *current=[[NSDictionary dictionaryWithContentsOfFile:path] mutableCopy] ?: [NSMutableDictionary dictionary];
    NSDictionary *saved=readJSON(record);
    if(saved && ![saved[@"path"] isEqual:path])fail(@"恢复记录属于另一份安装；请先用原路径恢复设置。");
    if(restore){
        if(!saved)return;
        desired=saved[@"desired"] ?: desired;
        for(NSString *key in desired){
            if(![current[key] isEqual:desired[key]])continue;
            if([saved[@"absent"] containsObject:key])[current removeObjectForKey:key];
            else current[key]=saved[@"original"][key];
        }
    }else{
        NSMutableDictionary *original=[saved[@"original"] mutableCopy] ?: [NSMutableDictionary dictionary];
        NSMutableArray *absent=[saved[@"absent"] mutableCopy] ?: [NSMutableArray array];
        for(NSString *key in desired)if(!original[key] && ![absent containsObject:key]){if(current[key])original[key]=current[key];else [absent addObject:key];}
        directory(record.stringByDeletingLastPathComponent);
        writeJSON(@{@"path":path,@"original":original,@"absent":absent,@"desired":desired},record);
        [current addEntriesFromDictionary:desired];
    }
    directory(path.stringByDeletingLastPathComponent);
    if(![current writeToFile:path atomically:YES])fail(@"无法保存 PlayCover 设置，请关闭 PlayCover 设置窗口后重试。");
    if(restore)[NSFileManager.defaultManager removeItemAtPath:record error:nil];
}
static NSString *argument(NSArray *arguments,NSString *key) {
    NSUInteger index=[arguments indexOfObject:key];
    return index!=NSNotFound && index+1<arguments.count ? arguments[index+1] : nil;
}
static NSDictionary *packageInfo(void) {return readJSON([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"clients.json"]);}
static NSString *stateRoot(void) {return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/BetterMacField"];}
#include "region172.inc"
static NSDictionary *catalog(void) {return readJSON([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"features.json"]);}
static NSDictionary *loadSettings(void) {
    NSMutableDictionary *values=[NSMutableDictionary dictionary];NSDictionary *saved=readJSON(preferencesPath());
    for(NSDictionary *field in catalog()[@"fields"])values[field[@"key"]]=saved[field[@"key"]] ?: field[@"default"];
    return values;
}
#include "current172.inc"
static void validatePackage(NSString *appPath) {
    NSDictionary *package=packageInfo();
    validate172(appPath);
    NSDictionary *payload=package[@"payload"];NSString *resources=NSBundle.mainBundle.resourcePath;
    for(NSString *required in @[@"features.json",@"BetterMacField172.dylib",@"BetterMacFieldGraphics.bundle/Contents/Info.plist",@"BetterMacFieldGraphics.bundle/Contents/MacOS/BetterMacFieldGraphics"])
        if(![payload[required] isKindOfClass:NSString.class])fail(@"启动器缺少画质组件校验清单，请重新下载完整安装包。");
    for(NSString *relative in payload)
        if(![sha256([resources stringByAppendingPathComponent:relative]) isEqual:payload[relative]])
            fail([@"BetterMacField 的画质组件已损坏或被修改，请重新下载完整安装包。\n问题文件：" stringByAppendingString:relative]);
}
static void stopClient(BOOL restart) {
    NSArray *running=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID172()];
    if(running.count && !restart)fail(@"请先关闭游戏，再恢复 PlayCover 设置。");
    if(restart)requirePublic172();
    for(NSRunningApplication *app in running)[app terminate];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:8];
    for(NSRunningApplication *app in running){
        while(!app.terminated && deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        if(!app.terminated)fail(@"游戏未能退出，请手动关闭后重试。");
    }
}
static NSTextField *label(NSString *text,CGFloat size,BOOL bold) {
    NSTextField *v=[NSTextField wrappingLabelWithString:text];v.font=bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];return v;
}
static NSTextField *hintLabel(NSString *text,CGFloat x,CGFloat y,CGFloat width,NSView *parent) {
    NSTextField *v=label(text,11,NO);v.textColor=NSColor.secondaryLabelColor;
    v.preferredMaxLayoutWidth=width;NSSize size=v.fittingSize;
    v.frame=NSMakeRect(x,y,width,ceil(size.height));[parent addSubview:v];return v;
}
// Small capsule for settings that only take effect when the game is launched again.
static NSTextField *restartTag(CGFloat x,CGFloat y,NSView *parent) {
    NSTextField *v=[NSTextField labelWithString:@"下次启动"];v.font=[NSFont systemFontOfSize:10];v.textColor=NSColor.secondaryLabelColor;
    v.alignment=NSTextAlignmentCenter;v.wantsLayer=YES;v.layer.cornerRadius=4;v.layer.borderWidth=0.5;
    v.layer.borderColor=NSColor.separatorColor.CGColor;[v sizeToFit];
    v.frame=NSMakeRect(x,y,NSWidth(v.frame)+10,NSHeight(v.frame)+2);[parent addSubview:v];return v;
}
@interface BMFFlippedView : NSView
@end
@implementation BMFFlippedView
- (BOOL)isFlipped {return YES;}
@end
static NSDictionary *layout172(void) {return catalog()[@"ui172"];}
static NSDictionary *field172(NSString *key) {
    for(NSDictionary *field in catalog()[@"fields"])if([field[@"key"] isEqual:key])return field;
    return nil;
}
@interface BMFLauncher : NSObject<NSApplicationDelegate,NSWindowDelegate>
@property NSWindow *window;
@property NSMutableDictionary<NSString *,NSControl *> *controls;
@property NSSegmentedControl *presetControl;
@property NSTextField *presetLabel;
@property NSTabView *pages;
@property NSTextField *status;
@property NSTextView *details;
@property NSButton *launchButton;
@property NSButton *compareButton;
@property NSTimer *timer;
@property BOOL busy;
@property BOOL wantsApply;
@property NSString *flash;
@property NSTimeInterval flashUntil;
@property NSString *lastSettingsFingerprint;
@property NSString *pendingRequest;
@property NSTimeInterval pendingSince;
@property NSString *lastError;
@end
@implementation BMFLauncher
- (NSDictionary *)settings {
    NSMutableDictionary *v=[loadSettings() mutableCopy];
    for(NSString *key in self.controls){
        NSControl *control=self.controls[key];
        if([control isKindOfClass:NSPopUpButton.class])v[key]=((NSPopUpButton *)control).selectedItem.representedObject;
        else v[key]=@(((NSButton *)control).state==NSControlStateValueOn);
    }return v;
}
- (void)showSettings:(NSDictionary *)values {
    for(NSString *key in self.controls){
        NSControl *control=self.controls[key];id value=values[key];
        if([control isKindOfClass:NSPopUpButton.class]){
            NSPopUpButton *popup=(id)control;
            for(NSMenuItem *item in popup.itemArray)if([item.representedObject isEqual:value])[popup selectItem:item];
        }else ((NSButton *)control).state=[value boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
    }
    [self updatePresetDisplay];
}
- (NSInteger)matchingPreset:(NSDictionary *)values {
    NSArray *presets=layout172()[@"presets"];
    for(NSUInteger i=0;i<presets.count;i++){
        BOOL all=YES;NSDictionary *preset=presets[i][@"values"];
        for(NSString *key in preset)if(![values[key] isEqual:preset[key]])all=NO;
        if(all)return (NSInteger)i;
    }
    return -1;
}
- (void)updatePresetDisplay {
    if(!self.presetControl)return;
    NSInteger match=[self matchingPreset:self.settings];
    for(NSInteger i=0;i<self.presetControl.segmentCount;i++)[self.presetControl setSelected:i==match forSegment:i];
    self.presetLabel.stringValue=match<0 ? @"自定义" : @"";
}
- (void)presetChosen:(NSSegmentedControl *)control {
    NSArray *presets=layout172()[@"presets"];
    if(control.selectedSegment<0 || control.selectedSegment>=(NSInteger)presets.count)return;
    NSMutableDictionary *values=[self.settings mutableCopy];
    [values addEntriesFromDictionary:presets[control.selectedSegment][@"values"]];
    [self showSettings:values];[self changed:nil];
}
- (void)pageChosen:(NSSegmentedControl *)control {[self.pages selectTabViewItemAtIndex:control.selectedSegment];}
- (void)showFlash:(NSString *)text {
    self.flash=text;self.flashUntil=NSProcessInfo.processInfo.systemUptime+6;
    self.status.stringValue=text;self.status.textColor=NSColor.secondaryLabelColor;
}
// Every change is saved at once and, when the game is connected, sent to it; restart-only settings say so.
- (void)changed:(id)sender {
    self.lastError=nil;[self updatePresetDisplay];
    @try{[self save];}@catch(NSException *e){[self error:e.reason];return;}
    NSDictionary *field=field172(sender ? [self.controls allKeysForObject:sender].firstObject : nil);
    self.wantsApply=YES;[self flushApply];
    if([field[@"restart"] boolValue])[self showFlash:[NSString stringWithFormat:@"已保存 ·「%@」下次启动游戏时生效",field[@"title"]]];
}
- (void)flushApply {
    if(!self.wantsApply)return;
    if(!live172(state172())){self.wantsApply=NO;[self showFlash:@"已保存 · 启动游戏后生效"];return;}
    if(self.pendingRequest){[self showFlash:@"已保存 · 正在应用到游戏…"];return;}
    @try{[self send:@"configure" settings:self.settings];self.wantsApply=NO;[self showFlash:@"已保存 · 正在应用到游戏…"];}
    @catch(NSException *e){self.wantsApply=NO;[self error:e.reason];}
}
- (void)resetDefaults:(id)sender {
    NSAlert *alert=[NSAlert new];alert.messageText=@"恢复默认设置？";
    alert.informativeText=@"画质、角色光影和操作恢复为推荐值；分辨率和键鼠操作保持不变。";
    [alert addButtonWithTitle:@"恢复默认"];[alert addButtonWithTitle:@"取消"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn)return;
        NSMutableDictionary *values=[self.settings mutableCopy];NSArray *keep=layout172()[@"keepOnReset"] ?: @[];
        for(NSDictionary *f in catalog()[@"fields"])if(![keep containsObject:f[@"key"]])values[f[@"key"]]=f[@"default"];
        [self showSettings:values];[self changed:nil];
    }];
}
- (NSString *)summary:(NSString *)state {
    NSMutableArray *parts=[NSMutableArray array];
    NSInteger preset=[self matchingPreset:self.settings];
    if([layout172()[@"presets"] count])[parts addObject:preset<0 ? @"自定义画质" : [layout172()[@"presets"][preset][@"title"] stringByAppendingString:@"画质"]];
    for(NSString *key in @[@"height",@"upscaler"]){
        NSPopUpButton *popup=(id)self.controls[key];
        if([popup isKindOfClass:NSPopUpButton.class] && popup.selectedItem)[parts addObject:[popup.selectedItem.title componentsSeparatedByString:@" · "].firstObject];
    }
    return [NSString stringWithFormat:@"%@\n%@",state,[parts componentsJoinedByString:@" · "]];
}
- (void)error:(NSString *)message {
    self.lastError=message;self.flash=nil;
    self.status.stringValue=[NSString stringWithFormat:@"⚠︎ %@\n详情见「高级」页。",[message componentsSeparatedByString:@"\n"].firstObject];
    self.status.textColor=NSColor.systemOrangeColor;
    self.details.string=message;
}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame parent:(NSView *)parent {
    NSButton *b=[NSButton buttonWithTitle:title target:self action:action];b.frame=frame;[parent addSubview:b];return b;
}
- (BOOL)confirmUsage {
    if([NSUserDefaults.standardUserDefaults boolForKey:@"SkipInjectionRiskNotice"])return YES;
    NSAlert *alert=[[NSAlert alloc] init];alert.alertStyle=NSAlertStyleWarning;
    alert.messageText=@"使用前请阅读";
    alert.informativeText=@"BetterMacField 在 PlayCover 基础上额外注入增强模块，可能增加账号被封禁的风险。请自行评估后决定是否使用。";
    NSButton *accept=[alert addButtonWithTitle:@"使用（10 秒）"];accept.enabled=NO;
    [alert addButtonWithTitle:@"不使用"].keyEquivalent=@"\e";
    alert.showsSuppressionButton=YES;alert.suppressionButton.title=@"下次不提示";
    NSTimeInterval deadline=NSProcessInfo.processInfo.systemUptime+10.0;
    NSTimer *timer=[NSTimer timerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *tick){
        NSInteger remaining=(NSInteger)ceil(MAX(0.0,deadline-NSProcessInfo.processInfo.systemUptime));
        accept.title=remaining ? [NSString stringWithFormat:@"使用（%ld 秒）",(long)remaining] : @"使用";
        accept.enabled=(remaining==0);
    }];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSModalPanelRunLoopMode];
    [NSApp activateIgnoringOtherApps:YES];
    NSModalResponse response=[alert runModal];[timer invalidate];
    if(response!=NSAlertFirstButtonReturn)return NO;
    if(alert.suppressionButton.state==NSControlStateValueOn)
        [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"SkipInjectionRiskNotice"];
    return YES;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    if(![self confirmUsage] || !selectRegion172(NSProcessInfo.processInfo.arguments,YES)){[NSApp terminate:nil];return;}
    [self buildWindow];
    [self showSettings:loadSettings()];[self refresh:nil];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:3 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
}
// Pages, sections and their order come from features.json "ui172" so the in-game menu matches.
- (void)buildWindow {
    self.controls=[NSMutableDictionary dictionary];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,720,640) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"BetterMacField";self.window.delegate=self;self.window.releasedWhenClosed=NO;
    NSView *root=self.window.contentView;
    NSTextField *title=label(@"BetterMacField",22,YES);title.frame=NSMakeRect(28,598,450,30);[root addSubview:title];
    NSTextField *sub=label([regionName172() stringByAppendingString:@" 1.7.2 · 游戏里按 ⌘⌥P 打开菜单，⌘G 对比原画"],12,NO);sub.textColor=NSColor.secondaryLabelColor;sub.frame=NSMakeRect(30,576,660,18);[root addSubview:sub];
    NSTabView *tabs=[[NSTabView alloc] initWithFrame:NSMakeRect(20,78,680,452)];tabs.tabViewType=NSNoTabsNoBorder;[root addSubview:tabs];self.pages=tabs;
    NSBox *top=[[NSBox alloc] initWithFrame:NSMakeRect(20,531,680,1)];top.boxType=NSBoxSeparator;[root addSubview:top];
    NSBox *bottom=[[NSBox alloc] initWithFrame:NSMakeRect(20,76,680,1)];bottom.boxType=NSBoxSeparator;[root addSubview:bottom];
    NSDictionary *ui=layout172();
    NSMutableArray *pageNames=[NSMutableArray array];for(NSDictionary *page in ui[@"pages"])[pageNames addObject:page[@"title"]];
    NSSegmentedControl *switcher=[NSSegmentedControl segmentedControlWithLabels:pageNames trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(pageChosen:)];
    for(NSInteger i=0;i<switcher.segmentCount;i++)[switcher setWidth:86 forSegment:i];
    [switcher sizeToFit];switcher.frame=NSMakeRect(round(360-NSWidth(switcher.frame)/2),540,NSWidth(switcher.frame),24);
    switcher.selectedSegment=0;[root addSubview:switcher];
    const CGFloat width=640,column=176;
    for(NSDictionary *page in ui[@"pages"]){
        NSTabViewItem *tab=[[NSTabViewItem alloc] initWithIdentifier:page[@"title"]];tab.label=page[@"title"];
        NSView *container=[[NSView alloc] initWithFrame:NSMakeRect(0,0,680,452)];tab.view=container;[tabs addTabViewItem:tab];
        NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:container.bounds];
        scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;scroll.hasVerticalScroller=YES;scroll.autohidesScrollers=YES;
        scroll.drawsBackground=NO;scroll.borderType=NSNoBorder;
        BMFFlippedView *view=[[BMFFlippedView alloc] initWithFrame:NSMakeRect(0,0,width,400)];
        CGFloat y=14;
        if([page[@"title"] isEqual:@"画质"] && [ui[@"presets"] count]){
            NSTextField *caption=label(@"画质预设",13,NO);caption.frame=NSMakeRect(20,y+3,150,20);[view addSubview:caption];
            NSMutableArray *names=[NSMutableArray array];for(NSDictionary *preset in ui[@"presets"])[names addObject:preset[@"title"]];
            self.presetControl=[NSSegmentedControl segmentedControlWithLabels:names trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(presetChosen:)];
            self.presetControl.frame=NSMakeRect(column,y,270,26);[view addSubview:self.presetControl];
            self.presetLabel=label(@"",11,NO);self.presetLabel.textColor=NSColor.secondaryLabelColor;self.presetLabel.frame=NSMakeRect(column+282,y+5,80,16);[view addSubview:self.presetLabel];
            y+=32;y+=NSHeight(hintLabel(@"一次调好阴影、远景、草丛、环境光和反射；分辨率和超分在下面单独选。",column,y,width-column-24,view).frame)+18;
        }
        for(NSDictionary *section in page[@"sections"]){
            if([section[@"title"] length]){
                NSTextField *header=label(section[@"title"],12,YES);header.textColor=NSColor.secondaryLabelColor;
                header.frame=NSMakeRect(20,y,width-40,16);[view addSubview:header];
                NSBox *line=[[NSBox alloc] initWithFrame:NSMakeRect(20,y+19,width-44,1)];line.boxType=NSBoxSeparator;[view addSubview:line];
                y+=30;
            }
            if([section[@"maintenance"] boolValue]){
                CGFloat x=16;
                for(NSArray *entry in @[@[@"校验客户端",NSStringFromSelector(@selector(check:))],@[@"复制诊断信息",NSStringFromSelector(@selector(copyLog:))],
                                        @[@"恢复 PlayCover 设置",NSStringFromSelector(@selector(restore:))],@[@"恢复默认设置…",NSStringFromSelector(@selector(resetDefaults:))]]){
                    NSButton *b=[self button:entry[0] action:NSSelectorFromString(entry[1]) frame:NSZeroRect parent:view];[b sizeToFit];
                    b.frame=NSMakeRect(x,y,NSWidth(b.frame)+12,30);x=NSMaxX(b.frame)+8;
                }
                y+=38;
                y+=NSHeight(hintLabel(@"「恢复 PlayCover 设置」撤销本工具对 PlayCover 的改动，保留之后手动修改的值。遇到问题时复制下面的诊断信息给发布者。",20,y,width-44,view).frame)+8;
                NSScrollView *details=[[NSScrollView alloc] initWithFrame:NSMakeRect(20,y,width-44,230)];details.hasVerticalScroller=YES;details.borderType=NSBezelBorder;
                self.details=[[NSTextView alloc] initWithFrame:details.bounds];self.details.editable=NO;
                self.details.font=[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];self.details.autoresizingMask=NSViewWidthSizable;
                details.documentView=self.details;[view addSubview:details];y+=240;
            }
            for(NSString *key in section[@"keys"]){
                NSDictionary *field=field172(key);if(!field)continue;
                NSString *help=field[@"help"];BOOL restart=[field[@"restart"] boolValue];
                if(field[@"choices"]){
                    NSTextField *caption=label(field[@"title"],13,NO);caption.frame=NSMakeRect(20,y+3,column-24,20);[view addSubview:caption];
                    NSPopUpButton *popup=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(column-3,y,250,26) pullsDown:NO];
                    for(NSDictionary *choice in field[@"choices"]){[popup addItemWithTitle:choice[@"title"]];popup.lastItem.representedObject=choice[@"value"];}
                    popup.target=self;popup.action=@selector(changed:);[view addSubview:popup];self.controls[key]=popup;
                    if(restart)restartTag(NSMaxX(popup.frame)+8,y+5,view);
                    y+=30;
                    if(help.length)y+=NSHeight(hintLabel(help,column,y,width-column-24,view).frame);
                    y+=16;
                }else{
                    NSButton *toggle=[NSButton checkboxWithTitle:field[@"title"] target:self action:@selector(changed:)];
                    [toggle sizeToFit];toggle.frame=NSMakeRect(18,y,NSWidth(toggle.frame),22);[view addSubview:toggle];self.controls[key]=toggle;
                    if(restart)restartTag(NSMaxX(toggle.frame)+8,y+3,view);
                    y+=23;
                    if(help.length)y+=NSHeight(hintLabel(help,38,y,width-62,view).frame);
                    y+=14;
                }
            }
            y+=8;
        }
        view.frame=NSMakeRect(0,0,width,MAX(y+8,scroll.contentSize.height));
        scroll.documentView=view;[container addSubview:scroll];
    }
    self.status=label(@"",12,NO);self.status.textColor=NSColor.secondaryLabelColor;self.status.frame=NSMakeRect(28,22,420,40);[root addSubview:self.status];
    self.compareButton=[self button:@"对比原画 ⌘G" action:@selector(compare:) frame:NSMakeRect(448,28,140,32) parent:root];self.compareButton.hidden=YES;
    self.launchButton=[self button:@"开始游戏" action:@selector(launch:) frame:NSMakeRect(592,24,108,40) parent:root];
    self.launchButton.controlSize=NSControlSizeLarge;self.launchButton.keyEquivalent=@"\r";
    self.launchButton.bezelColor=NSColor.controlAccentColor;self.launchButton.font=[NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    self.window.initialFirstResponder=self.launchButton;
}
- (void)save {directory(stateRoot());writeJSON(self.settings,preferencesPath());}
- (void)send:(NSString *)op settings:(NSDictionary *)settings {
    NSDictionary *state=state172();if(!live172(state))fail(@"图形组件尚未连接，请先启动游戏。");
    if(self.pendingRequest)fail(@"上一项设置仍在应用中。");
    NSString *request=NSUUID.UUID.UUIDString;
    NSMutableDictionary *message=[@{@"id":request,@"session":state[@"session"],@"op":op} mutableCopy];
    if([op isEqual:@"compare"])message[@"enabled"]=@(![state[@"comparisonBaseline"] boolValue]);
    else{
        NSMutableDictionary *live=[NSMutableDictionary dictionary];
        for(NSDictionary *field in catalog()[@"fields"]){NSString *key=field[@"key"];if(![key isEqual:@"metalHUD"] && settings[key])live[key]=settings[key];}
        message[@"settings"]=live;
    }
    writeJSON(message,[runtimeRoot() stringByAppendingPathComponent:@"graphics172-command.json"]);
    self.pendingRequest=request;self.pendingSince=NSProcessInfo.processInfo.systemUptime;
}
- (void)compare:(id)sender {@try{[self send:@"compare" settings:nil];}@catch(NSException *e){[self error:e.reason];}}
- (void)busy:(BOOL)value {self.busy=value;self.launchButton.enabled=!value;}
- (void)runCheck:(BOOL)launch {
    if(self.busy)return;
    self.lastError=nil;self.flash=nil;NSString *path=clientPath();NSDictionary *settings=self.settings;
    @try{if(launch)[self save];}@catch(NSException *e){[self error:e.reason];return;}
    [self busy:YES];self.status.textColor=NSColor.secondaryLabelColor;self.status.stringValue=@"正在校验客户端与画质组件…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSException *failure=nil;
        @try {validatePackage(path);if(launch)prepare172(path,settings);}
        @catch(NSException *e){failure=e;}
        dispatch_async(dispatch_get_main_queue(),^{
            if(failure){[self busy:NO];[self error:failure.reason];return;}
            if(!launch){[self busy:NO];[self showFlash:@"客户端与全部画质组件校验通过"];return;}
            self.status.stringValue=@"正在启动游戏；登录进入场景后自动应用设置…";
            launch172(settings,^(NSError *error){[self busy:NO];if(error)[self error:error.localizedDescription];});
        });
    });
}
- (void)launch:(id)sender {
    NSArray<NSRunningApplication *> *apps=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID172()];
    if(apps.count && running172() && live172(state172())){[apps.firstObject activateWithOptions:NSApplicationActivateAllWindows];return;}
    [self runCheck:YES];
}
- (void)check:(id)sender {[self runCheck:NO];}
- (void)restore:(id)sender {
    @try {
        stopClient(NO);displaySettings(clientPath(),restoreRecord(),packageInfo()[@"displaySettings"],YES);
        [self showFlash:@"PlayCover 设置已恢复"];
    }@catch(NSException *e){[self error:e.reason];}
}
- (void)copyLog:(id)sender {
    NSMutableString *text=[NSMutableString stringWithString:self.details.string ?: @""];
    NSData *data=[NSJSONSerialization dataWithJSONObject:state172() ?: @{} options:NSJSONWritingPrettyPrinted error:nil];
    [text appendFormat:@"\n\n图形组件状态：\n%@",[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
    [NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];[self showFlash:@"诊断信息已复制，可粘贴给发布者"];
}
- (void)refresh:(id)sender {
    if(self.busy)return;
    if(self.flash && NSProcessInfo.processInfo.systemUptime>self.flashUntil)self.flash=nil;
    NSArray<NSRunningApplication *> *apps=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID172()];
    NSDictionary *state=state172();BOOL live=live172(state),scene=live && [state[@"sceneReady"] boolValue],other=apps.count && !running172();
    BOOL comparing=[state[@"comparisonBaseline"] boolValue];
    self.compareButton.hidden=!live;self.compareButton.enabled=scene && !self.pendingRequest;
    self.compareButton.title=comparing ? @"恢复增强画面 ⌘G" : @"对比原画 ⌘G";
    self.launchButton.enabled=!other;
    self.launchButton.title=!apps.count ? @"开始游戏" : (other ? @"开始游戏" : (live ? @"回到游戏" : @"连接游戏"));
    NSString *prefix=!apps.count ? @"游戏未启动" : (other ? @"另一份同服游戏正在运行，请先退出" : (!live ? @"游戏运行中 · 等待增强模块连接" :
        (!scene ? @"已连接游戏 · 进入大世界后生效" : (comparing ? @"正在对比原画 · 再按 ⌘G 返回" : @"游戏中 · 设置改动会立即生效"))));
    BOOL acknowledged=!self.pendingRequest || ([self.pendingRequest isEqual:state[@"id"]] && ![state[@"settingsPending"] boolValue]);
    if(acknowledged && self.pendingRequest && [state[@"commandError"] isKindOfClass:NSString.class])[self error:state[@"commandError"]];
    else if(acknowledged && self.pendingRequest && !self.wantsApply)[self showFlash:state[@"settingsResult"] ?: @"游戏已应用"];
    if(self.pendingRequest && NSProcessInfo.processInfo.systemUptime-self.pendingSince>15){self.pendingRequest=nil;[self error:@"游戏未及时确认设置，请进入可玩场景后重试。"];}
    if(acknowledged)self.pendingRequest=nil;
    if(!self.pendingRequest && self.wantsApply)[self flushApply];
    if(live && acknowledged && !self.wantsApply && [state[@"settings"] isKindOfClass:NSDictionary.class]){
        NSString *fingerprint=[state[@"settings"] description];
        if(![fingerprint isEqual:self.lastSettingsFingerprint]){
            NSMutableDictionary *values=[loadSettings() mutableCopy];[values addEntriesFromDictionary:state[@"settings"]];
            self.lastSettingsFingerprint=fingerprint;[self showSettings:values];
            @try{directory(stateRoot());writeJSON(values,preferencesPath());}@catch(NSException *e){}
        }
    }
    if(!self.lastError && !self.flash){
        self.status.stringValue=[self summary:other ? [@"⚠︎ " stringByAppendingString:prefix] : prefix];
        self.status.textColor=other ? NSColor.systemOrangeColor : NSColor.secondaryLabelColor;
    }
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[clientPath() stringByAppendingPathComponent:@"Info.plist"]];
    NSMutableString *text=[NSMutableString stringWithFormat:@"%@\n客户端：%@\n已安装版本：%@\n支持版本：1.7.2\n安装位置：%@\n公开版副本：%@\n%@\n\n",prefix,regionName172(),info[@"CFBundleShortVersionString"] ?: @"未找到",clientPath(),clone172(),state[@"waiting"] ?: state[@"error"] ?: @""];
    if(live){
        NSDictionary *effects=state[@"sceneLighting172"];
        [text appendFormat:@"场景：%@\n画面对比：%@\nGTAO：%@ 帧 · %@ 错误\n接触阴影：%@ 帧 · %@ 错误\n",[state[@"sceneReady"] boolValue] ? @"已进入" : @"等待进入",comparing ? @"原生设置" : @"增强设置",effects[@"gtaoEncodes"] ?: @0,effects[@"gtaoErrors"] ?: @0,effects[@"contactEncodes"] ?: @0,effects[@"contactErrors"] ?: @0];
        NSDictionary *fx=state[@"metalFX"],*effective=state[@"effective"];
        NSString *upscaler=fx[@"requestedMode"],*lod=state[@"settings"][@"lod"];
        NSNumber *errors=fx[@"native"][@"errors"];
        [text appendFormat:@"超分：%@\nLOD：%@\nMetalFX：%@ 帧 · %@ 错误\n",upscaler ?: @"—",lod ?: @"—",fx[@"encodes"] ?: @0,errors ?: @0];
        {NSDictionary *output=state[@"output172"];[text appendFormat:@"实际输出：%@ × %@\n%@\n",output[@"actualWidth"] ?: @"—",output[@"actualHeight"] ?: @"—",output[@"message"] ?: @""];}
        NSNumber *density=effective[@"Grass"][@"GrassGlobalSparsity"];
        if([density isKindOfClass:NSNumber.class] && density.doubleValue>=0)
            [text appendFormat:@"植被密度：%.0f%%\n",density.doubleValue*100];
    }
    self.details.string=self.lastError ? [NSString stringWithFormat:@"%@\n\n%@",self.lastError,text] : text;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {return YES;}
@end
int main(int argc,const char **argv) {
    @autoreleasepool {
        NSArray *args=NSProcessInfo.processInfo.arguments;
        BOOL check=[args containsObject:@"--check"],restore=[args containsObject:@"--restore-settings"],launch=[args containsObject:@"--launch"],status=[args containsObject:@"--status"];
        BOOL commandLine=check || restore || launch || status;
        @try {
            if(!packageInfo() || !catalog())fail(@"启动器文件不完整，请重新解压完整安装包。");
            if(commandLine){
                selectRegion172(args,NO);
                if([args containsObject:@"--force"])fail(@"需要匹配已适配的 1.7.2 版本。 / --force is not supported.");
                NSString *path=argument(args,@"--app") ?: clientPath();
                if(status){
                    NSDictionary *state=state172();if(!live172(state))state=nil;
                    NSData *data=[NSJSONSerialization dataWithJSONObject:state ?: @{} options:NSJSONWritingPrettyPrinted error:nil];
                    fwrite(data.bytes,1,data.length,stdout);puts("");return 0;
                }
                if(restore){if(![path.stringByStandardizingPath isEqual:clientPath().stringByStandardizingPath])fail(@"只能恢复所选区域的 PlayCover 设置。 / Restore requires the selected installation.");stopClient(NO);displaySettings(path,restoreRecord(),packageInfo()[@"displaySettings"],YES);puts("PlayCover settings restored.");return 0;}
                validatePackage(path);
                if(check){printf("Verified %s 1.7.2 and all packaged graphics programs.\n",selectedRegion172.UTF8String);return 0;}
                requirePublic172();if([args containsObject:@"--restart"])stopClient(YES);
                NSDictionary *settings=loadSettings();prepare172(path,settings);
                [NSApplication sharedApplication];
                launch172(settings,^(NSError *error){if(error)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);exit(error ? 1 : 0);});[NSApp run];
            }else{
                [NSApplication sharedApplication];
                [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
                NSMenu *main=[NSMenu new],*app=[NSMenu new];NSMenuItem *root=[NSMenuItem new];root.submenu=app;[main addItem:root];
                [app addItemWithTitle:@"退出 BetterMacField" action:@selector(terminate:) keyEquivalent:@"q"];NSApp.mainMenu=main;
                BMFLauncher *controller=[BMFLauncher new];NSApp.delegate=controller;[NSApp run];
            }
        }@catch(NSException *e){
            if(commandLine)fprintf(stderr,"%s\n",e.reason.UTF8String);
            else{NSAlert *a=[NSAlert new];a.messageText=@"BetterMacField";a.informativeText=e.reason;[a runModal];}return 1;
        }
    }return 0;
}
