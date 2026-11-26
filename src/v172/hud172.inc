// The PC squad prefab's warning list is skipped by Lua on mobile builds, which
// leaves its empty template on screen. Show the current warnings in its place.
static BMFUnityRef172 *squadTip172;
static BOOL squadTipShown172;
static NSMutableArray<BMFUnityRef172 *> *squadCells172;
static NSArray<NSString *> *squadTexts172;

static void squadTipsReset172(void) {
    bool ok = false;
    for (BMFUnityRef172 *cell in squadCells172) {
        void *object = [cell target], *args[] = {object};
        if (unityAlive172(object)) callClass(wardClass172("Object"), NULL, "Destroy", 1, args, &ok);
    }
    void *tip = [squadTip172 target];
    if (unityAlive172(tip)) {
        bool shown = squadTipShown172;
        void *args[] = {&shown};
        call(tip, "SetActive", 1, args, &ok);
    }
    squadCells172 = nil; squadTexts172 = nil; squadTip172 = nil;
}

static NSDictionary *updateSquadTips172(BOOL enabled) {
    static NSTimeInterval next;
    static NSDictionary *last;
    bool ok = false;
    if (!enabled || ![renderNumber(callClass(input172Device, NULL, "get_usingKeyboard", 0, NULL, &ok), @YES) boolValue]) {
        if (squadTip172) squadTipsReset172();
        next = 0;
        return last = @{@"status":@"off"};
    }
    NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    if (now < next) return last;
    next = now + 0.5;
    void *tip = [squadTip172 target];
    if (!unityAlive172(tip)) {
        if (squadTip172) squadTipsReset172();
        void *name[] = {newString("SquadIconPanel")}, *path[] = {newString("Main/DisableLayout/DisableTips")};
        void *panel = callClass(wardClass172("GameObject"), NULL, "Find", 1, name, &ok);
        tip = wardGet172(call(wardGet172(panel, "get_transform"), "Find", 1, path, &ok), "get_gameObject");
        NSNumber *shown = renderNumber(wardGet172(tip, "get_activeSelf"), @YES);
        if (!shown) { next = now + 2; return last = @{@"status":@"waitingForPanel"}; }
        squadTipShown172 = shown.boolValue;
        squadTip172 = [BMFUnityRef172 hold:tip];
        squadCells172 = [NSMutableArray array];
    }
    void *world = lightingClass("Beyond.Gameplay.Core", "GameWorld");
    void *tips = wardGet172(input172Member(world, NULL, "battle"), "get_squadDisableTips");
    NSNumber *count = integerValue(wardGet172(tips, "get_Count"));
    if (!count || count.intValue < 0 || count.intValue > 16) return last = @{@"status":@"unavailable"};
    NSMutableArray *ids = [NSMutableArray array];
    for (int i = 0; i < count.intValue; i++) {
        void *args[] = {&i};
        NSString *identifier = textValue(call(tips, "get_Item", 1, args, &ok));
        if (!identifier) return last = @{@"status":@"unavailable"};
        [ids addObject:identifier];
    }
    if ([squadTexts172 isEqual:ids]) return last;
    bool hidden = false;
    void *hide[] = {&hidden};
    call(tip, "SetActive", 1, hide, &ok);
    void *parent = wardGet172(wardGet172(tip, "get_transform"), "get_parent");
    void *textClass = lightingClass("Beyond.UI", "UIText");
    if (!ok || !parent || !textClass) return last = @{@"status":@"unavailable"};
    while (squadCells172.count < ids.count) {
        void *args[] = {tip, parent};
        BMFUnityRef172 *cell = [BMFUnityRef172 hold:
            lightingCall(wardClass172("Object"), NULL, "Instantiate", 2, "UnityEngine.Object", args, &ok)];
        if (!ok || !cell) return last = @{@"status":@"unavailable"};
        [squadCells172 addObject:cell];
    }
    for (NSUInteger i = 0; i < squadCells172.count; i++) {
        void *cell = [squadCells172[i] target];
        bool show = i < ids.count;
        void *shown[] = {&show};
        call(cell, "SetActive", 1, shown, &ok);
        if (!show) continue;
        void *transform = wardGet172(cell, "get_transform");
        void *path[] = {newString("Ani/DisableTxt")}, *type[] = {lightTypeObject(lightType(textClass))};
        void *text = lightingCall(wardClass172("Component"), call(transform, "Find", 1, path, &ok),
                                  "GetComponent", 1, "System.Type", type, &ok);
        void *key[] = {newString([ids[i] UTF8String])};
        void *words[] = {lightingCall(lightingClass("Beyond.I18n", "I18nUtils"), NULL, "GetText", 1, "System.String", key, &ok)};
        if (!text || !words[0]) return last = @{@"status":@"unavailable"};
        call(text, "set_text", 1, words, &ok);
        call(transform, "SetAsFirstSibling", 0, NULL, &ok);
    }
    squadTexts172 = [ids copy];
    return last = @{@"status":@"active", @"warnings":@(ids.count)};
}
