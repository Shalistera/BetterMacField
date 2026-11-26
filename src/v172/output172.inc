// Screen dimensions belong to Unity. Change them once per user selection and
// verify on a later game frame, keeping the existing display mode intact.
typedef struct {
    NSInteger height;
    int originalW,originalH,ownedW,ownedH,beforeW,beforeH,goalW,goalH;
    BOOL selected, pending, finished, manual, rejected, sawScene;
    NSTimeInterval started;
} BMFOutput172;

static NSDictionary *runOutput172(BMFOutput172 *s, int requested, BOOL scene, BOOL active, int w, int h,
                                  NSTimeInterval now, BOOL (^setSize)(int,int)) {
    NSDictionary *(^report)(NSString *, NSString *) = ^(NSString *status, NSString *message) {
        return @{@"status":status, @"message":message, @"height":@(requested),
                 @"actualWidth":@(w), @"actualHeight":@(h),
                 @"requestedWidth":@(requested ? requested*16/9 : s->originalW),
                 @"requestedHeight":@(requested ?: s->originalH)};
    };
    if (!scene) {
        s->sawScene = NO;
        return report(@"waitingForScene", @"进入角色场景后设置输出尺寸");
    }
    if (!s->sawScene) {
        s->sawScene = YES;
        return report(@"waitingForScene", @"等待角色场景完成首帧");
    }
    if (w <= 0 || h <= 0) return report(@"waitingForDrawable", @"等待游戏报告输出尺寸");
    if (!active) return report(@"background", @"游戏回到前台后核对输出尺寸");

    // Finish the outstanding request first, even if a newer selection arrived.
    // Unity schedules SetResolution on a later frame. A quick return to default
    // needs that acknowledgement to restore the correct pre-request dimensions.
    if (s->pending) {
        if (w == s->goalW && h == s->goalH) {
            s->ownedW = s->height ? w : 0;
            s->ownedH = s->height ? h : 0;
            if (!s->height) s->originalW = s->originalH = 0;
        } else if (w != s->beforeW || h != s->beforeH) {
            s->manual = YES;
        } else if (now-s->started >= 12) {
            s->rejected = YES;
        } else {
            return report(@"waitingForReadback", @"已请求输出尺寸，等待读回");
        }
        s->pending = NO;
        s->finished = YES;
    }
    if ((s->ownedW && (w != s->ownedW || h != s->ownedH)) ||
        (s->selected && s->height && s->finished && !s->rejected && (w != s->goalW || h != s->goalH))) {
        s->manual = YES;
    }
    if (s->manual) s->ownedW = s->ownedH = s->originalW = s->originalH = 0;
    if (!s->selected || s->height != requested) {
        s->selected = YES;
        s->height = requested;
        s->finished = s->manual = s->rejected = NO;
    }
    if (s->manual) return report(@"userSize", @"沿用手动调整后的游戏窗口尺寸");
    if (s->rejected) return report(@"notApplied", @"游戏保留了当前输出尺寸");
    if (s->finished) return report(requested ? @"applied" : @"gameDefault",
                                   requested ? @"游戏已确认输出尺寸" : @"沿用游戏输出尺寸");
    if (!requested && (!s->ownedW || !s->originalW)) {
        s->finished = YES;
        s->originalW = s->originalH = 0;
        return report(@"gameDefault", @"沿用游戏输出尺寸");
    }
    s->goalW = requested ? requested*16/9 : s->originalW;
    s->goalH = requested ?: s->originalH;
    if (w == s->goalW && h == s->goalH) {
        s->finished = YES;
        if (!requested) s->ownedW = s->ownedH = s->originalW = s->originalH = 0;
        return report(requested ? @"applied" : @"gameDefault",
                      requested ? @"游戏当前输出尺寸与选择一致" : @"沿用游戏输出尺寸");
    }
    if (!s->ownedW) {
        s->originalW = w;
        s->originalH = h;
    }
    s->beforeW = w;
    s->beforeH = h;
    if (!setSize(s->goalW, s->goalH)) {
        s->rejected = s->finished = YES;
        return report(@"unavailable", @"游戏未接受输出尺寸请求");
    }
    s->started = now;
    s->pending = YES;
    return report(@"waitingForReadback", @"已向游戏请求输出尺寸");
}

static NSDictionary *updateOutput172(NSNumber *height,BOOL inScene,BOOL limitRaised) {
    static BMFOutput172 session;
    int target=height.intValue;
    if(target!=0 && target!=720 && target!=1080 && target!=1440 && target!=2160)
        return @{@"status":@"invalid",@"message":@"未知的输出分辨率"};
    if(!inScene)return runOutput172(&session,target,NO,NO,0,0,0,nil);
    bool ok=false;void *screen=classNamed("UnityEngine.CoreModule.dll","UnityEngine","Screen");
    int w=[integerValue(callClass(screen,NULL,"get_width",0,NULL,&ok)) intValue];
    int h=[integerValue(callClass(screen,NULL,"get_height",0,NULL,&ok)) intValue];
    // Unity clamps a request made before the game's own size limit has been raised.
    if(!limitRaised)return @{@"status":@"waitingForLimit",@"message":@"等待游戏放开输出尺寸上限",@"height":@(target),
                             @"actualWidth":@(w),@"actualHeight":@(h)};
    return runOutput172(&session,target,YES,NSApp.isActive,w,h,NSProcessInfo.processInfo.systemUptime,^BOOL(int width,int newHeight){
        if(!screen || !lightingAPIs())return NO;
        bool success=false;void *mode=callClass(screen,NULL,"get_fullScreenMode",0,NULL,&success);
        if(!success || !mode || strcmp(className(objectClass(mode)),"FullScreenMode"))return NO;
        void *args[]={&width,&newHeight,unbox(mode)},*iterator=NULL;const void *method;
        while((method=lightMethods(screen,&iterator))){
            if(strcmp(lightMethodName(method),"SetResolution") || lightCount(method)!=3 ||
               !(methodFlags(method,NULL)&0x10) || !namedType(lightArg(method,0),"System.Int32") ||
               !namedType(lightArg(method,1),"System.Int32") || !namedType(lightArg(method,2),"UnityEngine.FullScreenMode"))continue;
            void *exception=NULL;invoke(method,NULL,args,&exception);return exception==NULL;
        }
        return NO;
    });
}
