// Public graphics controls for the global and CN 1.7.2 PlayCover clients.
#import <AppKit/AppKit.h>
#import "runtime172.h"
#import "client_identity172.h"
#include <dlfcn.h>
#include <unistd.h>
#include <stdatomic.h>

@interface BMFGraphics172 : NSObject
+ (void)activateWithRequest:(NSDictionary *)request;
@end

static NSDictionary *gameProfile172;
static NSUUID *gameUUID172;
static BOOL gameUUIDBytesMatch172(const uint8_t *bytes) {
    return gameUUID172 && [gameUUID172 isEqual:[[NSUUID alloc] initWithUUIDBytes:bytes]];
}

static NSString *graphicsResource172(NSString *name) {
    return [[NSBundle bundleForClass:BMFGraphics172.class].resourcePath stringByAppendingPathComponent:name];
}

static void *(*domainGet)(void);
static void *(*threadCurrent)(void);
static const void **(*assemblies)(void *, size_t *);
static const void *(*assemblyImage)(const void *);
static const char *(*imageName)(const void *);
static void *(*lookupClass)(const void *, const char *, const char *);
static const void *(*lookupMethod)(void *, const char *, int);
static uint32_t (*methodFlags)(const void *, uint32_t *);
static void *(*objectClass)(void *);
static const char *(*className)(void *);
static void *(*invoke)(const void *, void *, void **, void **);
static void *(*unbox)(void *);
static void *(*newString)(const char *);
static const uint16_t *(*stringChars)(void *);
static int32_t (*stringLength)(void *);

@protocol BMFDisplayLink <NSObject>
- (NSTimeInterval)timestamp;
@end
@protocol BMFUnityController <NSObject>
- (id<BMFDisplayLink>)unityDisplayLink;
@end
@protocol BMFUnityFramework <NSObject>
+ (instancetype)getInstance;
- (id<BMFUnityController>)appController;
@end

// Unity creates its own display link after loading the player. Observe that link
// advance before any IL2CPP calls; a loaded UnityFramework alone is too early.
static BOOL playerDisplayLinkRunning(void) {
    Class<BMFUnityFramework> cls = (Class<BMFUnityFramework>)NSClassFromString(@"UnityFramework");
    if (![cls respondsToSelector:@selector(getInstance)]) return NO;
    id<BMFUnityFramework> framework = [cls getInstance];
    if (![framework respondsToSelector:@selector(appController)]) return NO;
    id<BMFUnityController> controller = framework.appController;
    if (![controller respondsToSelector:@selector(unityDisplayLink)]) return NO;
    id<BMFDisplayLink> link = controller.unityDisplayLink;
    static __weak id<BMFDisplayLink> observed;
    static NSTimeInterval previous;
    if (!link || ![link respondsToSelector:@selector(timestamp)]) { observed = nil; return NO; }
    NSTimeInterval current = link.timestamp;
    BOOL advancing = observed == link && previous > 0 && current > previous;
    observed = link;
    previous = current;
    return advancing;
}

static bool bindUnity(void) {
#define BIND(var, name) if (!(var = dlsym(RTLD_DEFAULT, "il2cpp_" #name))) return false;
    BIND(domainGet, domain_get)
    BIND(threadCurrent, thread_current)
    BIND(assemblies, domain_get_assemblies)
    BIND(assemblyImage, assembly_get_image)
    BIND(imageName, image_get_name)
    BIND(lookupClass, class_from_name)
    BIND(lookupMethod, class_get_method_from_name)
    BIND(methodFlags, method_get_flags)
    BIND(objectClass, object_get_class)
    BIND(className, class_get_name)
    BIND(invoke, runtime_invoke)
    BIND(unbox, object_unbox)
    BIND(newString, string_new)
    BIND(stringChars, string_chars)
    BIND(stringLength, string_length)
#undef BIND
    return true;
}

static void *classNamed(const char *image, const char *namespaceName, const char *name) {
    size_t count = 0;
    const void **list = assemblies(domainGet(), &count);
    for (size_t i = 0; i < count; ++i) {
        const void *current = assemblyImage(list[i]);
        if (current && !strcmp(imageName(current), image))
            return lookupClass(current, namespaceName, name);
    }
    return NULL;
}

static void *callClass(void *cls, void *receiver, const char *name, int n, void **args, bool *ok) {
    const void *method = cls ? lookupMethod(cls, name, n) : NULL;
    *ok = false;
    if (!method) return NULL;
    if (methodFlags(method, NULL) & 0x10) receiver = NULL;
    else if (!receiver) return NULL;
    void *exc = NULL;
    void *result = invoke(method, receiver, args, &exc);
    *ok = exc == NULL;
    return result;
}

static void *call(void *obj, const char *name, int n, void **args, bool *ok) {
    return callClass(obj ? objectClass(obj) : NULL, obj, name, n, args, ok);
}

static NSNumber *integerValue(void *obj) {
    if (!obj || strcmp(className(objectClass(obj)), "Int32")) return nil;
    return @(*(int32_t *)unbox(obj));
}

static NSNumber *renderNumber(void *obj, NSNumber *expected) {
    if (!obj) return nil;
    const char *type = className(objectClass(obj));
    if (CFGetTypeID((__bridge CFTypeRef)expected) == CFBooleanGetTypeID())
        return !strcmp(type, "Boolean") ? @(*(bool *)unbox(obj)) : nil;
    if (!strcmp(expected.objCType, @encode(float)))
        return !strcmp(type, "Single") ? @(*(float *)unbox(obj)) : nil;
    return integerValue(obj);
}

static NSString *textValue(void *obj) {
    if (!obj || strcmp(className(objectClass(obj)), "String")) return nil;
    return [[NSString alloc] initWithCharacters:stringChars(obj) length:stringLength(obj)];
}

static void *renderHub(void) {
    static void *cls;
    if (!cls) cls = classNamed("HG.RenderPipelines.Runtime.dll", "HG.Rendering.Runtime", "HGRenderPipelineSettingHub");
    bool ok;
    return callClass(cls, NULL, "get_instance", 0, NULL, &ok);
}

#include "lod172.inc"

static NSNumber *qualityScaleOverride;
static BOOL sceneResourcesReady172;
static NSDictionary *requestedGraphics(NSDictionary *settings) {
    NSMutableDictionary *requests = [NSMutableDictionary dictionary];
    NSMutableDictionary *targets = [NSMutableDictionary dictionary];
    if (qualityScaleOverride && settings[@"upscaler"] != nil)
        targets[@"RenderingScale"] = qualityScaleOverride;
    // The mobile quality table otherwise caps both targets at 1080p.
    int height = [settings[@"height"] intValue];
    if (height > 0) targets[@"BackBufferResolutionHeight"] = targets[@"TAAUResolveResolutionHeight"] = @(height);
    if(sceneResourcesReady172){
        targets[@"TransientGBuffer"]=@NO;
        requests[@"OnePassDeferred"]=@{@"shouldSplitOnePass":@YES};
    }
    if (targets.count) requests[@"RenderTargetSettings"] = targets;
    requests[@"Streaming"] = radiiForLOD172(settings[@"lod"]);
    int size = [settings[@"shadowResolution"] intValue];
    if (size == 1024 || size == 2048)
        requests[@"ShadowMap"] = @{@"CSMTileResolution":@(size), @"CharacterShadowMapResolution":@(size),
                                   @"ASMShadowMapTileResolution":@(size / 8)};
    NSNumber *density = settings[@"vegetationDensity"];
    if (density && [@[@0.5, @0.7, @1] containsObject:density])
        requests[@"Grass"] = @{@"GrassGlobalSparsity":@(density.floatValue)};
    NSString *sharpen = settings[@"sharpen"];
    if ([sharpen isEqual:@"no-taau"] || [sharpen isEqual:@"off"])
        requests[@"TAAU"] = @{@"1KSharpenStrength":@0.f, @"2KSharpenStrength":@0.f, @"4KSharpenStrength":@0.f};
    if ([sharpen isEqual:@"off"]) requests[@"PostProcess"] = @{@"SharpenEnabled":@NO};
    return requests;
}

static void backgroundFPS(BOOL scene, BOOL enabled, BOOL active, NSMutableDictionary *status) {
    static BOOL ownsLimit, confirmedLimit;
    static int foregroundFPS, lastRequest = INT_MIN;
    static NSTimeInterval lastRequestTime, targetSince;
    if (!scene && !ownsLimit) return;
    static void *application;
    if (!application) application = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Application");
    bool ok;
    status[@"backgroundActive"] = @(active);
    NSNumber *current = integerValue(callClass(application, NULL, "get_targetFrameRate", 0, NULL, &ok));
    if (!current) { status[@"backgroundError"] = @"targetFrameRate 读取失败"; return; }

    int rate = current.intValue;
    status[@"backgroundTargetFPS"] = current;
    BOOL needsLimit = scene && enabled && !active;
    if (needsLimit && !ownsLimit && (rate < 0 || rate > 15)) {
        foregroundFPS = rate;
        ownsLimit = YES;
        confirmedLimit = NO;
        lastRequest = INT_MIN;
    }
    if (!ownsLimit) { status[@"backgroundLimited"] = @NO; return; }

    // During a background scene transition Unity may choose a new foreground tier.
    if (needsLimit && confirmedLimit && rate > 15) {
        foregroundFPS = rate;
        confirmedLimit = NO;
    }
    int target = needsLimit ? 15 : foregroundFPS;
    status[@"backgroundRequestedFPS"] = @(target);
    status[@"backgroundOriginalFPS"] = @(foregroundFPS);
    status[@"backgroundLimited"] = @(needsLimit && rate == 15);
    NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    BOOL changed = target != lastRequest;
    if (changed) targetSince = now;
    if (changed || (rate != target && now - lastRequestTime >= 6)) {
        // Unity schedules this change; its getter can still report the previous
        // value on the same main-loop turn. Verify on a later timer tick.
        void *args[] = {&target};
        callClass(application, NULL, "set_targetFrameRate", 1, args, &ok);
        if (!ok) { status[@"backgroundError"] = @"targetFrameRate 调用失败"; return; }
        lastRequest = target;
        lastRequestTime = now;
    } else if (rate == target) {
        confirmedLimit = needsLimit;
        if (!needsLimit) ownsLimit = NO;
    }
    if (rate != target) {
        if (now - targetSince > 8)
            status[@"backgroundError"] = [NSString stringWithFormat:@"Unity 帧率请求 %d，当前读回 %d", target, rate];
        else status[@"backgroundPending"] = @YES;
    }
}

#include "input172.inc"
#include "lighting172.inc"
#include "native_temporal172_size.inc"
#include "metalfx172.inc"
#include "output172.inc"
#include "unity_objects172.inc"
#include "hud172.inc"
#include "character172.inc"
#include "scene172.inc"
#include "live172.inc"
#include "quality172.inc"
#include "menu172.inc"

static void writeStatus(NSMutableDictionary *state) {
    static NSDictionary *previous;
    state[@"session"] = session;
    state[@"loaded"] = @YES;
    state[@"version"] = @"1.7.2";
    state[@"liveProtocol"] = @1;
    if (selected) state[@"settings"] = [selected copy];
    if (lastCommandID) state[@"id"] = lastCommandID;
    state[@"comparisonBaseline"] = @(confirmedBaseline);
    state[@"requestedBaseline"] = @(wantsBaseline);
    static double lastBeat;
    double now = NSDate.date.timeIntervalSince1970;
    if ([previous isEqualToDictionary:state] && now - lastBeat < 4) return;
    // Counters change every tick; only a command reply or a scene change is urgent.
    BOOL (^same)(NSString *) = ^BOOL(NSString *key) { return previous[key] == state[key] || [previous[key] isEqual:state[key]]; };
    if (now - lastBeat < 0.5 && same(@"id") && same(@"sceneReady") && same(@"ready") && same(@"waiting")) return;
    previous = [state copy];
    lastBeat = now;
    state[@"updatedAt"] = @(now);
    // A report that cannot be written must not take the game down with it.
    NSData *data = [NSJSONSerialization isValidJSONObject:state] ?
        [NSJSONSerialization dataWithJSONObject:state options:0 error:NULL] : nil;
    NSError *error = nil;
    if (![data writeToFile:runtimeFile172(@"graphics172-status.json")
                   options:NSDataWritingAtomic error:&error]) {
        static BOOL reported;
        if (!reported) NSLog(@"BetterMacField 无法写入游戏状态文件：%@", error.localizedDescription);
        reported = YES;
        previous = nil;
    }
}

static atomic_bool gameEnding;

static void stopWithGame(dispatch_source_t timer) {
    NSMutableSet *events = [NSMutableSet setWithObjects:NSApplicationWillTerminateNotification,
                            @"UIApplicationWillTerminateNotification", nil];
    for (NSString *symbol in @[@"kUnityDidUnload", @"kUnityDidQuit"]) {
        NSString * __unsafe_unretained *name = (NSString * __unsafe_unretained *)dlsym(RTLD_DEFAULT, symbol.UTF8String);
        if (name && *name) [events addObject:*name];
    }
    for (NSString *event in events) {
        [NSNotificationCenter.defaultCenter addObserverForName:event object:nil queue:nil
            usingBlock:^(__unused NSNotification *notification) {
                atomic_store(&gameEnding, true);
                dispatch_source_cancel(timer);
                dispatch_async(dispatch_get_main_queue(), ^{
                    input172Ready = NO;
                });
            }];
    }
}

@implementation BMFGraphics172
+ (void)activateWithRequest:(NSDictionary *)request {
    if (session) return;
    NSDictionary *settings = request[@"settings"];
    session = request[@"session"];
    NSMutableDictionary *initial = [@{@"pid":@(getpid())} mutableCopy];
    NSBundle *game=NSBundle.mainBundle;
    NSString *region=regionForBundle172(game.bundleIdentifier);
    NSDictionary *profiles=[[NSBundle bundleForClass:self] objectForInfoDictionaryKey:@"BMFClients172"];
    gameProfile172=region ? profiles[region] : nil;
    if (!clientMatches172(game.bundlePath,game.infoDictionary,gameProfile172) || [gameProfile172[@"renderCallbacks"] count]!=3) {
        initial[@"error"]=@"游戏代码不匹配，未启用增强。 / Unsupported client build; enhancements disabled.";
        writeStatus(initial); return;
    }
    gameUUID172=[[NSUUID alloc] initWithUUIDString:gameProfile172[@"unityUUID"]];
    writeStatus(initial);
    selected = [@{@"shadowResolution":@0, @"vegetationDensity":@-1, @"sharpen":@"original",
                  @"lod":@"restore", @"backgroundLimit":@YES, @"upscaler":@"taau", @"quality":@"质量",
                  @"selfShadow":@NO, @"keyboard":@YES,
                  @"characterLighting":@NO, @"characterSpecular":@NO,
                  @"gtao":@NO, @"contact":@NO, @"ssr":@NO,
                  @"compareLOD":@YES, @"compareRendering":@YES, @"height":@0} mutableCopy];
    for (NSString *key in selected.allKeys)
        if (validValue(key, settings[key])) selected[key] = settings[key];
    char172InstallLibraries(([selected[@"characterLighting"] boolValue] ? 3 : 0) | ([selected[@"characterSpecular"] boolValue] ? 4 : 0));
    installScene172();
    ownedValues = [NSMutableDictionary dictionary];
    changedInGame = [NSMutableSet set];
    baselineChanges = [NSMutableDictionary dictionary];
    needsApply = YES;
    NSMutableDictionary *renderState = [NSMutableDictionary dictionary];
    void (^menuAction)(NSString *, id) = ^(NSString *key, id value) {
        NSDictionary *command = [key isEqual:@"compare"] ? @{@"op":@"compare", @"enabled":value} :
                                [key isEqual:@"settings"] ? @{@"op":@"configure", @"settings":value} :
                                @{@"op":@"configure", @"settings":@{key:value}};
        NSMutableDictionary *request = [command mutableCopy];
        request[@"id"] = NSUUID.UUID.UUIDString;
        handleLiveCommand(request);
    };
    static dispatch_source_t timer;
    timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    stopWithGame(timer);
    dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, NSEC_PER_SEC, NSEC_PER_SEC / 8);
    dispatch_source_set_event_handler(timer, ^{
        if (atomic_load(&gameEnding)) return;
        static bool bound;
        static void *timeClass;
        static NSTimeInterval renderCheck;
        static BOOL hadScene, installedMenu;
        NSMutableDictionary *state = [@{@"pid":@(getpid())} mutableCopy];
        if (!playerDisplayLinkRunning()) {
            state[@"waiting"] = @"等待 Unity 游戏显示链开始刷新"; writeStatus(state); return;
        }
        if (!bound) bound = bindUnity();
        if (!bound || !domainGet() || !threadCurrent()) {
            state[@"waiting"] = @"等待 Unity 运行时"; writeStatus(state); return;
        }
        if (!timeClass) timeClass = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Time");
        bool ok;
        int frameCount = [integerValue(callClass(timeClass, NULL, "get_frameCount", 0, NULL, &ok)) intValue];
        if (frameCount < 30) {
            state[@"waiting"] = @"等待 Unity 开始渲染"; writeStatus(state); return;
        }
        state[@"ready"] = @YES;
        BOOL inScene = input172PlayableScene(hadScene);
        NSMutableDictionary *inputSettings = [selected mutableCopy];
        inputSettings[@"inputRoute"] = @"native";
        updateInput172(inputSettings, state, inScene);
        readLiveCommand();
        backgroundFPS(inScene, [selected[@"backgroundLimit"] boolValue],
                      NSRunningApplication.currentApplication.active, state);
        state[@"sceneReady"] = @(inScene);
        state[@"nativeLighting172"] = sceneLightingPath172(inScene);
        newPlayableScene = inScene && !hadScene;
        hadScene = inScene;
        BOOL shadow = !wantsBaseline && [selected[@"selfShadow"] boolValue];
        BOOL compareResolution = wantsBaseline && [selected[@"compareRendering"] boolValue];
        NSDictionary *sceneEffects=updateScene172(selected,inScene,wantsBaseline);
        BOOL effectsReady=[sceneEffects[@"ready"] boolValue] || [sceneEffects[@"retentionReady"] boolValue];
        if(effectsReady!=sceneResourcesReady172){sceneResourcesReady172=effectsReady;needsApply=YES;}
        state[@"sceneLighting172"]=sceneEffects;
        NSMutableDictionary *lightState=[state[@"nativeLighting172"] mutableCopy];
        NSMutableDictionary *lightFeatures=[lightState[@"features"] mutableCopy] ?: [NSMutableDictionary dictionary];
        [lightFeatures addEntriesFromDictionary:sceneEffects[@"features"]];
        lightState[@"features"]=lightFeatures;state[@"nativeLighting172"]=lightState;
        if (inScene) {
            NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
            if (newPlayableScene || needsApply || now >= renderCheck) {
                [renderState removeAllObjects];
                NSString *failure = updateRenderSettings(renderState);
                renderState[@"settingsPending"] = @(failure != nil && commandProblem == nil);
                renderState[@"settingsResult"] = commandProblem ?: failure ?: (confirmedBaseline ?
                    @"已切回游戏原画" : (changedInGame.count ? @"已保留游戏主动调整的参数" : @"画质参数已应用"));
                if (commandProblem) renderState[@"commandError"] = commandProblem;
                renderCheck = now + (failure ? 2 : 10);
                needsApply = NO;
            }
            [state addEntriesFromDictionary:renderState];
            NSNumber *limit = renderState[@"effective"][@"RenderTargetSettings"][@"BackBufferResolutionHeight"];
            state[@"output172"] = updateOutput172(selected[@"height"], YES,
                ![selected[@"height"] intValue] || [limit isEqual:selected[@"height"]]);
            state[@"metalFX"] = updateMetalFX172(compareResolution ? @"taau" : selectedUpscaler172(selected), frameCount);
            state[@"renderQuality"] = quality172(state[@"metalFX"], state[@"effective"], compareResolution);
            updateLighting172(shadow, state, frameCount);

        } else {
            state[@"settingsPending"] = @(!commandProblem);
            state[@"settingsResult"] = commandProblem ?: @"画质设置已保存，等待进入可玩场景";
            state[@"output172"] = updateOutput172(selected[@"height"], NO, NO);
            state[@"metalFX"] = @{@"requestedMode": selectedUpscaler172(selected), @"status":@"waitingForCamera"};
            updateLighting172(NO, state, frameCount);
            if (shadow) state[@"nativeSelfShadow"] = @{@"requested":@YES, @"volumeOverride":@NO, @"reason":@"等待角色场景"};
        }
        state[@"squadTips172"] = updateSquadTips172(inScene && input172Native);
        int skinMode = ([selected[@"characterLighting"] boolValue] ? 3 : 0) |
                       ([selected[@"characterSpecular"] boolValue] ? 4 : 0);
        state[@"character172"] = updateCharacter172(skinMode);
        if (inScene && wantsBaseline) {
            BOOL restoringFX = [state[@"metalFX"][@"status"] isEqual:@"restoreFailed"];
            BOOL measuredTAAU = [state[@"metalFX"][@"status"] isEqual:@"cameraEnabled"] &&
                [state[@"renderQuality"][@"status"] isEqual:@"verified"];
            state[@"comparisonRendering"] = @{@"requested":@(compareResolution), @"verified":@(compareResolution && measuredTAAU),
                @"preservedLOD":@(![selected[@"compareLOD"] boolValue]), @"preservedUpscaler":@(!compareResolution)};
            confirmedBaseline = ![state[@"settingsPending"] boolValue] && !restoringFX &&
                (!compareResolution || measuredTAAU);
            if (compareResolution && !measuredTAAU) {
                state[@"settingsPending"] = @YES;
                state[@"settingsResult"] = state[@"renderQuality"][@"reason"] ?: @"等待游戏 TAAU 和 50% 输入尺寸读回";
            }
            if (restoringFX) {
                state[@"settingsPending"] = @YES;
                state[@"settingsResult"] = @"正在恢复相机原有超分模式";
            }
        }
        if (inScene || installedMenu)
            installedMenu = updateMenu172(selected, state, wantsBaseline, menuAction);
        state[@"menuInstalled"] = @(installedMenu);
        state[@"menuHintShown"] = @(menu172HintShown);
        state[@"menuShortcut"] = @"⌘⌥P";
        writeStatus(state);
    });
    dispatch_resume(timer);
}
@end
