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
static void validatePackage(NSString *region,NSString *appPath) {
    NSDictionary *package=packageInfo(),*client=package[@"clients"][region];
    if(!client)fail(@"启动器缺少客户端配置。");
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[appPath stringByAppendingPathComponent:@"Info.plist"]];
    if(!info)fail(@"没有找到客户端。请先在 PlayCover 安装对应服的 IPA，并启动游戏下载完资料。");
    if(![info[@"CFBundleIdentifier"] isEqual:client[@"bundleID"]] || ![info[@"CFBundleShortVersionString"] isEqual:client[@"version"]])
        fail(@"客户端版本不匹配。此包支持 iOS 1.6.3 的指定构建，请使用对应版本的 BetterMacField。");
    for(NSString *relative in client[@"files"])
        if(![sha256([appPath stringByAppendingPathComponent:relative]) isEqual:client[@"files"][relative]])
            fail([@"客户端文件已改变，需要重新适配：" stringByAppendingString:relative]);
    NSString *resources=NSBundle.mainBundle.resourcePath;
    if(![sha256([resources stringByAppendingPathComponent:@"libBetterMacField.dylib"]) isEqual:package[@"runtimeSHA256"]])fail(@"增强模块校验失败，请重新获取完整安装包。");
    for(NSString *relative in package[@"payload"])
        if(![sha256([resources stringByAppendingPathComponent:relative]) isEqual:package[@"payload"][relative]])fail([@"画质组件校验失败：" stringByAppendingString:relative]);
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
- (void)changed:(id)sender {self.dirty=YES;self.status.stringValue=@"设置待保存 · 点击“保存并应用”；启动游戏也会保存当前设置。";}
- (void)regionChanged:(id)sender {
    [NSUserDefaults.standardUserDefaults setObject:self.region forKey:@"SelectedRegion"];
    [self showSettings:loadSettings(self.region)];self.dirty=NO;self.lastSettingsFingerprint=nil;[self refresh:nil];
}
- (void)error:(NSString *)message {self.status.stringValue=message;self.details.string=message;}
- (NSButton *)button:(NSString *)title action:(SEL)action frame:(NSRect)frame parent:(NSView *)parent {
    NSButton *b=[NSButton buttonWithTitle:title target:self action:action];b.frame=frame;[parent addSubview:b];return b;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.controls=[NSMutableDictionary dictionary];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,780,660) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"BetterMacField · 画质与操作";self.window.delegate=self;self.window.releasedWhenClosed=NO;
    NSView *root=self.window.contentView;
    NSTextField *title=label(@"BetterMacField",26,YES);title.frame=NSMakeRect(28,604,450,36);[root addSubview:title];
    NSTextField *sub=label(@"PlayCover 上的终末地 · 画质、键鼠与实时光影对比",13,NO);sub.textColor=NSColor.secondaryLabelColor;sub.frame=NSMakeRect(30,577,580,22);[root addSubview:sub];
    self.regions=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(570,604,180,30) pullsDown:NO];[self.regions addItemsWithTitles:@[@"国际服 · 1.6.3",@"国服 · 1.6.3"]];
    self.regions.target=self;self.regions.action=@selector(regionChanged:);[root addSubview:self.regions];
    if([[NSUserDefaults.standardUserDefaults stringForKey:@"SelectedRegion"] isEqual:@"cn"])[self.regions selectItemAtIndex:1];
    NSTabView *tabs=[[NSTabView alloc] initWithFrame:NSMakeRect(24,136,732,432)];[root addSubview:tabs];
    for(NSString *name in @[@"画面",@"光影",@"操作",@"状态与恢复"]){
        NSTabViewItem *tab=[[NSTabViewItem alloc] initWithIdentifier:name];tab.label=name;
        NSView *page=[[NSView alloc] initWithFrame:NSMakeRect(0,0,706,388)];tab.view=page;[tabs addTabViewItem:tab];
        if([name isEqual:@"状态与恢复"]){
            NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(16,92,672,278)];scroll.hasVerticalScroller=YES;scroll.borderType=NSBezelBorder;
            self.details=[[NSTextView alloc] initWithFrame:scroll.bounds];self.details.editable=NO;self.details.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];self.details.autoresizingMask=NSViewWidthSizable;scroll.documentView=self.details;[page addSubview:scroll];
            [self button:@"校验客户端与组件" action:@selector(check:) frame:NSMakeRect(16,48,175,30) parent:page];
            [self button:@"复制运行日志" action:@selector(copyLog:) frame:NSMakeRect(203,48,150,30) parent:page];
            [self button:@"恢复 PlayCover 设置" action:@selector(restore:) frame:NSMakeRect(365,48,190,30) parent:page];
            NSTextField *help=label(@"直接从 PlayCover 启动即可使用原版；恢复按钮仅还原 Launcher 修改的 PlayCover 设置。",11,NO);help.frame=NSMakeRect(20,12,650,30);help.textColor=NSColor.secondaryLabelColor;[page addSubview:help];continue;
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
            y-=([name isEqual:@"光影"] ? 51 : 72);
        }
        if([name isEqual:@"操作"]){
            NSTextField *help=label(@"视频对比：F11 或 ⌘⇧G 临时切换原生／增强光影，再按即可恢复。\nSDK 登录和内置浏览器保留触摸兼容；按键与鼠标进入场景后自动接管。",13,NO);
            help.frame=NSMakeRect(20,42,660,70);[page addSubview:help];
        }
    }
    self.status=label(@"准备就绪",12,NO);self.status.frame=NSMakeRect(30,84,720,40);[root addSubview:self.status];
    self.compareButton=[self button:@"光影对比 · F11" action:@selector(compare:) frame:NSMakeRect(28,30,170,36) parent:root];
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
    @try{[self save];[self send:@"configure" settings:self.settings];self.status.stringValue=@"设置已保存；已运行的增强版会在进入场景后应用。Metal HUD 显示选项在下次启动生效。";}
    @catch(NSException *e){[self error:e.reason];}
}
- (void)compare:(id)sender {@try{[self send:@"compare" settings:nil];}@catch(NSException *e){[self error:e.reason];}}
- (void)busy:(BOOL)value {
    self.busy=value;self.launchButton.enabled=!value;self.applyButton.enabled=!value;self.regions.enabled=!value;
}
- (void)runCheck:(BOOL)launch {
    if(self.busy)return;
    NSString *region=self.region,*path=clientPath(region);NSDictionary *settings=self.settings;
    @try{if(launch)[self save];}@catch(NSException *e){[self error:e.reason];return;}
    [self busy:YES];self.status.stringValue=@"正在校验客户端与画质组件…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSString *failure=nil,*runtime=nil;
        @try {validatePackage(region,path);if(launch){stopClient(region,NO);runtime=prepareRuntime(region,path,settings);}}
        @catch(NSException *e){failure=e.reason;}
        dispatch_async(dispatch_get_main_queue(),^{
            if(failure){[self busy:NO];[self error:failure];return;}
            if(!launch){[self busy:NO];self.status.stringValue=@"客户端与全部画质组件校验通过。";return;}
            self.status.stringValue=@"正在启动游戏；登录进入场景后自动应用设置…";
            launchClient(region,path,runtime,settings,^(NSError *error){[self busy:NO];if(error)[self error:error.localizedDescription];});
        });
    });
}
- (void)launch:(id)sender {[self runCheck:YES];}
- (void)check:(id)sender {[self runCheck:NO];}
- (void)restore:(id)sender {
    @try {
        if([NSRunningApplication runningApplicationsWithBundleIdentifier:packageInfo()[@"clients"][self.region][@"bundleID"]].count)fail(@"请先关闭游戏，再恢复 PlayCover 设置。");
        displaySettings(clientPath(self.region),restoreRecord(self.region),packageInfo()[@"displaySettings"],YES);
        self.status.stringValue=@"PlayCover 设置已恢复。";
    }@catch(NSException *e){[self error:e.reason];}
}
- (void)copyLog:(id)sender {
    NSString *text=[NSString stringWithContentsOfFile:[runtimeRoot(self.region) stringByAppendingPathComponent:@"bettermacfield-runtime.log"] encoding:NSUTF8StringEncoding error:nil];
    if(!text){self.status.stringValue=@"尚无运行日志。";return;}
    [NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];self.status.stringValue=@"运行日志已复制。";
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
    if(!self.dirty)self.status.stringValue=[NSString stringWithFormat:@"%@%@",prefix,live ? [@" · " stringByAppendingString:state[@"settingsResult"] ?: @"就绪"] : @""];
    BOOL acknowledged=!self.pendingRequest || ([self.pendingRequest isEqual:state[@"id"]] && ![state[@"settingsPending"] boolValue]);
    if(acknowledged)self.pendingRequest=nil;
    if(live && acknowledged && !self.dirty && [state[@"settings"] isKindOfClass:NSDictionary.class]){
        NSString *fingerprint=[state[@"settings"] description];
        if(![fingerprint isEqual:self.lastSettingsFingerprint]){
            self.lastSettingsFingerprint=fingerprint;[self showSettings:state[@"settings"]];
            @try{directory(stateRoot());writeJSON(state[@"settings"],preferencesPath(region));}@catch(NSException *e){}
        }
    }
    NSMutableString *text=[NSMutableString stringWithFormat:@"%@\n客户端：%@\n版本：1.6.3\n安装位置：%@\n\n",prefix,[region isEqual:@"cn"] ? @"国服" : @"国际服",clientPath(region)];
    if(live){
        [text appendFormat:@"场景：%@\n%@\n光影对比：%@\n光影程序：%@    角色程序：%@\nGTAO 帧数：%@  错误：%@\n接触阴影帧数：%@  错误：%@\n\n",[state[@"sceneReady"] boolValue] ? @"已进入" : @"等待进入",state[@"settingsResult"],[state[@"comparisonBaseline"] boolValue] ? @"原生" : @"增强设置",state[@"lightingPrograms"],state[@"characterPrograms"],state[@"gtaoEncodes"],state[@"gtaoErrors"],state[@"contactEncodes"],state[@"contactErrors"]];
        NSDictionary *fx=state[@"metalFX"];[text appendFormat:@"MetalFX：%@\n",fx ?: @{}];
    }
    [text appendString:@"\nF11 / ⌘⇧G：临时切换光影。分辨率、超分和 LOD 保持当前设置。\n\nGTAO、接触阴影、SSR、角色光影可单独调整。\nMOD、模型和贴图替换未打包。\n\n测试版默认 Spatial＋原生 TAAU，移动时有拖影。\nTemporal 为实验选项，macOS 27 曾出现 ANE 异常退出。\n国服和国际服已完成 TAAU 场景抽查；状态页显示实际执行次数。"];
    self.details.string=text;
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
                NSString *region=argument(args,@"--region") ?: @"global";
                if(!packageInfo()[@"clients"][region])fail(@"未知客户端区域。");
                NSString *path=argument(args,@"--app") ?: clientPath(region);
                if(restore){stopClient(region,NO);displaySettings(path,restoreRecord(region),packageInfo()[@"displaySettings"],YES);puts("PlayCover settings restored.");return 0;}
                validatePackage(region,path);
                if(check){printf("Verified %s 1.6.3 and all packaged graphics programs.\n",region.UTF8String);return 0;}
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
