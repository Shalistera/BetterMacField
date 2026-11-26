// The current client uses PlayTools' Catalyst entry and a separate macOS bundle.
// All deployment and activation state belongs to the public launcher.
#include <sys/clonefile.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

static void validate172(NSString *appPath) {
    NSDictionary *profile=packageInfo()[@"clients"][selectedRegion172];
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[appPath stringByAppendingPathComponent:@"Info.plist"]];
    if(!clientMatches172(appPath,info,profile))fail([NSString stringWithFormat:
        @"%@：当前客户端 %@ (%@) 的版本或代码尚未适配。请安装匹配的 1.7.2 / build 0。\nUnsupported client build; install the verified %@ 1.7.2 (build 0) in PlayCover.",
        regionName172(),info[@"CFBundleShortVersionString"] ?: @"—",info[@"CFBundleIdentifier"] ?: @"—",selectedRegion172]);
}
static NSString *root172(void) {return [stateRoot() stringByAppendingPathComponent:[selectedRegion172 isEqual:@"cn"] ? @"CN/PlayCover" : @"PlayCover"];}
static NSString *clone172(void) {return [root172() stringByAppendingPathComponent:cloneRelative172()];}
static NSString *entry172(void) {return [root172() stringByAppendingPathComponent:@"Endfield.app"];}
static BOOL sameFile172(NSString *a,NSString *b) {
    struct stat first,second;
    return a && b && !stat(a.fileSystemRepresentation,&first) && !stat(b.fileSystemRepresentation,&second) && first.st_dev==second.st_dev && first.st_ino==second.st_ino;
}
static NSRunningApplication *running172(void) {
    for(NSRunningApplication *app in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID172()])
        if(!app.terminated && sameFile172(app.executableURL.path,[clone172() stringByAppendingPathComponent:@"Endfield"]))return app;
    return nil;
}
static void requirePublic172(void) {
    for(NSRunningApplication *app in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID172()])
        if(!app.terminated && !sameFile172(app.executableURL.path,[clone172() stringByAppendingPathComponent:@"Endfield"]))
            fail(@"另一份同服游戏正在运行。请先退出游戏，再启动 BetterMacField 公开版。");
}
static NSString *command172(NSString *executable,NSArray<NSString *> *arguments) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:executable];task.arguments=arguments;
    NSPipe *pipe=[NSPipe pipe];task.standardOutput=pipe;task.standardError=pipe;
    NSError *error=nil;if(![task launchAndReturnError:&error])fail(error.localizedDescription);
    NSData *data=[pipe.fileHandleForReading readDataToEndOfFile];[task waitUntilExit];
    NSString *output=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
    if(task.terminationStatus)fail([NSString stringWithFormat:@"%@ 执行失败：%@",executable.lastPathComponent,output]);
    return output;
}
static void signed172(NSString *path) {command172(@"/usr/bin/codesign",@[@"--verify",@"--strict",path]);}
static NSDictionary *snapshot172(NSString *source) {
    NSString *resources=NSBundle.mainBundle.resourcePath;
    return @{@"schema":@1,@"source":source.stringByResolvingSymlinksInPath,
        @"sourceResources":sha256([source stringByAppendingPathComponent:@"_CodeSignature/CodeResources"]),
        @"sourceInfo":sha256([source stringByAppendingPathComponent:@"Info.plist"]),
        @"sourceSignature":command172(@"/usr/bin/codesign",@[@"-d",@"--verbose=4",source]),
        @"entry":sha256([resources stringByAppendingPathComponent:@"BetterMacField172.dylib"]),
        @"graphics":sha256([resources stringByAppendingPathComponent:@"BetterMacFieldGraphics.bundle/Contents/_CodeSignature/CodeResources"])};
}
static NSDictionary *cloneInfo172(NSString *source) {
    NSMutableDictionary *info=[[NSDictionary dictionaryWithContentsOfFile:[source stringByAppendingPathComponent:@"Info.plist"]] mutableCopy];
    [info removeObjectForKey:@"NSUserTrackingUsageDescription"];info[@"GLEnableTracking"]=@"NO";
    return info;
}
static BOOL validClone172(NSString *root,NSString *source,NSDictionary *snapshot) {
    NSString *clone=[root stringByAppendingPathComponent:cloneRelative172()];
    NSString *alias=[root stringByAppendingPathComponent:@"Endfield.app"];
    NSDictionary *receipt=readJSON([root stringByAppendingPathComponent:@"public-game.json"]);
    if(![receipt[@"source"] isEqual:snapshot] || ![clone.stringByResolvingSymlinksInPath isEqual:clone] ||
       sameFile172([clone stringByAppendingPathComponent:@"Endfield"],[source stringByAppendingPathComponent:@"Endfield"]))return NO;
    @try{
        if(![sha256([clone stringByAppendingPathComponent:@"_CodeSignature/CodeResources"]) isEqual:receipt[@"resources"]] ||
           ![sha256([clone stringByAppendingPathComponent:@"Frameworks/UserPlugins/BetterMacField172.dylib"]) isEqual:snapshot[@"entry"]] ||
           ![sha256([clone stringByAppendingPathComponent:@"PlugIns/BetterMacFieldGraphics.bundle/Contents/_CodeSignature/CodeResources"]) isEqual:snapshot[@"graphics"]] ||
           ![[NSDictionary dictionaryWithContentsOfFile:[clone stringByAppendingPathComponent:@"Info.plist"]] isEqual:cloneInfo172(source)])return NO;
        NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:clone error:nil];
        if(![[NSSet setWithArray:names] isEqual:[NSSet setWithArray:[NSFileManager.defaultManager contentsOfDirectoryAtPath:alias error:nil] ?: @[]]])return NO;
        for(NSString *name in names){
            NSString *item=[alias stringByAppendingPathComponent:name];
            if([name isEqual:@"Info.plist"]){if(![[NSData dataWithContentsOfFile:item] isEqual:[NSData dataWithContentsOfFile:[clone stringByAppendingPathComponent:name]]])return NO;}
            else if(![[NSFileManager.defaultManager destinationOfSymbolicLinkAtPath:item error:nil] isEqual:[[@".." stringByAppendingPathComponent:cloneRelative172()] stringByAppendingPathComponent:name]])return NO;
        }
        signed172(clone);return YES;
    }@catch(NSException *e){return NO;}
}
static void copyClone172(NSString *source,NSString *target) {
    NSFileManager *fm=NSFileManager.defaultManager;directory(target);
    NSDirectoryEnumerator *items=[fm enumeratorAtPath:source];
    for(NSString *relative in items){
        NSString *from=[source stringByAppendingPathComponent:relative],*to=[target stringByAppendingPathComponent:relative];
        struct stat st;if(lstat(from.fileSystemRepresentation,&st))fail(@"读取游戏文件失败。");
        if(S_ISDIR(st.st_mode)){directory(to);[fm setAttributes:@{NSFilePosixPermissions:@(st.st_mode&07777)} ofItemAtPath:to error:nil];}
        else if(S_ISLNK(st.st_mode)){
            NSString *link=[fm destinationOfSymbolicLinkAtPath:from error:nil];NSError *error=nil;
            if(!link || ![fm createSymbolicLinkAtPath:to withDestinationPath:link error:&error])fail(error.localizedDescription ?: @"复制游戏链接失败。");
        }else if(S_ISREG(st.st_mode)){
            if(clonefile(from.fileSystemRepresentation,to.fileSystemRepresentation,0))fail(@"无法创建游戏的独立 APFS 副本。请将 PlayCover 游戏和用户目录放在同一个 APFS 卷。");
        }else fail(@"游戏包包含无法复制的文件类型。");
    }
}
static void prepareClone172(NSString *source) {
    NSString *installed=clientPath();
    if(!sameFile172([source stringByAppendingPathComponent:@"Endfield"],[installed stringByAppendingPathComponent:@"Endfield"]))
        fail(@"请使用 PlayCover 已安装的所选区域原游戏创建公开版副本。");
    source=installed;validate172(source);
    NSFileManager *fm=NSFileManager.defaultManager;NSString *parent=root172().stringByDeletingLastPathComponent;
    if(![parent.stringByResolvingSymlinksInPath isEqual:parent])fail(@"副本目录不能是符号链接。");directory(parent);
    int lock=open([parent stringByAppendingPathComponent:@"public-game.lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX)){if(lock>=0)close(lock);fail(@"无法锁定公开版游戏副本。");}
    NSString *stage=[parent stringByAppendingPathComponent:[@".public-game-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    @try{
        NSDictionary *snapshot=snapshot172(source);
        if(validClone172(root172(),source,snapshot))return;
        if(running172())fail(@"公开版组件已更新。请先退出游戏，再重新启动以更新副本。");
        BOOL exists=[fm fileExistsAtPath:root172()];
        if(![root172().stringByResolvingSymlinksInPath isEqual:root172()] || (exists && !readJSON([root172() stringByAppendingPathComponent:@"public-game.json"])))fail(@"公开版副本目录被其他文件占用，请检查 Application Support/BetterMacField 下所选客户端的副本。");
        NSString *resources=NSBundle.mainBundle.resourcePath;
        signed172(source);signed172([resources stringByAppendingPathComponent:@"BetterMacField172.dylib"]);signed172([resources stringByAppendingPathComponent:@"BetterMacFieldGraphics.bundle"]);
        NSString *clone=[stage stringByAppendingPathComponent:cloneRelative172()];
        copyClone172(source,clone);
        for(NSString *relative in @[@"Endfield",@"Info.plist",@"_CodeSignature/CodeResources"]){
            NSString *item=[clone stringByAppendingPathComponent:relative];
            if(![item.stringByResolvingSymlinksInPath isEqual:item] || sameFile172(item,[source stringByAppendingPathComponent:relative]))
                fail(@"游戏副本的签名文件必须独立于原游戏。");
        }
        if(![cloneInfo172(source) writeToFile:[clone stringByAppendingPathComponent:@"Info.plist"] atomically:YES])fail(@"无法写入公开版游戏配置。");
        for(NSString *relative in @[@"Frameworks/UserPlugins",@"PlugIns"]){
            NSString *path=[clone stringByAppendingPathComponent:relative];
            if(![path.stringByResolvingSymlinksInPath isEqual:path])fail(@"游戏插件目录链接到了其他位置。");directory(path);
        }
        for(NSString *name in @[@"BetterMacField172.dylib",@"BetterMacFieldGraphics.bundle"]){
            NSString *directoryName=[name.pathExtension isEqual:@"dylib"] ? @"Frameworks/UserPlugins" : @"PlugIns";
            NSString *target=[[clone stringByAppendingPathComponent:directoryName] stringByAppendingPathComponent:name];
            if([fm fileExistsAtPath:target])fail(@"原游戏已包含同名增强组件，请从 PlayCover 安装的原游戏启动。");
            command172(@"/usr/bin/ditto",@[[resources stringByAppendingPathComponent:name],target]);
        }
        command172(@"/usr/bin/codesign",@[@"--force",@"--sign",@"-",@"--timestamp=none",@"--preserve-metadata=identifier,entitlements,requirements,flags,runtime,launch-constraints,library-constraints",clone]);
        signed172(clone);
        NSString *alias=[stage stringByAppendingPathComponent:@"Endfield.app"];directory(alias);
        for(NSString *name in [fm contentsOfDirectoryAtPath:clone error:nil]){
            NSError *error=nil;NSString *target=[alias stringByAppendingPathComponent:name];
            BOOL copied=[name isEqual:@"Info.plist"] ? [fm copyItemAtPath:[clone stringByAppendingPathComponent:name] toPath:target error:&error] :
                [fm createSymbolicLinkAtPath:target withDestinationPath:[[@".." stringByAppendingPathComponent:cloneRelative172()] stringByAppendingPathComponent:name] error:&error];
            if(!copied)fail(error.localizedDescription);
        }
        if(![snapshot isEqual:snapshot172(source)])fail(@"创建副本期间原游戏发生变化，请重试。");
        writeJSON(@{@"source":snapshot,@"resources":sha256([clone stringByAppendingPathComponent:@"_CodeSignature/CodeResources"])},[stage stringByAppendingPathComponent:@"public-game.json"]);
        if(!validClone172(stage,source,snapshot))fail(@"公开版游戏副本校验失败。");
        if(exists){if(renamex_np(stage.fileSystemRepresentation,root172().fileSystemRepresentation,RENAME_SWAP))fail(@"无法替换公开版游戏副本。");}
        else if(rename(stage.fileSystemRepresentation,root172().fileSystemRepresentation))fail(@"无法安装公开版游戏副本。");
    }@finally{[fm removeItemAtPath:stage error:nil];close(lock);}
}
static NSDictionary *state172(void) {
    NSString *path=[runtimeRoot() stringByAppendingPathComponent:@"graphics172-status.json"];
    NSDictionary *state=readJSON(path);NSRunningApplication *game=running172();
    NSDate *written=[NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;
    if(game && game.launchDate && written && game.processIdentifier==[state[@"pid"] intValue] &&
       [written timeIntervalSinceDate:game.launchDate]>=-1 && [state[@"session"] isKindOfClass:NSString.class] && [state[@"session"] length])return state;
    return nil;
}
static BOOL live172(NSDictionary *state) {
    return [state[@"ready"] boolValue] && [state[@"liveProtocol"] integerValue]==1 && NSDate.date.timeIntervalSince1970-[state[@"updatedAt"] doubleValue]<15;
}
static NSString *prepare172(NSString *source,NSDictionary *settings) {
    requirePublic172();prepareClone172(source);directory(runtimeRoot());
    writeJSON(settings,[runtimeRoot() stringByAppendingPathComponent:@"bettermacfield-settings.json"]);
    NSMutableDictionary *display=[packageInfo()[@"displaySettings"] mutableCopy];
    display[@"keymapping"]=@NO;display[@"metalHUD"]=@([settings[@"metalHUD"] boolValue]);
    displaySettings(clientPath(),restoreRecord(),display,NO);
    return runtimeRoot();
}
static void activate172(NSRunningApplication *game,NSDictionary *settings,void (^done)(NSError *)) {
    if(!game || game.terminated || !game.launchDate || !sameFile172(game.executableURL.path,[clone172() stringByAppendingPathComponent:@"Endfield"])){
        done([NSError errorWithDomain:@"BetterMacField" code:1 userInfo:@{NSLocalizedDescriptionKey:@"系统未返回公开版游戏进程。"}]);return;
    }
    if(live172(state172())){[game activateWithOptions:NSApplicationActivateIgnoringOtherApps];done(nil);return;}
    NSString *root=runtimeRoot();NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:120];
    __block NSString *session=nil;
    [NSTimer scheduledTimerWithTimeInterval:0.3 repeats:YES block:^(NSTimer *timer){
        @try{
            if(game.terminated)fail(@"游戏在增强模块完成连接前已退出。");
            NSString *instancePath=[root stringByAppendingPathComponent:@"graphics172-instance.json"];
            NSDictionary *instance=readJSON(instancePath);
            NSDate *written=[NSFileManager.defaultManager attributesOfItemAtPath:instancePath error:nil].fileModificationDate;
            if(!session && [instance[@"pid"] intValue]==game.processIdentifier && [instance[@"instance"] isKindOfClass:NSString.class] &&
               [instance[@"instance"] length] && written && [written timeIntervalSinceDate:game.launchDate]>=-1){
                NSString *requestPath=[root stringByAppendingPathComponent:@"graphics172-activation.json"];
                NSDictionary *previous=readJSON(requestPath);
                if([previous[@"instance"] isEqual:instance[@"instance"]] && [previous[@"session"] isKindOfClass:NSString.class] && [previous[@"session"] length])session=previous[@"session"];
                else{
                    session=NSUUID.UUID.UUIDString;
                    writeJSON(@{@"pid":@(game.processIdentifier),@"instance":instance[@"instance"],@"session":session,@"settings":settings},requestPath);
                }
            }
            NSDictionary *state=state172();
            if(session && [state[@"session"] isEqual:session]){
                if([state[@"error"] isKindOfClass:NSString.class])fail(state[@"error"]);
                if([state[@"ready"] boolValue]){[timer invalidate];done(nil);return;}
            }
            if(deadline.timeIntervalSinceNow<=0)fail(session ? @"图形组件尚未就绪，请查看诊断信息。" : @"PlayTools 未报告增强插件已载入。请检查 PlayCover 安装。 ");
        }@catch(NSException *e){[timer invalidate];done([NSError errorWithDomain:@"BetterMacField" code:1 userInfo:@{NSLocalizedDescriptionKey:e.reason}]);}
    }];
}
static void launch172(NSDictionary *settings,void (^done)(NSError *)) {
    NSRunningApplication *running=running172();
    if(running){activate172(running,settings,done);return;}
    NSWorkspaceOpenConfiguration *configuration=[NSWorkspaceOpenConfiguration configuration];
    BOOL hud=[settings[@"metalHUD"] boolValue];configuration.arguments=@[@"-MetalForceHudEnabled",hud ? @"YES" : @"NO"];
    configuration.environment=@{@"MTL_HUD_ENABLED":hud ? @"1" : @"0",@"MTL_HUD_DISABLE_MENU_BAR":@"0"};
    [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:entry172() isDirectory:YES]
        configuration:configuration completionHandler:^(NSRunningApplication *game,NSError *error){
            dispatch_async(dispatch_get_main_queue(),^{if(error)done(error);else activate172(game,settings,done);});
        }];
}
