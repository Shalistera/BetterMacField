#import <Foundation/Foundation.h>

// In a sandboxed game NSHomeDirectory is its container's Data directory.
static NSString *runtimeFile172(NSString *name, BOOL inGame) {
    NSString *root = inGame ? NSHomeDirectory() :
        [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Containers/com.gryphline.endfield.ios/Data"];
    return [[root stringByAppendingPathComponent:@"tmp/BetterMacField"] stringByAppendingPathComponent:name];
}
