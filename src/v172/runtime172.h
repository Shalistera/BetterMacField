#import <Foundation/Foundation.h>

// In a sandboxed game NSHomeDirectory is its container's Data directory.
static NSString *runtimeFile172(NSString *name) {
    return [[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/BetterMacField"] stringByAppendingPathComponent:name];
}
