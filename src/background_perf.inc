/* Foreground override and inactive throttling share one saved gameplay cap. */
static bool background_perf_limited;
static int background_perf_foreground_rate;
static bool background_perf_overridden;
static bool frame_rate_link_saved;
static CAFrameRateRange frame_rate_original_range;
static NSInteger frame_rate_original_preferred;
static id frame_rate_display_link(void) {
    id app=[NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
    id controller=[app valueForKey:@"delegate"];
    Ivar field=class_getInstanceVariable([controller class],"_displayLink");
    return field ? object_getIvar(controller,field) : nil;
}
static void frame_rate_link_apply(int target,bool enabled) {
    id link=frame_rate_display_link();
    if(!link)return;
    CAFrameRateRange current=((CAFrameRateRange (*)(id,SEL))[link methodForSelector:@selector(preferredFrameRateRange)])(link,@selector(preferredFrameRateRange));
    NSInteger preferred=[[link valueForKey:@"preferredFramesPerSecond"] integerValue];
    if(enabled && !frame_rate_link_saved){
        frame_rate_original_range=current;frame_rate_original_preferred=preferred;frame_rate_link_saved=true;
        fprintf(output,"frame_link=captured preferred=%ld range=%g/%g/%g\n",(long)preferred,current.minimum,current.maximum,current.preferred);
    }
    if(!enabled && !frame_rate_link_saved)return;
    CAFrameRateRange range=enabled ? CAFrameRateRangeMake(target,target,target) : frame_rate_original_range;
    NSInteger fps=enabled ? target : frame_rate_original_preferred;
    if(preferred!=fps)[link setValue:@(fps) forKey:@"preferredFramesPerSecond"];
    if(current.minimum!=range.minimum || current.maximum!=range.maximum || current.preferred!=range.preferred)
        ((void (*)(id,SEL,CAFrameRateRange))[link methodForSelector:@selector(setPreferredFrameRateRange:)])(link,@selector(setPreferredFrameRateRange:),range);
    if(!enabled)frame_rate_link_saved=false;
}
static NSDictionary *frame_rate_state(void) {
    void *value=input_static(find_class("UnityEngine","Application"),"get_targetFrameRate",0,NULL);
    void *frames=input_static(find_class("UnityEngine","Time"),"get_renderedFrameCount",0,NULL);
    id screen=[NSClassFromString(@"UIScreen") valueForKey:@"mainScreen"];
    id link=frame_rate_display_link();
    CAFrameRateRange range=link ? ((CAFrameRateRange (*)(id,SEL))[link methodForSelector:@selector(preferredFrameRateRange)])(link,@selector(preferredFrameRateRange)) : CAFrameRateRangeDefault;
    return @{@"requested":bmf_settings[@"frameRate"] ?: @0,
        @"target":value ? @(*(int *)object_unbox(value)) : @0,
        @"renderedFrames":frames ? @(*(int *)object_unbox(frames)) : @0,
        @"screenMaximum":[screen valueForKey:@"maximumFramesPerSecond"] ?: @0,
        @"displayLink":link ? NSStringFromClass([link class]) : @"unavailable",
        @"preferredFramesPerSecond":link ? [link valueForKey:@"preferredFramesPerSecond"] : @0,
        @"preferredRange":@[@(range.minimum),@(range.maximum),@(range.preferred)],
        @"active":@(NSApp.isActive),@"backgroundLimited":@(background_perf_limited)};
}

static void background_perf_tick(void) {
    int requested=[bmf_settings[@"frameRate"] intValue];
    bool override=requested==60 || requested==120;
    if(!bmf_background_limit && !background_perf_limited && !override && !background_perf_overridden)return;
    // Login/loading uses a different cap. Save a gameplay cap only after the
    // scene exists; otherwise the first background launch can pin play to 30.
    if(!background_perf_limited && !background_perf_overridden && !input_scene_is_ready())return;
    if((NSApp.isActive || !bmf_background_limit) && !background_perf_limited && !override && !background_perf_overridden)return;
    void *application=find_class("UnityEngine","Application");
    const void *getter=application ? class_get_method_from_name(application,"get_targetFrameRate",0) : NULL;
    const void *setter=application ? typed_method(application,"set_targetFrameRate",1,"System.Int32") : NULL;
    if(!getter || !setter)return;
    void *exception=NULL,*value=runtime_invoke(getter,NULL,NULL,&exception);
    if(exception || !value)return;
    int current=*(int *)object_unbox(value);
    bool active=NSApp.isActive || !bmf_background_limit;
    if(!background_perf_limited && !background_perf_overridden){
        background_perf_foreground_rate=current;
    }
    int target=active ? (override ? requested : background_perf_foreground_rate) : 15;
    // Keep the throttled link until focus returns, even if the user switches
    // back to the game's cap while inactive.
    frame_rate_link_apply(target,override || (!active && frame_rate_link_saved));
    if(current!=target){
        void *args[]={&target};
        exception=NULL;runtime_invoke(setter,NULL,args,&exception);
        if(exception){fprintf(output,"background_frame_rate=set_failed active=%d target=%d\n",active,target);return;}
        background_perf_limited=!active;
        background_perf_overridden=override;
        exception=NULL;value=runtime_invoke(getter,NULL,NULL,&exception);
        if(exception || !value)return;
        int actual=*(int *)object_unbox(value);
        fprintf(output,"background_frame_rate=changed active=%d previous=%d requested=%d actual=%d foreground=%d\n",
            active,current,target,actual,background_perf_foreground_rate);
        if(actual!=target)return;
    }
    background_perf_limited=!active;
    background_perf_overridden=override;
}
