#import "../src/v172/client_identity172.h"

// Select once per launcher process. An outstanding activation, settings reply or
// restore can never move to another client's container as the UI is refreshed.
static NSString *selectedRegion172;
static NSString *regionName172(void) {return [selectedRegion172 isEqual:@"cn"] ? @"国服 / CN" : @"国际服 / Global";}
static NSString *bundleForRegion172(NSString *region) {
    NSString *bundle=packageInfo()[@"clients"][region][@"bundleID"];
    if(![regionForBundle172(bundle) isEqual:region])fail(@"缺少客户端区域配置。 / Missing client profile.");
    return bundle;
}
static NSString *installedPath172(NSString *bundle) {
    return [NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/Containers/io.playcover.PlayCover/Applications/%@.app",bundle]];
}
static BOOL selectRegion172(NSArray *args,BOOL gui) {
    NSString *region=argument(args,@"--region"),*path=argument(args,@"--app");
    if([args containsObject:@"--region"] && !([region isEqual:@"global"] || [region isEqual:@"cn"]))
        fail(@"未知区域。 / Use --region global or --region cn.");
    if(!region && path){
        NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[path stringByAppendingPathComponent:@"Info.plist"]];
        region=regionForBundle172(info[@"CFBundleIdentifier"]);
        if(!region)fail(@"未找到受支持的客户端。 / No supported client at --app.");
    }
    if(!region){
        BOOL global=[NSFileManager.defaultManager fileExistsAtPath:installedPath172(bundleForRegion172(@"global"))];
        BOOL cn=[NSFileManager.defaultManager fileExistsAtPath:installedPath172(bundleForRegion172(@"cn"))];
        if(global==cn){
            if(!gui)fail(global ? @"检测到两个客户端。 / Both clients are installed; specify --region global or --region cn." :
                                 @"未安装客户端。 / No PlayCover client found; use --app to check a local copy.");
            NSAlert *a=[NSAlert new];a.messageText=@"选择客户端 / Choose client";
            if(!global)a.informativeText=@"未检测到 PlayCover 客户端。 / No PlayCover client is installed.";
            [a addButtonWithTitle:@"国际服 / Global"];[a addButtonWithTitle:@"国服 / CN"];[a addButtonWithTitle:@"取消 / Cancel"];
            NSModalResponse choice=[a runModal];
            if(choice!=NSAlertFirstButtonReturn && choice!=NSAlertSecondButtonReturn)return NO;
            region=choice==NSAlertSecondButtonReturn ? @"cn" : @"global";
        }else region=cn ? @"cn" : @"global";
    }
    bundleForRegion172(region);selectedRegion172=[region copy];return YES;
}
static NSString *bundleID172(void) {return bundleForRegion172(selectedRegion172);}
static NSString *clientPath(void) {return installedPath172(bundleID172());}
static NSString *cloneRelative172(void) {return [@"Applications" stringByAppendingPathComponent:clientPath().lastPathComponent];}
static NSString *runtimeRoot(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Library/Containers/%@/Data/tmp/BetterMacField",bundleID172()]];
}
static NSString *preferencesPath(void) {return [stateRoot() stringByAppendingPathComponent:[NSString stringWithFormat:@"settings-%@.json",selectedRegion172]];}
static NSString *restoreRecord(void) {return [stateRoot() stringByAppendingPathComponent:[selectedRegion172 isEqual:@"cn"] ? @"playcover-settings-cn.json" : @"playcover-settings.json"];}
