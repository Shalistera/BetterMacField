// Unity object lifetime, array access and resource integrity helpers.
#import <CommonCrypto/CommonDigest.h>

@interface BMFUnityRef172 : NSObject
@property(nonatomic, readonly) uint32_t handle;
+ (instancetype)hold:(void *)object;
- (void *)target;
@end
@implementation BMFUnityRef172
+ (instancetype)hold:(void *)object {
    if (!object) return nil;
    BMFUnityRef172 *ref = [self new];
    ref->_handle = lightPin(object, false);
    return ref->_handle ? ref : nil;
}
- (void *)target { return _handle ? lightTarget(_handle) : NULL; }
- (void)dealloc { if (_handle) lightUnpin(_handle); }
@end

static void *wardClass172(const char *name) {
    return classNamed("UnityEngine.CoreModule.dll", "UnityEngine", name);
}

static void *wardGet172(void *obj, const char *name) {
    bool ok = false;
    void *value = call(obj, name, 0, NULL, &ok);
    return ok ? value : NULL;
}

static NSString *wardName172(void *obj) {
    bool ok = false;
    return textValue(callClass(wardClass172("Object"), obj, "get_name", 0, NULL, &ok));
}

static void *wardArrayClass172(void) {
    static void *cls;
    if (!cls) cls = lightingClass("System", "Array");
    return cls;
}

static int wardLength172(void *array) {
    bool ok = false;
    NSNumber *n = integerValue(callClass(wardArrayClass172(), array, "get_Length", 0, NULL, &ok));
    return ok && n ? n.intValue : -1;
}

static void *wardItem172(void *array, int i) {
    bool ok = false;
    void *args[] = {&i};
    return lightingCall(wardArrayClass172(), array, "GetValue", 1, "System.Int32", args, &ok);
}

static NSString *wardHash172(const uint8_t *bytes, size_t length) {
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    if (length > UINT32_MAX) return nil;
    CC_SHA256(bytes, (CC_LONG)length, digest);
    NSMutableString *result = [NSMutableString stringWithCapacity:64];
    for (size_t i = 0; i < sizeof(digest); ++i) [result appendFormat:@"%02x", digest[i]];
    return result;
}

static BOOL wardHex172(id s) {
    if (![s isKindOfClass:NSString.class] || [s length] != 64) return NO;
    NSCharacterSet *invalid = [NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"].invertedSet;
    return [s rangeOfCharacterFromSet:invalid].location == NSNotFound;
}

static bool unityAlive172(void *object) {
    if (!object) return false;
    bool ok = false; void *args[] = {object};
    void *alive = callClass(input172UnityObject, NULL, "op_Implicit", 1, args, &ok);
    return ok && [renderNumber(alive, @YES) boolValue];
}
