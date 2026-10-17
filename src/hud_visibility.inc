/* Keep PC squad/skill HUD visible through the existing fade-request API. */
static NSMutableArray<NSNumber *> *input_hud_fade_handles;
static NSMutableDictionary<NSString *,NSDictionary *> *input_hud_roots;

static void input_hud_visibility_reset(void) {
    for(NSNumber *handle in input_hud_fade_handles){
        bool show=false,ok=false;void *args[]={string_new("native_keyboard_hud"),&show};
        void *controller=gchandle_get_target(handle.unsignedIntValue);
        if(input_unity_alive(controller))invoke(controller,"SetLocalNeedShow",2,args,&ok);
        gchandle_free(handle.unsignedIntValue);
    }
    input_hud_fade_handles=nil;
    for(NSDictionary *entry in input_hud_roots.allValues){
        gchandle_free([entry[@"root"] unsignedIntValue]);
        gchandle_free([entry[@"controllers"] unsignedIntValue]);
    }
    input_hud_roots=nil;
}

static void input_hud_visibility_update(void) {
    void *keyboard=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!input_watch_fit_enabled || !keyboard || !*(bool *)object_unbox(keyboard)){input_hud_visibility_reset();return;}
    void *klass=find_class("Beyond.UI","UIHudFadeController");
    if(!input_hud_fade_handles)input_hud_fade_handles=[NSMutableArray array];
    if(!input_hud_roots)input_hud_roots=[NSMutableDictionary dictionary];
    for(NSNumber *handle in [input_hud_fade_handles copy]){
        if(input_unity_alive(gchandle_get_target(handle.unsignedIntValue)))continue;
        gchandle_free(handle.unsignedIntValue);[input_hud_fade_handles removeObject:handle];
    }
    // Discover only the two authored HUD subtrees, then reuse their controllers.
    // Resources.FindObjectsOfTypeAll scanned the entire client twice a second.
    for(NSString *name in @[@"BattleActionPanel",@"SquadIconPanel"]){
        NSDictionary *entry=input_hud_roots[name];
        if(entry && !input_unity_alive(gchandle_get_target([entry[@"root"] unsignedIntValue]))){
            gchandle_free([entry[@"root"] unsignedIntValue]);
            gchandle_free([entry[@"controllers"] unsignedIntValue]);
            [input_hud_roots removeObjectForKey:name];entry=nil;
        }
        if(!entry){
            void *findArgs[]={string_new(name.UTF8String)};
            void *root=input_static(find_class("UnityEngine","GameObject"),"Find",1,findArgs);
            if(!root)continue;
            bool inactive=true,ok=false;void *args[]={type_get_object(class_get_type(klass)),&inactive};
            void *controllers=invoke_typed(root,"GetComponentsInChildren",2,"System.Type",args,&ok);
            if(!ok || !controllers)continue;
            entry=@{@"root":@(gchandle_new(root,false)),@"controllers":@(gchandle_new(controllers,false))};
            input_hud_roots[name]=entry;
        }
        void *array=gchandle_get_target([entry[@"controllers"] unsignedIntValue]);
        for(uintptr_t i=0;i<array_length(array);i++){
            void *controller=array_item(array,i);bool ok=false,known=false;
            for(NSNumber *handle in input_hud_fade_handles)if(gchandle_get_target(handle.unsignedIntValue)==controller){known=true;break;}
            if(known || !input_unity_alive(controller))continue;
            void *active=invoke(controller,"get_isActiveAndEnabled",0,NULL,&ok);
            if(!ok || !active || !*(bool *)object_unbox(active))continue;
            bool show=true;void *showArgs[]={string_new("native_keyboard_hud"),&show};
            invoke(controller,"SetLocalNeedShow",2,showArgs,&ok);
            if(ok){[input_hud_fade_handles addObject:@(gchandle_new(controller,false))];fprintf(output,"input_hud_keep_visible=%s\n",object_name(controller).UTF8String);}
        }
    }
}
