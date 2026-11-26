/* Preserve the native lighting graph and replace its final scaling boundary. */
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <mach/mach.h>
#include "metalfx_history172.h"
static uint32_t (*fxRoot172)(void *, bool);
static uint32_t (*fxWeak172)(void *, bool);
static void *(*fxTarget172)(uint32_t);
static void (*fxFree172)(uint32_t);
static uint32_t fxCamera172;
static int fxOriginal172, fxApplied172, fxLastFrame172 = -1;
static BOOL fxYielded172;
static NSUInteger fxFailures172;
static atomic_bool fxNativeRequested172;
static BOOL fxNativeConfigure172(void *camera, BOOL spatial, int instance);
static void fxNativeStop172(void);
static NSDictionary *fxNativeState172(void);

static BOOL fxHandles172(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        fxRoot172 = dlsym(RTLD_DEFAULT, "il2cpp_gchandle_new");
        fxWeak172 = dlsym(RTLD_DEFAULT, "il2cpp_gchandle_new_weakref");
        fxTarget172 = dlsym(RTLD_DEFAULT, "il2cpp_gchandle_get_target");
        fxFree172 = dlsym(RTLD_DEFAULT, "il2cpp_gchandle_free");
    });
    return fxRoot172 && fxWeak172 && fxTarget172 && fxFree172;
}

static void releaseFXHandle172(uint32_t *handle) {
    if (*handle) fxFree172(*handle);
    *handle = 0;
}

static NSNumber *fxMode172(void *camera) {
    bool ok;
    void *value = call(camera, "get_antialiasing", 0, NULL, &ok);
    if (!ok || !value || strcmp(className(objectClass(value)), "AntialiasingMode")) return nil;
    return @(*(const int32_t *)unbox(value));
}

static BOOL restoreFXCamera172(void) {
    if (!fxCamera172) return YES;
    void *camera = fxTarget172(fxCamera172);
    if (camera) {
        NSNumber *current = fxMode172(camera);
        if (!current) return NO;
        if (current.intValue == fxApplied172) {
            void *args[] = {&fxOriginal172};
            bool ok = false;
            call(camera, "set_antialiasing", 1, args, &ok);
            NSNumber *restored = fxMode172(camera);
            if (!ok || !restored || restored.intValue != fxOriginal172) return NO;
        }
    }
    releaseFXHandle172(&fxCamera172);
    return YES;
}

static NSArray<NSNumber *> *fxSize172(void *camera, const char *getter) {
    bool ok;
    void *value = call(camera, getter, 0, NULL, &ok);
    if (!ok || !value || strcmp(className(objectClass(value)), "Vector2Int")) return nil;
    const int32_t *size = unbox(value);
    if (size[0] <= 0 || size[1] <= 0) return nil;
    return @[@(size[0]), @(size[1])];
}

// The game's MetalFX passes exist only in its managed render graph. The engine
// publishes the active graph through one writable flag, read by this getter.
static bool *fxCppFlag172(void) {
    static bool *flag;
    static BOOL searched;
    if (searched) return flag;
    searched = YES;
    void *(*resolve)(const char *) = dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    const uint32_t *wrapper = resolve ? resolve("UnityEngine.HyperGryph.HGGraphicsUtils::get_enableCppRenderPath()") : NULL;
    // stp x29,x30,[sp,#-16]! ; mov x29,sp ; bl getter ; ldp x29,x30,[sp],#16 ; ret
    if (!wrapper || wrapper[0] != 0xa9bf7bfd || wrapper[1] != 0x910003fd || (wrapper[2] & 0xfc000000) != 0x94000000 ||
        wrapper[3] != 0xa8c17bfd || wrapper[4] != 0xd65f03c0) return NULL;
    const uint32_t *getter = wrapper + 2 + (((int32_t)(wrapper[2] << 6)) >> 6);
    // adrp x8, page ; ldrb w0, [x8, #offset] ; ret
    uint32_t adrp = getter[0], load = getter[1];
    if ((adrp & 0x9f00001f) != 0x90000008 || (load & 0xffc003ff) != 0x39400100 || getter[2] != 0xd65f03c0) return NULL;
    int64_t pages = ((int64_t)(((adrp >> 5) & 0x7ffff) << 2 | ((adrp >> 29) & 3)) << 43) >> 43;
    uint8_t *candidate = (uint8_t *)(((uintptr_t)getter & ~(uintptr_t)0xfff) + pages * 4096 + ((load >> 10) & 0xfff));
    Dl_info code, data;
    if (!dladdr(getter, &code) || !dladdr(candidate, &data) || code.dli_fbase != data.dli_fbase) return NULL;
    // Global 1.7.2 keeps this flag at one known place in UnityFramework's writable data.
    if ((uintptr_t)candidate - (uintptr_t)code.dli_fbase != 0x1562b369) return NULL;
    vm_address_t address = (vm_address_t)candidate;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (vm_region_64(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS ||
        address > (vm_address_t)candidate || !(info.protection & VM_PROT_WRITE) || *candidate > 1) return NULL;
    return flag = (bool *)candidate;
}

static BOOL fxOwnsCpp172, fxCppOriginal172;

static void restoreFXPath172(void) {
    bool *flag = fxCppFlag172();
    if (fxOwnsCpp172 && flag && *flag) *flag = fxCppOriginal172;
    fxOwnsCpp172 = NO;
}

// Count the scaler encodes the game actually submits.
static NSMutableArray *fxTraceRows172;
static NSMutableSet *fxTraceClasses172;
static NSUInteger fxTraceEncodes172[2];
static NSDictionary *fxTraceLast172[2];
// 1.7.2 ConstructPass (0xb32c588) supplies managed camera (+X,-Y).
// Paired GPU crops show the raster X displacement has the opposite sign;
// negate X here. Y is already converted by the game and stays unchanged.
static float fxTraceSign172[2] = {-1, 1}, fxTraceRaw172[2];
static char fxHistoryKey172;
static uint64_t fxHistoryGeneration172;
static int fxHistoryCamera172, fxHistoryMode172;
static BOOL fxHistoryEnabled172;
static NSUInteger fxHistoryResets172;
static unsigned fxHistoryLastReasons172;

static NSObject *fxHistoryLock172(void) {
    static NSObject *lock;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ lock = [NSObject new]; });
    return lock;
}

static void fxHistorySelect172(int camera, int mode, BOOL enabled) {
    @synchronized (fxHistoryLock172()) {
        if (camera != fxHistoryCamera172 || mode != fxHistoryMode172 || enabled != fxHistoryEnabled172)
            ++fxHistoryGeneration172;
        fxHistoryCamera172 = camera;
        fxHistoryMode172 = mode;
        fxHistoryEnabled172 = enabled;
    }
}

static unsigned fxHistoryPrepare172(id scaler) {
    @synchronized (fxHistoryLock172()) {
        if (!fxHistoryEnabled172) return 0;
        id<MTLTexture> color = [scaler valueForKey:@"colorTexture"];
        id<MTLTexture> output = [scaler valueForKey:@"outputTexture"];
        if (!color || !output) return 0;
        unsigned size[] = {(unsigned)color.width, (unsigned)color.height,
            (unsigned)output.width, (unsigned)output.height,
            [[scaler valueForKey:@"inputContentWidth"] unsignedIntValue],
            [[scaler valueForKey:@"inputContentHeight"] unsignedIntValue]};
        NSMutableData *storage = objc_getAssociatedObject(scaler, &fxHistoryKey172);
        if (!storage) {
            storage = [NSMutableData dataWithLength:sizeof(BMFFXHistory172)];
            objc_setAssociatedObject(scaler, &fxHistoryKey172, storage, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        unsigned reasons = fxHistoryAdvance172(storage.mutableBytes, fxHistoryGeneration172,
            NSProcessInfo.processInfo.systemUptime, size, fxTraceSign172);
        if (reasons) { ++fxHistoryResets172; fxHistoryLastReasons172 = reasons; }
        return reasons;
    }
}


static id fxTraceTexture172(id object, NSString *key) {
    id<MTLTexture> texture = [object respondsToSelector:NSSelectorFromString(key)] ? [object valueForKey:key] : nil;
    if (!texture) return NSNull.null;
    return @[@(texture.width), @(texture.height), @(texture.pixelFormat)];
}

static void fxTraceObserve172(id scaler, BOOL temporal, unsigned resetReasons, BOOL gameReset) {
    @synchronized (fxTraceRows172) {
        fxTraceEncodes172[temporal]++;
        if (fxTraceRows172.count >= 24) [fxTraceRows172 removeObjectAtIndex:0];
        NSMutableDictionary *row = [@{@"class":NSStringFromClass([scaler class]), @"time":@(NSProcessInfo.processInfo.systemUptime),
            @"color":fxTraceTexture172(scaler, @"colorTexture"), @"output":fxTraceTexture172(scaler, @"outputTexture")} mutableCopy];
        if (temporal) {
            row[@"reset"] = [scaler valueForKey:@"reset"];
            row[@"gameReset"] = @(gameReset);
            row[@"historyResetReasons"] = @(resetReasons);
            row[@"jitter"] = @[[scaler valueForKey:@"jitterOffsetX"], [scaler valueForKey:@"jitterOffsetY"]];
            row[@"rawJitter"] = @[@(fxTraceRaw172[0]), @(fxTraceRaw172[1])];
            row[@"motionScale"] = @[[scaler valueForKey:@"motionVectorScaleX"], [scaler valueForKey:@"motionVectorScaleY"]];
            row[@"content"] = @[[scaler valueForKey:@"inputContentWidth"], [scaler valueForKey:@"inputContentHeight"]];
            row[@"depthReversed"] = [scaler valueForKey:@"depthReversed"];
            row[@"depth"] = fxTraceTexture172(scaler, @"depthTexture");
            row[@"motion"] = fxTraceTexture172(scaler, @"motionTexture");
        }
        [fxTraceRows172 addObject:row];
        fxTraceLast172[temporal] = row;
    }
}

static NSArray *fxTraceInstall172(void) {
    if (!fxTraceRows172) { fxTraceRows172 = [NSMutableArray array]; fxTraceClasses172 = [NSMutableSet set]; }
    unsigned int classCount = 0;
    Class *classes = objc_copyClassList(&classCount);
    SEL encode = NSSelectorFromString(@"encodeToCommandBuffer:");
    SEL setters[2] = {NSSelectorFromString(@"setJitterOffsetX:"), NSSelectorFromString(@"setJitterOffsetY:")};
    for (unsigned int i = 0; i < classCount; i++) {
        Class cls = classes[i];
        const char *raw = class_getName(cls);
        BOOL temporal = strstr(raw, "Temporal") != NULL;
        if (!strstr(raw, "FX") || (!temporal && !strstr(raw, "Spatial")) || [fxTraceClasses172 containsObject:@(raw)]) continue;
        unsigned int count = 0;
        Method *methods = class_copyMethodList(cls, &count);
        BOOL hooked = NO;
        for (unsigned int j = 0; j < count; j++) {
            Method method = methods[j];
            SEL name = method_getName(method);
            IMP original = method_getImplementation(method);
            if (name == encode) {
                hooked = YES;
                method_setImplementation(method, imp_implementationWithBlock(^(id object, id buffer) {
                    BOOL gameReset = temporal && [[object valueForKey:@"reset"] boolValue];
                    unsigned reasons = temporal ? fxHistoryPrepare172(object) : 0;
                    BOOL override = reasons && !gameReset;
                    if (override) [object setValue:@YES forKey:@"reset"];
                    @try {
                        fxTraceObserve172(object, temporal, reasons, gameReset);
                        ((void (*)(id, SEL, id))original)(object, encode, buffer);
                    } @finally {
                        // A forced reset belongs to this encode, not every later frame.
                        if (override) [object setValue:@NO forKey:@"reset"];
                    }
                }));
            } else if (temporal && (name == setters[0] || name == setters[1])) {
                int axis = name == setters[1];
                method_setImplementation(method, imp_implementationWithBlock(^(id object, float value) {
                    fxTraceRaw172[axis] = value;
                    float sign = atomic_load(&fxNativeRequested172) ? 1.f : fxTraceSign172[axis];
                    ((void (*)(id, SEL, float))original)(object, name, value * sign);
                }));
            }
        }
        if (hooked) [fxTraceClasses172 addObject:@(raw)];
        free(methods);
    }
    free(classes);
    return fxTraceClasses172.allObjects;
}

static NSUInteger fxEncodes172(BOOL temporal) {
    @synchronized (fxTraceRows172) { return fxTraceEncodes172[temporal]; }
}

static NSDictionary *updateMetalFX172(NSString *requested, int frameCount) {
    static NSString *previous;
    static NSUInteger cameraChanges;
    static int lastChangeFrame = -1;
    static int observedCamera;
    BOOL temporal = [requested isEqualToString:@"metalfx-temporal"];
    BOOL spatial = [requested isEqualToString:@"metalfx-spatial"];
    BOOL taau = [requested isEqualToString:@"taau"];
    BOOL original = !requested || [requested isEqualToString:@"default"];
    NSMutableDictionary *state = [@{@"requestedMode": requested ?: @"default", @"enabled": @NO,
        @"status": @"off", @"controlErrors": @(fxFailures172)} mutableCopy];
    if (!original && !spatial && !temporal && !taau) { state[@"status"] = @"invalidMode"; return state; }
    if (original || ![requested isEqualToString:previous]) {
        fxNativeStop172();
        fxHistorySelect172(0, 0, NO);
        cameraChanges = 0; lastChangeFrame = -1;
        if (!restoreFXCamera172()) {
            state[@"status"] = @"restoreFailed";
            state[@"controlErrors"] = @(++fxFailures172); return state;
        }
        fxLastFrame172 = -1;
        fxYielded172 = NO;
        if (!temporal && !spatial) restoreFXPath172();
        previous = [requested copy];
        if (original) return state;
    }
    if (!fxHandles172()) { state[@"status"] = @"unavailable"; return state; }
    const char *supportGetter = temporal ? "get_supportsMetalFXTemporalScaler" : "get_supportsMetalFXSpatialScaler";
    const char *enabledGetter = "get_enableTAAU";
    const char *toggle = "ToggleTAAU";
    const int expected = 2;
    bool ok = false;
    void *info = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "SystemInfo");
    if (!taau) {
        void *support = callClass(info, NULL, supportGetter, 0, NULL, &ok);
        if (!ok || ![renderNumber(support, @YES) boolValue]) {
            state[@"status"] = @"unsupported"; return state;
        }
    }
    void *unityCamera = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    void *main = callClass(unityCamera, NULL, "get_main", 0, NULL, &ok);
    int multipass = 0;
    void *args[] = {main, &multipass};
    void *hg = classNamed("HG.RenderPipelines.Runtime.dll", "HG.Rendering.Runtime", "HGCamera");
    void *camera = main ? callClass(hg, NULL, "TryGet", 2, args, &ok) : NULL;
    if (!ok || !camera) {
        fxNativeStop172();
        fxHistorySelect172(0, 0, NO);
        state[@"status"] = @"waitingForCamera"; return state;
    }
    NSNumber *instance = integerValue(callClass(classNamed("UnityEngine.CoreModule.dll","UnityEngine","Object"),
                                               main,"GetInstanceID",0,NULL,&ok));
    if (ok && instance) state[@"cameraInstanceID"] = instance;
    if(instance && observedCamera!=instance.intValue){observedCamera=instance.intValue;fxYielded172=NO;}
    if (fxCamera172 && fxTarget172(fxCamera172) != camera) {
        if (!restoreFXCamera172()) {
            state[@"status"] = @"restoreFailed";
            state[@"controlErrors"] = @(++fxFailures172); return state;
        }
    }
    NSNumber *mode = fxMode172(camera);
    fxHistorySelect172(instance.intValue, mode.intValue, NO);
    state[@"observedAtFrame"] = @(frameCount);
    if (mode) state[@"modeBeforeRequest"] = mode;
    void *raw = call(camera, enabledGetter, 0, NULL, &ok);
    NSNumber *enabled = ok ? renderNumber(raw, @YES) : nil;
    if (!mode || !enabled) { state[@"status"] = @"unavailable"; return state; }
    if (fxCamera172 && mode.intValue != fxApplied172) {
        releaseFXHandle172(&fxCamera172);
        fxYielded172 = YES;
    }
    if(fxYielded172 && mode.intValue==expected)fxYielded172=NO;
    if (fxYielded172) { fxNativeStop172(); restoreFXPath172(); state[@"status"] = @"changedInGame"; return state; }
    if (!taau) {
        bool *cpp = fxCppFlag172();
        if (!cpp || !fxNativeConfigure172(camera,spatial,instance.intValue)) {
            state[@"status"] = @"unavailable";
            state[@"reason"] = fxNativeState172()[@"reason"] ?: @"原生光照超分入口未就绪";
            return state;
        }
        if (!fxOwnsCpp172) { fxCppOriginal172 = *cpp; fxOwnsCpp172 = YES; }
        *cpp = true;
    }
    if (mode.intValue != expected && !fxCamera172) {
        fxCamera172 = fxWeak172(camera, false);
        if (!fxCamera172) { state[@"status"] = @"unavailable"; return state; }
        fxOriginal172 = mode.intValue;
        fxApplied172 = expected;
        call(camera, toggle, 0, NULL, &ok);
        mode = fxMode172(camera);
        fxHistorySelect172(instance.intValue, mode.intValue, NO);
        if (!ok || !mode || mode.intValue != expected) {
            fxNativeStop172(); restoreFXPath172();
            if (mode) fxApplied172 = mode.intValue;
            restoreFXCamera172();
            state[@"status"] = @"requestFailed";
            state[@"controlErrors"] = @(++fxFailures172); return state;
        }
        fxLastFrame172 = frameCount;
        lastChangeFrame = frameCount; ++cameraChanges;
        raw = call(camera, enabledGetter, 0, NULL, &ok);
        enabled = ok ? renderNumber(raw, @YES) : nil;
        if (!enabled) {
            state[@"cameraMode"] = mode; state[@"status"] = @"unavailable";
            state[@"waitingFor"] = @"cameraReadback"; return state;
        }
    }
    if(!fxCamera172 && mode.intValue==expected) {
        fxCamera172=fxWeak172(camera,false);
        if(!fxCamera172){fxNativeStop172();state[@"status"]=@"unavailable";return state;}
        fxOriginal172=fxApplied172=expected;
    }
    state[@"cameraMode"] = mode;
    state[@"enabled"] = enabled;
    state[@"modeChanges"] = @(cameraChanges);
    if (lastChangeFrame >= 0) state[@"modeChangedAtFrame"] = @(lastChangeFrame);
    NSArray<NSNumber *> *input = fxSize172(camera, "get_sceneRTSize");
    NSArray<NSNumber *> *output = fxSize172(camera, "get_finalRTSize");
    NSArray<NSNumber *> *resolve = fxSize172(camera, "get_taauRTSize");
    if (input) state[@"sceneSize"] = input;
    if (output) state[@"finalSize"] = output;
    if (resolve) state[@"taauSize"] = resolve;
    NSWindow *window = NSApp.mainWindow ?: NSApp.keyWindow;
    if (window) {
        NSSize frameSize = window.frame.size, contentSize = window.contentView.bounds.size;
        NSSize backingSize = [window.contentView convertRectToBacking:window.contentView.bounds].size;
        state[@"window"] = @{@"frame":@[@(frameSize.width),@(frameSize.height)],
            @"content":@[@(contentSize.width),@(contentSize.height)],
            @"backing":@[@(backingSize.width),@(backingSize.height)], @"scale":@(window.backingScaleFactor)};
    }
    if (taau) {
        state[@"status"] = enabled.boolValue && mode.intValue == expected ? @"cameraEnabled" : @"waitingForCamera";
        return state;
    }
    BOOL larger = input && output && output[0].intValue >= input[0].intValue &&
        output[1].intValue >= input[1].intValue && ![input isEqual:output];
    void *managedField = lightingAPIs() ? lightField(objectClass(camera), "preivousEnableCppRenderPath") : NULL;
    bool nativePath = true;
    if (managedField) lightGet(camera, managedField, &nativePath);
    state[@"cppRenderPath"] = @(nativePath);
    state[@"native"] = fxNativeState172();
    // Rescan for the scaler classes until the game has created its scaler.
    static double nextScan;
    double now = NSProcessInfo.processInfo.systemUptime;
    NSDictionary *nativeState=state[@"native"];
    NSUInteger encodes = [nativeState[@"encodes"] unsignedIntegerValue];
    NSDictionary *lastEncode=nativeState[@"lastEncode"];
    BOOL encoding = encodes && lastEncode && now - [lastEncode[@"time"] doubleValue] < 2 &&
        [lastEncode[@"spatial"] boolValue]==spatial && [lastEncode[@"generation"] isEqual:nativeState[@"generation"]];
    if (now >= nextScan) { fxTraceInstall172(); nextScan = now + 4; }
    state[@"encodes"] = @(encodes);
    if (lastEncode) state[@"lastEncode"] = lastEncode;
    id encodedOutput = lastEncode[@"output"];
    BOOL outputMatches = [encodedOutput isKindOfClass:NSArray.class] && [encodedOutput count] >= 2 && output &&
        [encodedOutput[0] intValue] == output[0].intValue && [encodedOutput[1] intValue] == output[1].intValue;
    // Native request dimensions, rather than the managed camera's TAAU size,
    // determine the size of the post-process target receiving this output.
    NSDictionary *nativeSize = state[@"native"][@"size"];
    BOOL resolveMatches = spatial || [nativeSize[@"taauSize"] isEqual:output];
    state[@"outputSizeVerified"] = @(encoding && outputMatches && resolveMatches);
    @synchronized (fxHistoryLock172()) {
        state[@"historyResets"] = nativeState[@"historyResets"] ?: @0;
        state[@"historyGeneration"] = nativeState[@"generation"] ?: @0;
    }
    state[@"status"] = enabled.boolValue && nativePath && larger && encoding && outputMatches && resolveMatches ? @"active" : @"waitingForRender";
    if (![state[@"status"] isEqual:@"active"]) {
        if (!enabled.boolValue) state[@"waitingFor"] = @"camera";
        else if (!nativePath) state[@"waitingFor"] = @"renderPath";
        else if (!input || !output) state[@"waitingFor"] = @"dimensions";
        else if (!larger) state[@"waitingFor"] = @"upscale";
        else if (encoding && (!outputMatches || !resolveMatches)) state[@"waitingFor"] = @"outputSize";
        else state[@"waitingFor"] = @"encodes";
    }
    return state;
}
