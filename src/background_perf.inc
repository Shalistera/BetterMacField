/* Limit Unity's whole update loop while the host application is inactive. */
static bool background_perf_limited;
static int background_perf_foreground_rate;

static void background_perf_tick(void) {
    // Login/loading uses a different cap. Save a gameplay cap only after the
    // scene exists; otherwise the first background launch can pin play to 30.
    if(!background_perf_limited && !input_scene_is_ready())return;
    if(NSApp.isActive && !background_perf_limited)return;
    void *application=find_class("UnityEngine","Application");
    const void *getter=application ? class_get_method_from_name(application,"get_targetFrameRate",0) : NULL;
    const void *setter=application ? typed_method(application,"set_targetFrameRate",1,"System.Int32") : NULL;
    if(!getter || !setter)return;
    void *exception=NULL,*value=runtime_invoke(getter,NULL,NULL,&exception);
    if(exception || !value)return;
    int current=*(int *)object_unbox(value);
    bool active=NSApp.isActive;
    int target=active ? background_perf_foreground_rate : 15;
    if(!active && !background_perf_limited){
        if(current>0 && current<=target)return;
        background_perf_foreground_rate=current;
    }
    if(current!=target){
        void *args[]={&target};
        exception=NULL;runtime_invoke(setter,NULL,args,&exception);
        if(exception){fprintf(output,"background_frame_rate=set_failed active=%d target=%d\n",active,target);return;}
        if(!active)background_perf_limited=true;
        exception=NULL;value=runtime_invoke(getter,NULL,NULL,&exception);
        if(exception || !value)return;
        int actual=*(int *)object_unbox(value);
        fprintf(output,"background_frame_rate=changed active=%d previous=%d requested=%d actual=%d foreground=%d\n",
            active,current,target,actual,background_perf_foreground_rate);
        if(actual!=target)return;
    }
    background_perf_limited=!active;
}
