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
int main(int argc,const char **argv) {
    @autoreleasepool {
        NSArray *args=NSProcessInfo.processInfo.arguments;
        BOOL check=[args containsObject:@"--check"],restore=[args containsObject:@"--restore-settings"];
        BOOL commandLine=check || restore || [args containsObject:@"--launch"];
        @try {
            NSDictionary *package=readJSON([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"clients.json"]);
            if(!package)fail(@"启动器文件不完整，请重新解压完整安装包。");
            NSDictionary *clients=package[@"clients"];
            NSString *region=argument(args,@"--region");
            NSString *root=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Containers/io.playcover.PlayCover/Applications"];
            if(!region && !commandLine){
                [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp activateIgnoringOtherApps:YES];
                NSAlert *alert=[NSAlert new];alert.messageText=@"BetterMacField";
                alert.informativeText=@"选择已在 PlayCover 安装并下载完资料的客户端。顶部画面菜单提供画质设置；F11 可切换光影对比。";
                NSButton *restoreOption=[NSButton checkboxWithTitle:@"恢复 PlayCover 设置（需先退出游戏）" target:nil action:NULL];
                restoreOption.frame=NSMakeRect(0,0,300,24);alert.accessoryView=restoreOption;
                [alert addButtonWithTitle:@"国际服"];[alert addButtonWithTitle:@"国服"];[alert addButtonWithTitle:@"取消"];
                NSModalResponse choice=[alert runModal];if(choice==NSAlertThirdButtonReturn)return 0;
                region=choice==NSAlertSecondButtonReturn ? @"cn" : @"global";
                restore=restoreOption.state==NSControlStateValueOn;
            }
            region=region ?: @"global";NSDictionary *client=clients[region];if(!client)fail(@"未知客户端区域。");
            NSString *bundle=client[@"bundleID"];
            NSString *appPath=argument(args,@"--app") ?: [root stringByAppendingPathComponent:[bundle stringByAppendingString:@".app"]];
            NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[appPath stringByAppendingPathComponent:@"Info.plist"]];
            if(!info)fail(@"没有找到客户端。请先在 PlayCover 安装对应服的 IPA，并正常启动、下载完游戏资料。");
            if(![info[@"CFBundleIdentifier"] isEqual:bundle] || ![info[@"CFBundleShortVersionString"] isEqual:client[@"version"]])
                fail(@"客户端版本不匹配。此包支持指定构建的 iOS 1.6.3，请等待对应版本的 BetterMacField 更新。");
            NSString *state=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/BetterMacField"];
            NSString *record=[state stringByAppendingPathComponent:[region isEqual:@"cn"] ? @"playcover-settings-cn.json" : @"playcover-settings.json"];
            NSArray *running=[NSRunningApplication runningApplicationsWithBundleIdentifier:bundle];
            if(restore){
                if(running.count)fail(@"请先关闭游戏，再恢复 PlayCover 设置。");
                displaySettings(appPath,record,package[@"displaySettings"],YES);
                if(commandLine)puts("PlayCover settings restored.");
                else{NSAlert *alert=[NSAlert new];alert.messageText=@"PlayCover 设置已恢复";alert.informativeText=@"现在可以从 PlayCover 正常启动游戏。";[alert runModal];}
                return 0;
            }
            for(NSString *relative in client[@"files"]){
                if(![sha256([appPath stringByAppendingPathComponent:relative]) isEqual:client[@"files"][relative]])
                    fail([@"客户端文件已改变，需要重新适配：" stringByAppendingString:relative]);
            }
            NSString *source=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"libBetterMacField.dylib"];
            if(![sha256(source) isEqual:package[@"runtimeSHA256"]])fail(@"增强模块校验失败，请重新获取完整安装包。");
            if(check){printf("Verified %s 1.6.3 and packaged runtime.\n",bundle.UTF8String);return 0;}
            if(running.count){
                BOOL restart=[args containsObject:@"--restart"];
                if(!commandLine){
                    NSAlert *alert=[NSAlert new];alert.messageText=@"游戏正在运行";alert.informativeText=@"加载增强模块需要先退出并重新启动这一个客户端。";
                    [alert addButtonWithTitle:@"退出并重新启动"];[alert addButtonWithTitle:@"取消"];restart=[alert runModal]==NSAlertFirstButtonReturn;
                    if(!restart)return 0;
                }
                if(!restart)fail(@"游戏正在运行，请先退出或使用 --restart。");
                for(NSRunningApplication *app in running)[app terminate];
                NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:8];
                for(NSRunningApplication *app in running){
                    while(!app.terminated && deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
                    if(!app.terminated)fail(@"游戏未能退出，请手动关闭后重试。");
                }
            }
            NSString *runtime=[NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/Containers/%@/Data/tmp/BetterMacField",bundle]];
            directory(runtime);
            NSString *library=[runtime stringByAppendingPathComponent:@"libBetterMacField.dylib"];
            NSData *bytes=[NSData dataWithContentsOfFile:source];NSError *error=nil;
            if(![bytes writeToFile:library options:NSDataWritingAtomic error:&error])fail(error.localizedDescription);
            displaySettings(appPath,record,package[@"displaySettings"],NO);
            for(NSString *name in @[@"bettermacfield-command.json",@"bettermacfield-status.json"])
                [NSFileManager.defaultManager removeItemAtPath:[runtime stringByAppendingPathComponent:name] error:nil];
            [NSApplication sharedApplication];
            NSWorkspaceOpenConfiguration *configuration=[NSWorkspaceOpenConfiguration configuration];
            configuration.createsNewApplicationInstance=YES;configuration.activates=YES;
            configuration.arguments=@[@"-MetalForceHudEnabled",@"YES"];
            configuration.environment=@{@"DYLD_INSERT_LIBRARIES":library,@"BMF_RUNTIME_DIR":runtime,@"MTL_HUD_ENABLED":@"1",@"MTL_HUD_DISABLE_MENU_BAR":@"0"};
            [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:appPath] configuration:configuration completionHandler:^(NSRunningApplication *app,NSError *error){
                dispatch_async(dispatch_get_main_queue(),^{
                    if(error){
                        if(commandLine)fprintf(stderr,"Launch failed: %s\n",error.localizedDescription.UTF8String);
                        else{NSAlert *alert=[NSAlert new];alert.messageText=@"启动失败";alert.informativeText=error.localizedDescription;[alert runModal];}
                    }
                    exit(error ? 1 : 0);
                });
            }];
            [NSApp run];
        }@catch(NSException *exception){
            if(commandLine)fprintf(stderr,"%s\n",exception.reason.UTF8String);
            else{[NSApplication sharedApplication];NSAlert *alert=[NSAlert new];alert.messageText=@"BetterMacField";alert.informativeText=exception.reason;[alert runModal];}
            return 1;
        }
    }
    return 0;
}
