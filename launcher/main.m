#import <AppKit/AppKit.h>
#import <CommonCrypto/CommonDigest.h>
#include "../src/vegetation_files.inc"

static void fail(NSString *message) {
    @throw [NSException exceptionWithName:@"BetterMacField" reason:message userInfo:nil];
}
static void compatibilityWarning(NSString *summary,NSString *guidance,NSString *diagnostic) {
    @throw [NSException exceptionWithName:@"BMFClientCompatibility"
        reason:[NSString stringWithFormat:@"%@\n%@\n\n排查信息（供反馈）：\n%@",summary,guidance,diagnostic]
        userInfo:@{@"summary":summary,@"guidance":guidance}];
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
        for(NSString *key in desired){
            if(![current[key] isEqual:desired[key]])continue;
            if([saved[@"absent"] containsObject:key])[current removeObjectForKey:key];
            else current[key]=saved[@"original"][key];
        }
    }else{
        if(!saved){
            NSMutableDictionary *original=[NSMutableDictionary dictionary];NSMutableArray *absent=[NSMutableArray array];
            for(NSString *key in desired){if(current[key])original[key]=current[key];else [absent addObject:key];}
            directory(record.stringByDeletingLastPathComponent);
            writeJSON(@{@"path":path,@"original":original,@"absent":absent},record);
        }
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
static NSString *clientPath(NSString *region) {
    NSString *bundle=packageInfo()[@"clients"][region][@"bundleID"];
    return [NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/Containers/io.playcover.PlayCover/Applications/%@.app",bundle]];
}
static NSString *runtimeRoot(NSString *region) {
    return [NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/Containers/%@/Data/tmp/BetterMacField",packageInfo()[@"clients"][region][@"bundleID"]]];
}
static NSString *preferencesPath(NSString *region) {return [stateRoot() stringByAppendingPathComponent:[NSString stringWithFormat:@"settings-%@.json",region]];}
static NSDictionary *catalog(void) {return readJSON([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"features.json"]);}
static NSDictionary *loadSettings(NSString *region) {
    NSMutableDictionary *v=[NSMutableDictionary dictionary];
    for(NSDictionary *field in catalog()[@"fields"])v[field[@"key"]]=field[@"default"];
    [v addEntriesFromDictionary:readJSON(preferencesPath(region)) ?: @{}];return v;
}
static NSString *restoreRecord(NSString *region) {return [stateRoot() stringByAppendingPathComponent:[region isEqual:@"cn"] ? @"playcover-settings-cn.json" : @"playcover-settings.json"];}
static void validatePackage(NSString *region,NSString *appPath,BOOL force) {
    NSDictionary *package=packageInfo(),*client=package[@"clients"][region];
    if(!client)fail(@"启动器缺少客户端配置。");
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[appPath stringByAppendingPathComponent:@"Info.plist"]];
    if(!info)fail(@"没有找到客户端。请先在 PlayCover 安装对应服的 IPA，并启动游戏下载完资料。");
    NSString *diagnostic=[NSString stringWithFormat:@"BetterMacField：%@（%@）\n所选服务器：%@\n支持的游戏版本：%@\n已安装的游戏版本：%@（%@）\n客户端标识：%@\n安装位置：%@",
        [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"未知",
        [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] ?: @"未知",
        [region isEqual:@"cn"] ? @"国服" : @"国际服",client[@"version"],
        info[@"CFBundleShortVersionString"] ?: @"未知",info[@"CFBundleVersion"] ?: @"未知",info[@"CFBundleIdentifier"] ?: @"未知",appPath];
    if(![info[@"CFBundleIdentifier"] isEqual:client[@"bundleID"]])
        fail([NSString stringWithFormat:@"选择的服务器与已安装的游戏不一致。\n请切换到对应服务器，或在 PlayCover 中安装所选服务器的游戏。\n\n排查信息（供反馈）：\n%@",diagnostic]);
    if(!force && ![info[@"CFBundleShortVersionString"] isEqual:client[@"version"]])
        compatibilityWarning(@"游戏版本与当前 BetterMacField 适配的版本不同。",
            [NSString stringWithFormat:@"当前游戏版本为 %@，此工具适配 %@。请按 BetterMacField 下载页的说明，使用相互匹配的工具版本和游戏安装包。\n也可以点击「仍然启动」继续；版本不兼容时可能闪退或出现功能异常。",
                info[@"CFBundleShortVersionString"] ?: @"未知",client[@"version"]],diagnostic);
    NSString *resources=NSBundle.mainBundle.resourcePath;
    if(![sha256([resources stringByAppendingPathComponent:@"libBetterMacField.dylib"]) isEqual:package[@"runtimeSHA256"]])fail(@"BetterMacField 的增强组件已损坏或被修改。\n请重新下载并解压完整的 BetterMacField 安装包，再打开新解压的应用。");
    for(NSString *relative in package[@"payload"])
        if(![sha256([resources stringByAppendingPathComponent:relative]) isEqual:package[@"payload"][relative]])fail([@"BetterMacField 的画质组件已损坏或被修改。\n请重新下载并解压完整的 BetterMacField 安装包，再打开新解压的应用。\n\n问题文件：" stringByAppendingString:relative]);
}
static void stopClient(NSString *region,BOOL restart) {
    NSString *bundle=packageInfo()[@"clients"][region][@"bundleID"];
    NSArray *running=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundle];
    if(running.count && !restart)fail(@"游戏正在运行。请先退出游戏，再从 Launcher 启动以加载当前版本。");
    for(NSRunningApplication *app in running)[app terminate];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:8];
    for(NSRunningApplication *app in running){
        while(!app.terminated && deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        if(!app.terminated)fail(@"游戏未能退出，请手动关闭后重试。");
    }
}
static NSString *prepareRuntime(NSString *region,NSString *appPath,NSDictionary *settings) {
    NSString *runtime=runtimeRoot(region),*resources=NSBundle.mainBundle.resourcePath;directory(runtime);
    NSString *restoreError=vegetation_files_restore(runtime);if(restoreError)fail(restoreError);
    NSMutableDictionary *files=[packageInfo()[@"payload"] mutableCopy];files[@"libBetterMacField.dylib"]=packageInfo()[@"runtimeSHA256"];
    for(NSString *relative in files){
        NSString *name=[relative hasPrefix:@"Graphics/"] ? [relative substringFromIndex:9] : relative;
        NSString *target=[runtime stringByAppendingPathComponent:name];directory(target.stringByDeletingLastPathComponent);
        NSData *data=[NSData dataWithContentsOfFile:[resources stringByAppendingPathComponent:relative]];
        if([relative.pathExtension isEqual:@"json"]){
            NSString *json=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            // JSON-escape the runtime prefix before substituting its value.
            NSData *encoded=[NSJSONSerialization dataWithJSONObject:@[runtime] options:0 error:nil];
            NSString *escaped=[[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding];
            escaped=[escaped substringWithRange:NSMakeRange(2,escaped.length-4)];
            data=[[json stringByReplacingOccurrencesOfString:@"${RUNTIME}" withString:escaped] dataUsingEncoding:NSUTF8StringEncoding];
        }
        NSError *error=nil;if(![data writeToFile:target options:NSDataWritingAtomic error:&error])fail(error.localizedDescription ?: @"无法写入画质组件");
    }
    writeJSON(settings,[runtime stringByAppendingPathComponent:@"bettermacfield-settings.json"]);
    for(NSString *name in @[@"bettermacfield-command.json",@"bettermacfield-status.json"])
        [NSFileManager.defaultManager removeItemAtPath:[runtime stringByAppendingPathComponent:name] error:nil];
    displaySettings(appPath,restoreRecord(region),packageInfo()[@"displaySettings"],NO);
    return runtime;
}
static void launchClient(NSString *region,NSString *appPath,NSString *runtime,NSDictionary *settings,void (^done)(NSError *)) {
    NSString *library=[runtime stringByAppendingPathComponent:@"libBetterMacField.dylib"];
    NSWorkspaceOpenConfiguration *configuration=[NSWorkspaceOpenConfiguration configuration];
    configuration.createsNewApplicationInstance=YES;configuration.activates=YES;
    BOOL hud=[settings[@"metalHUD"] boolValue];configuration.arguments=@[@"-MetalForceHudEnabled",hud ? @"YES" : @"NO"];
    configuration.environment=@{@"DYLD_INSERT_LIBRARIES":library,@"BMF_RUNTIME_DIR":runtime,
        @"MTL_HUD_ENABLED":hud ? @"1" : @"0",@"MTL_HUD_DISABLE_MENU_BAR":@"0",
        @"ENDFIELD_AO_PATCHES_PATH":[runtime stringByAppendingPathComponent:@"endfield-ios-ao-patches.json"],
        @"ENDFIELD_CHARACTER_SKIN_PATH":[runtime stringByAppendingPathComponent:@"endfield-ios-character-skin.json"],
        @"ENDFIELD_OFFICIAL_GTAO_PATH":[runtime stringByAppendingPathComponent:@"endfield-official-gtao"]};
    [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:appPath] configuration:configuration completionHandler:^(NSRunningApplication *app,NSError *error){dispatch_async(dispatch_get_main_queue(),^{done(error);});}];
}
static NSTextField *label(NSString *text,CGFloat size,BOOL bold) {
    NSTextField *v=[NSTextField wrappingLabelWithString:text];v.font=bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];return v;
}
@interface BMFLauncher : NSObject<NSApplicationDelegate,NSWindowDelegate>
@property NSWindow *window;
@property NSPopUpButton *regions;
@property NSMutableDictionary<NSString *,NSControl *> *controls;
@property NSTextField *status;
@property NSTextView *details;
@property NSButton *launchButton;
@property NSButton *applyButton;
@property NSButton *compareButton;
@property NSTimer *timer;
@property BOOL dirty;
@property BOOL busy;
@property NSString *lastSettingsFingerprint;
@property NSString *pendingRequest;
@property NSString *lastError;
@end
@implementation BMFLauncher
- (NSString *)region {return self.regions.indexOfSelectedItem==1 ? @"cn" : @"global";}
- (NSDictionary *)settings {
    NSMutableDictionary *v=[NSMutableDictionary dictionary];
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
}
- (void)changed:(id)sender {self.dirty=YES;self.status.stringValue=@"设置未保存";}
- (void)regionChanged:(id)sender {
    self.lastError=nil;
    [NSUserDefaults.standardUserDefaults setObject:self.region forKey:@"SelectedRegion"];
    [self showSettings:loadSettings(self.region)];self.dirty=NO;self.lastSettingsFingerprint=nil;[self refresh:nil];
}
- (void)error:(NSString *)message {
    self.lastError=message;
    self.status.stringValue=[NSString stringWithFormat:@"%@ 请在「状态与恢复」查看详情。",[message componentsSeparatedByString:@"\n"].firstObject];
    self.details.string=message;
}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame parent:(NSView *)parent {
    NSButton *b=[NSButton buttonWithTitle:title target:self action:action];b.frame=frame;[parent addSubview:b];return b;
}
- (void)experimentalUnavailable:(id)sender {
    NSAlert *alert=[[NSAlert alloc] init];alert.messageText=@"暂未开放";
    [alert addButtonWithTitle:@"知道了"];
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
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
    if(![self confirmUsage]){[NSApp terminate:nil];return;}
    self.controls=[NSMutableDictionary dictionary];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,780,660) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"BetterMacField";self.window.delegate=self;self.window.releasedWhenClosed=NO;
    NSView *root=self.window.contentView;
    NSTextField *title=label(@"BetterMacField",26,YES);title.frame=NSMakeRect(28,604,450,36);[root addSubview:title];
    NSTextField *sub=label(@"终末地 · macOS 增强工具",13,NO);sub.textColor=NSColor.secondaryLabelColor;sub.frame=NSMakeRect(30,577,580,22);[root addSubview:sub];
    self.regions=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(570,604,180,30) pullsDown:NO];[self.regions addItemsWithTitles:@[@"国际服 · 1.6.3",@"国服 · 1.6.3"]];
    self.regions.target=self;self.regions.action=@selector(regionChanged:);[root addSubview:self.regions];
    if(![[NSUserDefaults.standardUserDefaults stringForKey:@"SelectedRegion"] isEqual:@"global"])[self.regions selectItemAtIndex:1];
    NSTabView *tabs=[[NSTabView alloc] initWithFrame:NSMakeRect(24,136,732,432)];[root addSubview:tabs];
    for(NSString *name in @[@"画面",@"光影",@"操作",@"实验性功能",@"状态与恢复"]){
        NSTabViewItem *tab=[[NSTabViewItem alloc] initWithIdentifier:name];tab.label=name;
        NSView *page=[[NSView alloc] initWithFrame:NSMakeRect(0,0,706,388)];tab.view=page;[tabs addTabViewItem:tab];
        if([name isEqual:@"状态与恢复"]){
            NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(16,92,672,278)];scroll.hasVerticalScroller=YES;scroll.borderType=NSBezelBorder;
            self.details=[[NSTextView alloc] initWithFrame:scroll.bounds];self.details.editable=NO;self.details.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];self.details.autoresizingMask=NSViewWidthSizable;scroll.documentView=self.details;[page addSubview:scroll];
            [self button:@"校验客户端与组件" action:@selector(check:) frame:NSMakeRect(16,48,175,30) parent:page];
            [self button:@"复制诊断信息" action:@selector(copyLog:) frame:NSMakeRect(203,48,150,30) parent:page];
            [self button:@"恢复 PlayCover 设置" action:@selector(restore:) frame:NSMakeRect(365,48,190,30) parent:page];
            NSTextField *help=label(@"恢复本工具修改的 PlayCover 设置与场景植被文件。",11,NO);help.frame=NSMakeRect(20,12,650,30);help.textColor=NSColor.secondaryLabelColor;[page addSubview:help];continue;
        }
        CGFloat y=362;
        for(NSDictionary *field in catalog()[@"fields"]){
            if(![field[@"page"] isEqual:name])continue;
            NSString *key=field[@"key"];
            if(field[@"choices"]){
                NSTextField *caption=label(field[@"title"],13,YES);caption.frame=NSMakeRect(18,y-6,180,24);[page addSubview:caption];
                NSPopUpButton *popup=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(224,y-7,452,28) pullsDown:NO];
                for(NSDictionary *choice in field[@"choices"]){[popup addItemWithTitle:choice[@"title"]];popup.lastItem.representedObject=choice[@"value"];}
                popup.target=self;popup.action=@selector(changed:);[page addSubview:popup];self.controls[key]=popup;
            }else{
                NSButton *toggle=[NSButton checkboxWithTitle:field[@"title"] target:self action:@selector(changed:)];toggle.frame=NSMakeRect(18,y-6,650,24);[page addSubview:toggle];self.controls[key]=toggle;
            }
            NSString *text=field[@"help"];if(text.length){NSTextField *help=label(text,11,NO);help.textColor=NSColor.secondaryLabelColor;help.frame=NSMakeRect(20,y-36,660,26);[page addSubview:help];}
            y-=([name isEqual:@"实验性功能"] ? 70 : [name isEqual:@"画面"] ? (text.length ? 60 : 44) : 51);
        }
        if([name isEqual:@"实验性功能"]){
            for(NSString *title in @[@"替换PC版角色高模",@"使用EFMIMOD"]){
                NSButton *button=[self button:title action:@selector(experimentalUnavailable:) frame:NSMakeRect(18,y-6,300,38) parent:page];
                button.attributedTitle=[[NSAttributedString alloc] initWithString:title attributes:@{NSForegroundColorAttributeName:NSColor.disabledControlTextColor}];
                button.toolTip=@"暂未开放";y-=52;
            }
        }

    }
    self.status=label(@"准备就绪",12,NO);self.status.frame=NSMakeRect(30,84,720,40);[root addSubview:self.status];
    self.compareButton=[self button:@"画面对比 · ⌘G" action:@selector(compare:) frame:NSMakeRect(28,30,170,36) parent:root];
    self.applyButton=[self button:@"保存并应用" action:@selector(apply:) frame:NSMakeRect(432,30,145,36) parent:root];
    self.launchButton=[self button:@"启动游戏" action:@selector(launch:) frame:NSMakeRect(590,30,160,36) parent:root];self.launchButton.keyEquivalent=@"\r";
    [self regionChanged:nil];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:3 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];

}
- (void)save {directory(stateRoot());writeJSON(self.settings,preferencesPath(self.region));self.dirty=NO;}
- (void)send:(NSString *)op settings:(NSDictionary *)settings {
    NSString *root=runtimeRoot(self.region);directory(root);
    self.pendingRequest=NSUUID.UUID.UUIDString;
    writeJSON(@{@"id":self.pendingRequest,@"op":op,@"settings":settings ?: @{}},[root stringByAppendingPathComponent:@"bettermacfield-command.json"]);
}
- (void)apply:(id)sender {
    @try{[self save];[self send:@"configure" settings:self.settings];self.status.stringValue=@"设置已保存。Metal HUD 在下次启动生效。";}
    @catch(NSException *e){[self error:e.reason];}
}
- (void)compare:(id)sender {@try{[self send:@"compare" settings:nil];}@catch(NSException *e){[self error:e.reason];}}
- (void)busy:(BOOL)value {
    self.busy=value;self.launchButton.enabled=!value;self.applyButton.enabled=!value;self.regions.enabled=!value;
}
- (void)runCheck:(BOOL)launch force:(BOOL)force {
    if(self.busy)return;
    self.lastError=nil;
    NSString *region=self.region,*path=clientPath(region);NSDictionary *settings=self.settings;
    @try{if(launch)[self save];}@catch(NSException *e){[self error:e.reason];return;}
    [self busy:YES];self.status.stringValue=@"正在校验客户端与画质组件…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSException *failure=nil;NSString *runtime=nil;
        @try {validatePackage(region,path,force);if(launch){stopClient(region,NO);runtime=prepareRuntime(region,path,settings);}}
        @catch(NSException *e){failure=e;}
        dispatch_async(dispatch_get_main_queue(),^{
            if(failure){
                [self busy:NO];[self error:failure.reason];
                if(launch && !force && [failure.name isEqual:@"BMFClientCompatibility"]){
                    NSAlert *alert=[NSAlert new];alert.messageText=failure.userInfo[@"summary"];alert.informativeText=failure.userInfo[@"guidance"];
                    [alert addButtonWithTitle:@"返回检查"];[alert addButtonWithTitle:@"仍然启动"];
                    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
                        if(response==NSAlertSecondButtonReturn)[self runCheck:YES force:YES];
                    }];
                }
                return;
            }
            if(!launch){[self busy:NO];self.status.stringValue=@"客户端与全部画质组件校验通过。";return;}
            self.status.stringValue=@"正在启动游戏；登录进入场景后自动应用设置…";
            launchClient(region,path,runtime,settings,^(NSError *error){[self busy:NO];if(error)[self error:error.localizedDescription];});
        });
    });
}
- (void)launch:(id)sender {[self runCheck:YES force:NO];}
- (void)check:(id)sender {[self runCheck:NO force:NO];}
- (void)restore:(id)sender {
    @try {
        if([NSRunningApplication runningApplicationsWithBundleIdentifier:packageInfo()[@"clients"][self.region][@"bundleID"]].count)fail(@"请先关闭游戏，再恢复 PlayCover 设置。");
        displaySettings(clientPath(self.region),restoreRecord(self.region),packageInfo()[@"displaySettings"],YES);
        NSString *restoreError=vegetation_files_restore(runtimeRoot(self.region));if(restoreError)fail(restoreError);
        self.status.stringValue=@"PlayCover 设置与场景植被文件已恢复。";
    }@catch(NSException *e){[self error:e.reason];}
}
- (void)copyLog:(id)sender {
    NSString *log=[NSString stringWithContentsOfFile:[runtimeRoot(self.region) stringByAppendingPathComponent:@"bettermacfield-runtime.log"] encoding:NSUTF8StringEncoding error:nil];
    NSMutableString *text=[NSMutableString stringWithString:self.details.string ?: @""];
    if(log.length)[text appendFormat:@"\n\n最近一次游戏运行日志：\n%@",log];
    [NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];self.status.stringValue=@"诊断信息已复制，可粘贴给发布者。";
}
- (void)refresh:(id)sender {
    if(self.busy)return;
    NSString *region=self.region,*runtime=runtimeRoot(region),*bundle=packageInfo()[@"clients"][region][@"bundleID"];
    NSArray<NSRunningApplication *> *apps=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundle];
    NSDictionary *state=readJSON([runtime stringByAppendingPathComponent:@"bettermacfield-status.json"]);
    BOOL live=NO;for(NSRunningApplication *app in apps)if(app.processIdentifier==[state[@"pid"] intValue] && NSDate.date.timeIntervalSince1970-[state[@"updatedAt"] doubleValue]<12)live=YES;
    self.compareButton.enabled=live && [state[@"sceneReady"] boolValue];
    self.launchButton.enabled=!apps.count;
    NSString *prefix=apps.count ? (live ? @"增强模块已连接" : @"游戏运行中 · 等待增强模块连接") : @"游戏未运行";
    if(!self.dirty && !self.lastError)self.status.stringValue=[NSString stringWithFormat:@"%@%@",prefix,live ? [@" · " stringByAppendingString:state[@"settingsResult"] ?: @"就绪"] : @""];
    BOOL acknowledged=!self.pendingRequest || ([self.pendingRequest isEqual:state[@"id"]] && ![state[@"settingsPending"] boolValue]);
    if(acknowledged)self.pendingRequest=nil;
    if(live && acknowledged && !self.dirty && [state[@"settings"] isKindOfClass:NSDictionary.class]){
        NSString *fingerprint=[state[@"settings"] description];
        if(![fingerprint isEqual:self.lastSettingsFingerprint]){
            self.lastSettingsFingerprint=fingerprint;[self showSettings:state[@"settings"]];
            @try{directory(stateRoot());writeJSON(state[@"settings"],preferencesPath(region));}@catch(NSException *e){}
        }
    }
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[clientPath(region) stringByAppendingPathComponent:@"Info.plist"]];
    NSMutableString *text=[NSMutableString stringWithFormat:@"%@\n客户端：%@\n已安装版本：%@\n支持版本：%@\n安装位置：%@\n\n",prefix,[region isEqual:@"cn"] ? @"国服" : @"国际服",info[@"CFBundleShortVersionString"] ?: @"未找到",packageInfo()[@"clients"][region][@"version"],clientPath(region)];
    if(live){
        [text appendFormat:@"场景：%@\n画面对比：%@\nGTAO：%@ 帧 · %@ 错误\n接触阴影：%@ 帧 · %@ 错误\n",[state[@"sceneReady"] boolValue] ? @"已进入" : @"等待进入",[state[@"comparisonBaseline"] boolValue] ? @"原生设置" : @"增强设置",state[@"gtaoEncodes"],state[@"gtaoErrors"],state[@"contactEncodes"],state[@"contactErrors"]];
        NSDictionary *fx=state[@"metalFX"],*effective=state[@"effective"];
        NSDictionary *frameRate=state[@"frameRate"];
        if(frameRate)[text appendFormat:@"目标帧率：%@ FPS · 屏幕上限：%@ Hz\n",frameRate[@"target"] ?: @"—",frameRate[@"screenMaximum"] ?: @"—"];
        NSDictionary *vegetation=state[@"sceneVegetation"],*bindings=state[@"keyboardBindings"];
        if(vegetation)[text appendFormat:@"场景植被：%@%@\n",vegetation[@"result"] ?: @"—",[vegetation[@"restartRequired"] boolValue] ? @"（设置已保存，下次启动生效）" : @""];
        if(bindings)[text appendFormat:@"自定义按键页面：%@\n",[bindings[@"tabVisible"] boolValue] ? @"已开放" : @"未开放"];
        [text appendFormat:@"超分：%@\nLOD：%@\nMetalFX：%@ 帧 · %@ 错误\n",effective[@"upscaler"] ?: @"—",effective[@"lod"] ?: @"—",fx[@"encodes"] ?: @0,fx[@"errors"] ?: @0];
        NSNumber *density=effective[@"vegetationDensity"];
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
        BOOL check=[args containsObject:@"--check"],restore=[args containsObject:@"--restore-settings"],launch=[args containsObject:@"--launch"];
        BOOL commandLine=check || restore || launch;
        @try {
            if(!packageInfo() || !catalog())fail(@"启动器文件不完整，请重新解压完整安装包。");
            if(commandLine){
                NSString *region=argument(args,@"--region") ?: @"cn";
                if(!packageInfo()[@"clients"][region])fail(@"未知客户端区域。");
                NSString *path=argument(args,@"--app") ?: clientPath(region);
                if(restore){stopClient(region,NO);displaySettings(path,restoreRecord(region),packageInfo()[@"displaySettings"],YES);NSString *error=vegetation_files_restore(runtimeRoot(region));if(error)fail(error);puts("PlayCover settings and vegetation files restored.");return 0;}
                BOOL force=[args containsObject:@"--force"];
                validatePackage(region,path,force);
                if(force)fprintf(stderr,"已按 --force 继续，忽略游戏版本匹配提示。\n");
                if(check){
                    if(force)puts("Packaged components verified; client compatibility override enabled.");
                    else printf("Verified %s 1.6.3 and all packaged graphics programs.\n",region.UTF8String);
                    return 0;
                }
                stopClient(region,[args containsObject:@"--restart"]);
                NSDictionary *settings=loadSettings(region);NSString *runtime=prepareRuntime(region,path,settings);
                [NSApplication sharedApplication];
                launchClient(region,path,runtime,settings,^(NSError *error){if(error)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);exit(error ? 1 : 0);});[NSApp run];
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
