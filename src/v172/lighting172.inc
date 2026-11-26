// Own a single native shadow Volume.
static void *(*lightNew)(void *);
static const void *(*lightType)(void *);
static void *(*lightTypeObject)(const void *);
static void *(*lightField)(void *, const char *);
static void (*lightGet)(void *, void *, void *);
static void (*lightSet)(void *, void *, void *);
static void (*lightSetRef)(void *, void *, void *);
static const void *(*lightFieldType)(void *);
static const void *(*lightMethods)(void *, void **);
static const char *(*lightMethodName)(const void *);
static uint32_t (*lightCount)(const void *);
static const void *(*lightArg)(const void *, uint32_t);
static char *(*lightTypeName)(const void *);
static void (*lightFree)(void *);
static uint32_t (*lightPin)(void *, bool);
static void *(*lightTarget)(uint32_t);
static void (*lightUnpin)(uint32_t);

static BOOL lightingAPIs(void) {
    static BOOL bound;
    if (bound) return YES;
#define LIGHT(var, name) if (!(var = dlsym(RTLD_DEFAULT, "il2cpp_" #name))) return NO;
    LIGHT(lightNew, object_new) LIGHT(lightType, class_get_type) LIGHT(lightTypeObject, type_get_object)
    LIGHT(lightField, class_get_field_from_name) LIGHT(lightGet, field_get_value)
    LIGHT(lightSet, field_set_value) LIGHT(lightSetRef, field_set_value_object) LIGHT(lightFieldType, field_get_type)
    LIGHT(lightMethods, class_get_methods) LIGHT(lightMethodName, method_get_name) LIGHT(lightCount, method_get_param_count)
    LIGHT(lightArg, method_get_param) LIGHT(lightTypeName, type_get_name) LIGHT(lightFree, free)
    LIGHT(lightPin, gchandle_new) LIGHT(lightTarget, gchandle_get_target) LIGHT(lightUnpin, gchandle_free)
#undef LIGHT
    return bound = YES;
}

static void *lightingClass(const char *ns, const char *name) {
    size_t n = 0;
    const void **list = assemblies(domainGet(), &n);
    for (size_t i = 0; i < n; i++) {
        void *klass = lookupClass(assemblyImage(list[i]), ns, name);
        if (klass) return klass;
    }
    return NULL;
}

static bool namedType(const void *type, const char *expected) {
    if (!type) return false;
    char *name = lightTypeName(type);
    bool match = name && !strcmp(name, expected);
    if (name) lightFree(name);
    return match;
}

static void *lightingCall(void *cls, void *receiver, const char *name, int count,
                          const char *firstType, void **args, bool *ok) {
    void *iterator = NULL;
    const void *method;
    *ok = false;
    if (!cls) return NULL;
    while ((method = lightMethods(cls, &iterator))) {
        if (strcmp(name, lightMethodName(method)) || lightCount(method) != (uint32_t)count ||
            !namedType(lightArg(method, 0), firstType)) continue;
        void *exception = NULL;
        void *result = invoke(method, receiver, args, &exception);
        *ok = exception == NULL;
        return result;
    }
    return NULL;
}

static void *lightTypedField(void *obj, const char *name, const char *type) {
    void *field = obj ? lightField(objectClass(obj), name) : NULL;
    return field && namedType(lightFieldType(field), type) ? field : NULL;
}

static void destroyLightingObject(void *obj) {
    if (!obj) return;
    bool ok;
    void *args[] = {obj};
    callClass(classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Object"), NULL, "Destroy", 1, args, &ok);
}

static NSString *createShadowProfile(uint32_t *goHandle, uint32_t *profileHandle, uint32_t *componentHandle) {
    void *goClass = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    void *scriptable = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "ScriptableObject");
    void *volumeClass = lightingClass("UnityEngine.Rendering", "Volume");
    void *profileClass = lightingClass("UnityEngine.Rendering", "VolumeProfile");
    void *componentClass = lightingClass("HG.Rendering.Runtime", "HGCharacterVolume");
    if (!goClass || !scriptable || !volumeClass || !profileClass || !componentClass) return @"原生自阴影组件未就绪";
    bool ok;
    void *typeArgs[] = {lightTypeObject(lightType(profileClass))};
    void *profile = lightingCall(scriptable, NULL, "CreateInstance", 1, "System.Type", typeArgs, &ok);
    if (!ok || !profile) return @"自阴影 VolumeProfile 创建失败";
    void *go = lightNew(goClass);
    void *name[] = {newString("BetterMacField character shadows")};
    void *component = NULL;
    if (go) lightingCall(goClass, go, ".ctor", 1, "System.String", name, &ok);
    if (go && ok) {
        void *vType[] = {lightTypeObject(lightType(volumeClass))};
        void *v = lightingCall(goClass, go, "AddComponent", 1, "System.Type", vType, &ok);
        void *priority = lightTypedField(v, "priority", "System.Single");
        void *weight = lightTypedField(v, "weight", "System.Single");
        void *shared = lightTypedField(v, "sharedProfile", "UnityEngine.Rendering.VolumeProfile");
        if (ok && priority && weight && shared) {
            bool yes = true, no = false;
            float p = 100000.f, w = 1.f;
            void *on[] = {&yes}, *profileArgs[] = {profile};
            call(v, "set_isGlobal", 1, on, &ok);
            lightSet(v, priority, &p); lightSet(v, weight, &w); lightSetRef(v, shared, profile);
            if (ok) call(v, "SetInstantiatedProfile", 1, profileArgs, &ok);
            void *args[] = {lightTypeObject(lightType(componentClass)), &no};
            if (ok) component = lightingCall(profileClass, profile, "Add", 2, "System.Type", args, &ok);
            void *field = component ? lightField(componentClass, "charSelfShadowQualityMode") : NULL;
            void *param = NULL;
            if (field) lightGet(component, field, &param);
            void *override = lightTypedField(param, "overrideState", "System.Boolean");
            if (ok && override) {
                int tier = 0; // HGCharacterVolume.CharacterRenderFeatureQualityTier.AlwaysOn
                void *value[] = {&tier};
                call(param, "set_value", 1, value, &ok);
                if (ok) lightSet(param, override, &yes);
                bool flag = false;
                lightGet(param, override, &flag);
                void *readback = call(param, "get_value", 0, NULL, &ok);
                if (ok && flag && readback &&
                    !strcmp(className(objectClass(readback)), "CharacterRenderFeatureQualityTier") &&
                    *(int32_t *)unbox(readback) == tier) {
                    call(v, "Play", 0, NULL, &ok);
                    void *persist[] = {go};
                    if (ok) callClass(classNamed("UnityEngine.CoreModule.dll","UnityEngine","Object"), NULL,
                                      "DontDestroyOnLoad", 1, persist, &ok);
                    if (ok) {
                        *goHandle = lightPin(go, false); *profileHandle = lightPin(profile, false);
                        *componentHandle = lightPin(component, false);
                        return nil;
                    }
                }
            }
        }
    }
    destroyLightingObject(go);
    destroyLightingObject(profile);
    return @"自阴影原生 Volume 参数创建或读回失败";
}

static void *lightingCamera172(void) {
    bool ok;
    void *main = callClass(classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Camera"),
                           NULL, "get_main", 0, NULL, &ok);
    if (!main) return NULL;
    int multipass = 0;
    void *args[] = {main, &multipass};
    return callClass(classNamed("HG.RenderPipelines.Runtime.dll", "HG.Rendering.Runtime", "HGCamera"),
                     NULL, "TryGet", 2, args, &ok);
}

static NSDictionary *sceneLightingPath172(BOOL scene) {
    static NSTimeInterval next;
    static NSDictionary *last;
    if (!scene) { next = 0; return @{@"status":@"waitingForScene"}; }
    NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    if (now < next && last) return last;
    next = now + 4;
    bool ok = false;
    void *camera = lightingCamera172();
    void *path = call(camera, "get_renderPathInstance", 0, NULL, &ok);
    if (!ok || !path) return last = @{@"status":@"waitingForRenderPath"};
    NSString *name = @(className(objectClass(path)));
    BOOL full = [name isEqual:@"HGRenderPathDefaultDeferred"], one = [name isEqual:@"HGRenderPathOnePassDeferred"];
    NSNumber *mobile = renderNumber(call(renderHub(),"get_isMobileShader",0,NULL,&ok),@YES);
    NSMutableDictionary *features = [NSMutableDictionary dictionary];
    NSDictionary *titles = @{@"gtao":@"GTAO", @"contact":@"接触阴影", @"volumetricFog":@"体积雾", @"ssr":@"SSR"};
    for (NSString *key in titles) {
        BOOL ssr = [key isEqual:@"ssr"];
        NSString *status = full ? @"compositionUnverified" : (one && !ssr ? @"passAbsent" : @"unsupported");
        NSString *reason;
        if (one && !ssr) reason = [NSString stringWithFormat:@"移动单通道渲染路径缺少%@通道",titles[key]];
        else if (ssr) reason = @"SSR 缺少已验证的场景法线、粗糙度和反射合成链";
        else if (full) reason = [NSString stringWithFormat:@"完整延迟路径包含%@通道，当前 iOS 场景光照的合成尚未验证", titles[key]];
        else reason = @"当前渲染路径的输入和合成尚未验证";
        features[key] = @{@"status":status, @"reason":reason, @"available":@NO};
    }
    return last = @{@"status":@"observed", @"renderPath":name, @"mobileShader":mobile ?: NSNull.null, @"features":features};
}

static NSNumber *sceneShadowGate(void) {
    bool ok;
    void *camera = lightingCamera172();
    void *stack = call(camera, "get_volumeStack", 0, NULL, &ok);
    void *klass = classNamed("HG.RenderPipelines.Runtime.dll", "HG.Rendering.Runtime", "HGCharacterVolume");
    if (!stack || !klass) return nil;
    void *type[] = {lightTypeObject(lightType(klass))};
    void *volume = lightingCall(objectClass(stack), stack, "GetComponent", 1, "System.Type", type, &ok);
    return renderNumber(call(volume, "GetCharShadowPassEnableState", 0, NULL, &ok), @YES);
}

static void updateLighting172(BOOL shadow, NSMutableDictionary *state, int frame) {
    static BOOL bound;
    static uint32_t go, profile, component;
    static NSTimeInterval retry;
    static NSString *problem;
    if (!shadow && !go) {
        state[@"nativeSelfShadow"] = @{@"requested":@NO, @"volumeOverride":@NO};
        return;
    }
    if (!bound) bound = lightingAPIs();
    if (!bound) { state[@"nativeLightingError"] = @"Unity 光照接口不可用"; return; }
    if (shadow && !go && NSProcessInfo.processInfo.systemUptime >= retry) {
        problem = createShadowProfile(&go, &profile, &component);
        retry = NSProcessInfo.processInfo.systemUptime + 10;
    }
    if (!shadow) {
        if (go) {
            destroyLightingObject(lightTarget(go)); destroyLightingObject(lightTarget(profile));
            lightUnpin(go); lightUnpin(profile); lightUnpin(component);
            go = profile = component = 0;
        }
        retry = 0; problem = nil;
    }
    NSMutableDictionary *report = [@{@"requested":@(shadow), @"volumeOverride":@(go != 0)} mutableCopy];
    if (problem) report[@"reason"] = problem;
    if (component) {
        bool ok;
        NSNumber *gate = renderNumber(call(lightTarget(component), "GetCharShadowPassEnableState", 0, NULL, &ok), @YES);
        if (gate) report[@"volumeGate"] = gate;
        NSNumber *effective = sceneShadowGate();
        if (effective) report[@"sceneVolumeGate"] = effective;
    }
    state[@"nativeSelfShadow"] = report;
}
