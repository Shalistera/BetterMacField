static NSString *fxNativeProblem172;
static int fxNativeCameraID172;

static void fxNativeStop172(void) {
    native_temporal_configure(false,0,0);
    native172ConfigureSize(NO,NO);
    atomic_store(&fxNativeRequested172,false);
    fxNativeCameraID172=0;
}

static BOOL fxNativeConfigure172(void *camera,BOOL spatial,int instance) {
    NSArray *size=fxSize172(camera,"get_finalRTSize");
    fxNativeProblem172=nil;
    if(size.count!=2 || [size[0] intValue]<=0 || [size[1] intValue]<=0) {
        fxNativeProblem172=@"等待原生相机输出尺寸";fxNativeStop172();return NO;
    }
    if(!native_temporal_prepare172() || !native172InstallSize(camera)) {
        fxNativeProblem172=native172SizeState()[@"reason"] ?: @"原生渲染接口校验未通过";
        fxNativeStop172();return NO;
    }
    if(fxNativeCameraID172!=instance) {
        fxNativeCameraID172=instance;
        atomic_fetch_add(&native_temporal_generation,1);
    }
    atomic_store(&native_spatial_enabled,spatial);
    if(!native_temporal_configure(true,[size[0] unsignedIntValue],[size[1] unsignedIntValue])) {
        fxNativeProblem172=@"原生超分输出尺寸不受支持";fxNativeStop172();return NO;
    }
    native172ConfigureSize(YES,!spatial);
    atomic_store(&fxNativeRequested172,true);
    return YES;
}

static NSDictionary *fxNativeState172(void) {
    NSMutableDictionary *state=[native_temporal_state() mutableCopy];
    state[@"size"]=native172SizeState();
    if(fxNativeProblem172)state[@"reason"]=fxNativeProblem172;
    return state;
}
