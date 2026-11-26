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
static NSString *clientPath(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Containers/io.playcover.PlayCover/Applications/com.gryphline.endfield.ios.app"];
}
static NSString *runtimeRoot(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Containers/com.gryphline.endfield.ios/Data/tmp/BetterMacField"];
}
static NSString *preferencesPath(void) {return [stateRoot() stringByAppendingPathComponent:@"settings-global.json"];}
static NSDictionary *catalog(void) {return readJSON([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"features.json"]);}
static NSDictionary *loadSettings(void) {
    NSMutableDictionary *values=[NSMutableDictionary dictionary];NSDictionary *saved=readJSON(preferencesPath());
    for(NSDictionary *field in catalog()[@"fields"])values[field[@"key"]]=saved[field[@"key"]] ?: field[@"default"];
    return values;
}
static NSString *restoreRecord(void) {return [stateRoot() stringByAppendingPathComponent:@"playcover-settings.json"];}
#include "current172.inc"
static void validatePackage(NSString *appPath) {
    NSDictionary *package=packageInfo();
    if(![package[@"clients"][@"global"][@"bundleID"] isEqual:@"com.gryphline.endfield.ios"])
        fail(@"启动器缺少国际服 1.7.2 配置，请重新下载完整安装包。");
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[appPath stringByAppendingPathComponent:@"Info.plist"]];
    if(!info)fail(@"没有找到客户端。请先在 PlayCover 安装国际服 1.7.2，并启动游戏下载完资料。");
    if(![info[@"CFBundleIdentifier"] isEqual:@"com.gryphline.endfield.ios"] || ![info[@"CFBundleShortVersionString"] isEqual:@"1.7.2"])
        fail([NSString stringWithFormat:@"当前客户端 %@（%@）尚未适配。此版本支持国际服 1.7.2。",info[@"CFBundleShortVersionString"] ?: @"未知",info[@"CFBundleIdentifier"] ?: @"未知"]);
    validate172(appPath);
    NSDictionary *payload=package[@"payload"];NSString *resources=NSBundle.mainBundle.resourcePath;
    for(NSString *required in @[@"features.json",@"BetterMacField172.dylib",@"BetterMacFieldGraphics.bundle/Contents/Info.plist",@"BetterMacFieldGraphics.bundle/Contents/MacOS/BetterMacFieldGraphics"])
        if(![payload[required] isKindOfClass:NSString.class])fail(@"启动器缺少画质组件校验清单，请重新下载完整安装包。");
    for(NSString *relative in payload)
        if(![sha256([resources stringByAppendingPathComponent:relative]) isEqual:payload[relative]])
            fail([@"BetterMacField 的画质组件已损坏或被修改，请重新下载完整安装包。\n问题文件：" stringByAppendingString:relative]);
}
static void stopClient(BOOL restart) {
    NSArray *running=[NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.gryphline.endfield.ios"];
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
@interface BMFLauncher : NSObject<NSApplicationDelegate,NSWindowDelegate>
@property NSWindow *window;
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
@property NSTimeInterval pendingSince;
@property NSString *lastError;
@end
@implementation BMFLauncher
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
- (void)error:(NSString *)message {
    self.lastError=message;
    self.status.stringValue=[NSString stringWithFormat:@"%@ 请在「状态与恢复」查看详情。",[message componentsSeparatedByString:@"\n"].firstObject];
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
    if(![self confirmUsage]){[NSApp terminate:nil];return;}
    self.controls=[NSMutableDictionary dictionary];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,780,660) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"BetterMacField";self.window.delegate=self;self.window.releasedWhenClosed=NO;
    NSView *root=self.window.contentView;
    NSTextField *title=label(@"BetterMacField",26,YES);title.frame=NSMakeRect(28,604,450,36);[root addSubview:title];
    NSTextField *sub=label(@"终末地 · macOS 增强工具",13,NO);sub.textColor=NSColor.secondaryLabelColor;sub.frame=NSMakeRect(30,577,580,22);[root addSubview:sub];
    NSTextField *version=label(@"国际服 · 1.7.2",14,NO);version.frame=NSMakeRect(590,608,160,26);[root addSubview:version];
    NSTabView *tabs=[[NSTabView alloc] initWithFrame:NSMakeRect(24,136,732,432)];[root addSubview:tabs];
    for(NSString *name in @[@"画面",@"光影",@"操作",@"状态与恢复"]){
        NSTabViewItem *tab=[[NSTabViewItem alloc] initWithIdentifier:name];tab.label=name;
        NSView *page=[[NSView alloc] initWithFrame:NSMakeRect(0,0,706,388)];tab.view=page;[tabs addTabViewItem:tab];
        if([name isEqual:@"状态与恢复"]){
            NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(16,92,672,278)];scroll.hasVerticalScroller=YES;scroll.borderType=NSBezelBorder;
            self.details=[[NSTextView alloc] initWithFrame:scroll.bounds];self.details.editable=NO;self.details.font=[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];self.details.autoresizingMask=NSViewWidthSizable;scroll.documentView=self.details;[page addSubview:scroll];
            [self button:@"校验客户端与组件" action:@selector(check:) frame:NSMakeRect(16,48,175,30) parent:page];
            [self button:@"复制诊断信息" action:@selector(copyLog:) frame:NSMakeRect(203,48,150,30) parent:page];
            [self button:@"恢复 PlayCover 设置" action:@selector(restore:) frame:NSMakeRect(365,48,190,30) parent:page];
            NSTextField *help=label(@"恢复本工具修改的 PlayCover 设置，保留之后手动修改的值。",11,NO);help.frame=NSMakeRect(20,12,650,30);help.textColor=NSColor.secondaryLabelColor;[page addSubview:help];continue;
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
            y-=([name isEqual:@"画面"] ? (text.length ? 60 : 44) : 51);
        }

    }
    self.status=label(@"准备就绪",12,NO);self.status.frame=NSMakeRect(30,84,720,40);[root addSubview:self.status];
    self.compareButton=[self button:@"画面对比 · ⌘G" action:@selector(compare:) frame:NSMakeRect(28,30,170,36) parent:root];
    self.applyButton=[self button:@"保存并应用" action:@selector(apply:) frame:NSMakeRect(432,30,145,36) parent:root];
    self.launchButton=[self button:@"启动游戏" action:@selector(launch:) frame:NSMakeRect(590,30,160,36) parent:root];self.launchButton.keyEquivalent=@"\r";
    [self showSettings:loadSettings()];[self refresh:nil];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:3 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];

}
- (void)save {directory(stateRoot());writeJSON(self.settings,preferencesPath());self.dirty=NO;}
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
- (void)apply:(id)sender {
    @try{[self save];
        if(live172(state172()))[self send:@"configure" settings:self.settings];
        self.status.stringValue=@"设置已保存。启动选项在下次启动生效。";}
    @catch(NSException *e){[self error:e.reason];}
}
- (void)compare:(id)sender {@try{[self send:@"compare" settings:nil];}@catch(NSException *e){[self error:e.reason];}}
- (void)busy:(BOOL)value {
    self.busy=value;self.launchButton.enabled=!value;self.applyButton.enabled=!value;
}
- (void)runCheck:(BOOL)launch {
    if(self.busy)return;
    self.lastError=nil;NSString *path=clientPath();NSDictionary *settings=self.settings;
    @try{if(launch)[self save];}@catch(NSException *e){[self error:e.reason];return;}
    [self busy:YES];self.status.stringValue=@"正在校验客户端与画质组件…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSException *failure=nil;
        @try {validatePackage(path);if(launch)prepare172(path,settings);}
        @catch(NSException *e){failure=e;}
        dispatch_async(dispatch_get_main_queue(),^{
            if(failure){[self busy:NO];[self error:failure.reason];return;}
            if(!launch){[self busy:NO];self.status.stringValue=@"客户端与全部画质组件校验通过。";return;}
            self.status.stringValue=@"正在启动游戏；登录进入场景后自动应用设置…";
            launch172(settings,^(NSError *error){[self busy:NO];if(error)[self error:error.localizedDescription];});
        });
    });
}
- (void)launch:(id)sender {[self runCheck:YES];}
- (void)check:(id)sender {[self runCheck:NO];}
- (void)restore:(id)sender {
    @try {
        stopClient(NO);displaySettings(clientPath(),restoreRecord(),packageInfo()[@"displaySettings"],YES);
        self.status.stringValue=@"PlayCover 设置已恢复。";
    }@catch(NSException *e){[self error:e.reason];}
}
- (void)copyLog:(id)sender {
    NSMutableString *text=[NSMutableString stringWithString:self.details.string ?: @""];
    NSData *data=[NSJSONSerialization dataWithJSONObject:state172() ?: @{} options:NSJSONWritingPrettyPrinted error:nil];
    [text appendFormat:@"\n\n图形组件状态：\n%@",[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]];
    [NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];self.status.stringValue=@"诊断信息已复制，可粘贴给发布者。";
}
- (void)refresh:(id)sender {
    if(self.busy)return;
    NSArray<NSRunningApplication *> *apps=[NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.gryphline.endfield.ios"];
    NSDictionary *state=state172();BOOL live=live172(state);
    self.compareButton.enabled=live && [state[@"sceneReady"] boolValue];
    self.launchButton.enabled=!apps.count || (running172() && !live);
    self.launchButton.title=running172() && !live ? @"连接游戏" : @"启动游戏";
    NSString *prefix=apps.count ? (live ? @"增强模块已连接" : @"游戏运行中 · 等待增强模块连接") : @"游戏未运行";
    if(apps.count && !running172())prefix=@"另一份国际服游戏正在运行，请退出后启动公开版";
    if(!self.dirty && !self.lastError)self.status.stringValue=[NSString stringWithFormat:@"%@%@",prefix,live ? [@" · " stringByAppendingString:(state[@"settingsResult"] ?: @"就绪")] : @""];
    BOOL acknowledged=!self.pendingRequest || ([self.pendingRequest isEqual:state[@"id"]] && ![state[@"settingsPending"] boolValue]);
    if(acknowledged && self.pendingRequest && [state[@"commandError"] isKindOfClass:NSString.class])[self error:state[@"commandError"]];
    if(self.pendingRequest && NSProcessInfo.processInfo.systemUptime-self.pendingSince>15){self.pendingRequest=nil;[self error:@"游戏未及时确认设置，请进入可玩场景后重试。"];}
    if(acknowledged)self.pendingRequest=nil;
    if(live && acknowledged && !self.dirty && [state[@"settings"] isKindOfClass:NSDictionary.class]){
        NSString *fingerprint=[state[@"settings"] description];
        if(![fingerprint isEqual:self.lastSettingsFingerprint]){
            NSMutableDictionary *values=[loadSettings() mutableCopy];[values addEntriesFromDictionary:state[@"settings"]];
            self.lastSettingsFingerprint=fingerprint;[self showSettings:values];
            @try{directory(stateRoot());writeJSON(values,preferencesPath());}@catch(NSException *e){}
        }
    }
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[clientPath() stringByAppendingPathComponent:@"Info.plist"]];
    NSMutableString *text=[NSMutableString stringWithFormat:@"%@\n客户端：国际服\n已安装版本：%@\n支持版本：1.7.2\n安装位置：%@\n公开版副本：%@\n%@\n\n",prefix,info[@"CFBundleShortVersionString"] ?: @"未找到",clientPath(),clone172(),state[@"waiting"] ?: state[@"error"] ?: @""];
    if(live){
        NSDictionary *effects=state[@"sceneLighting172"];
        [text appendFormat:@"场景：%@\n画面对比：%@\nGTAO：%@ 帧 · %@ 错误\n接触阴影：%@ 帧 · %@ 错误\n",[state[@"sceneReady"] boolValue] ? @"已进入" : @"等待进入",[state[@"comparisonBaseline"] boolValue] ? @"原生设置" : @"增强设置",effects[@"gtaoEncodes"] ?: @0,effects[@"gtaoErrors"] ?: @0,effects[@"contactEncodes"] ?: @0,effects[@"contactErrors"] ?: @0];
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
                NSString *region=argument(args,@"--region") ?: @"global";
                if(![region isEqual:@"global"])fail(@"此版本仅支持国际服 1.7.2；未知客户端区域。");
                if([args containsObject:@"--force"])fail(@"此版本需要匹配国际服 1.7.2，--force 已移除。");
                NSString *path=argument(args,@"--app") ?: clientPath();
                if(status){
                    NSDictionary *state=state172();if(!live172(state))state=nil;
                    NSData *data=[NSJSONSerialization dataWithJSONObject:state ?: @{} options:NSJSONWritingPrettyPrinted error:nil];
                    fwrite(data.bytes,1,data.length,stdout);puts("");return 0;
                }
                if(restore){stopClient(NO);displaySettings(path,restoreRecord(),packageInfo()[@"displaySettings"],YES);puts("PlayCover settings restored.");return 0;}
                validatePackage(path);
                if(check){puts("Verified global 1.7.2 and all packaged graphics programs.");return 0;}
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
