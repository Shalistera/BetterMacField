#import <Foundation/Foundation.h>
#import "runtime172.h"
#import "client_identity172.h"
#include <fcntl.h>
#include <unistd.h>

@protocol BMFGraphicsActivation
+ (void)activateWithRequest:(NSDictionary *)request;
@end

// PlayTools loads this Catalyst plugin. Publish an instance identifier before
// accepting an activation from the launcher for this particular process.
__attribute__((constructor)) static void registerGraphics172(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSBundle *game = NSBundle.mainBundle;
        if (!regionForBundle172(game.bundleIdentifier) ||
            ![[game objectForInfoDictionaryKey:@"CFBundleVersion"] isEqual:@"0"] ||
            ![[game objectForInfoDictionaryKey:@"CFBundleShortVersionString"] isEqual:@"1.7.2"]) return;
        NSString *requestPath = runtimeFile172(@"graphics172-activation.json");
        NSString *directory = requestPath.stringByDeletingLastPathComponent;
        if (![NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES
                                                       attributes:nil error:NULL]) return;
        int fd = open(directory.fileSystemRepresentation, O_EVTONLY | O_CLOEXEC);
        if (fd < 0) return;
        static dispatch_source_t watcher;
        watcher = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, fd, DISPATCH_VNODE_WRITE,
                                         dispatch_get_main_queue());
        if (!watcher) { close(fd); return; }
        NSString *instance = NSUUID.UUID.UUIDString;
        void (^activate)(void) = ^{
            NSData *data = [NSData dataWithContentsOfFile:requestPath];
            if (!data || data.length > 65536) return;
            NSDictionary *request = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
            if (![request isKindOfClass:NSDictionary.class] || [request[@"pid"] intValue] != getpid() ||
                ![instance isEqual:request[@"instance"]] || ![request[@"session"] isKindOfClass:NSString.class] ||
                ![request[@"session"] length] || ![request[@"settings"] isKindOfClass:NSDictionary.class]) return;
            dispatch_source_cancel(watcher);
            NSURL *url = [game.bundleURL URLByAppendingPathComponent:@"PlugIns/BetterMacFieldGraphics.bundle"];
            NSBundle *graphics = [NSBundle bundleWithURL:url];
            NSError *error = nil;
            Class<BMFGraphicsActivation> entry = [graphics loadAndReturnError:&error] ? graphics.principalClass : Nil;
            if ([(Class)entry respondsToSelector:@selector(activateWithRequest:)]) {
                [entry activateWithRequest:request];
                return;
            }
            NSDictionary *state = @{@"pid":@(getpid()), @"session":request[@"session"],
                                    @"error":error.localizedDescription ?: @"图形组件缺少启动入口。"};
            NSData *message = [NSJSONSerialization dataWithJSONObject:state options:0 error:NULL];
            [message writeToFile:runtimeFile172(@"graphics172-status.json") atomically:YES];
        };
        dispatch_source_set_event_handler(watcher, activate);
        dispatch_source_set_cancel_handler(watcher, ^{ close(fd); watcher = nil; });
        dispatch_resume(watcher);
        NSDictionary *hello = @{@"pid":@(getpid()), @"instance":instance};
        NSData *data = [NSJSONSerialization dataWithJSONObject:hello options:0 error:NULL];
        [data writeToFile:runtimeFile172(@"graphics172-instance.json") atomically:YES];
        activate();
    });
}
