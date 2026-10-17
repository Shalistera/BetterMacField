/* Forward keys whose iOS backend loses or delays physical edges. */
static const unsigned short input_hud_native_keys[]={0x32,0x60,0x61,0x62,0x64,0x65,0x0e,0x38,0x3c};
static const char *input_hud_key_names[]={"BackQuote","F5","F6","F7","F8","F9","E","LeftShift","RightShift"};
enum { InputHudKeyCount=sizeof(input_hud_native_keys)/sizeof(input_hud_native_keys[0]) };
static int input_hud_game_keys[InputHudKeyCount],input_hud_timings[3];
static bool input_hud_keys_ready,input_hud_keys_held[InputHudKeyCount];
static double input_hud_key_pressed_at[InputHudKeyCount];
static NSEventModifierFlags input_hud_key_modifiers[InputHudKeyCount];
static NSUInteger input_hud_key_events,input_hud_key_actions;
static NSString *input_hud_last_action;

static void input_hud_keys_init(void) {
    input_hud_keys_ready=false;memset(input_hud_keys_held,0,sizeof(input_hud_keys_held));
    void *keys=find_class("Beyond.Input","KeyboardKeyCode"),*timings=find_class("Beyond.Input","InputTimingType");
    for(int i=0;i<InputHudKeyCount;i++){
        void *field=keys ? class_get_field_from_name(keys,input_hud_key_names[i]) : NULL;
        if(!field)return;
        field_static_get_value(field,&input_hud_game_keys[i]);
    }
    const char *names[]={"OnPress","OnRelease","OnClick"};
    for(int i=0;i<3;i++){
        void *field=timings ? class_get_field_from_name(timings,names[i]) : NULL;
        if(!field)return;
        field_static_get_value(field,&input_hud_timings[i]);
    }
    input_hud_keys_ready=true;
}

static bool input_hud_key_dispatch(int index,bool down,NSEventModifierFlags modifiers,double timestamp,bool cancelled) {
    input_hud_key_events++;
    bool wasHeld=input_hud_keys_held[index];
    if(!down)input_hud_keys_held[index]=false;
    if(down && (!NSApp.isActive || (modifiers&NSEventModifierFlagCommand)))return false;
    void *keyboard=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!keyboard || !*(bool *)object_unbox(keyboard))return false;
    if(!down && !wasHeld)return false;
    // A Shift key is the binding's primary key, not its own Shift modifier.
    if(input_hud_native_keys[index]==0x38 || input_hud_native_keys[index]==0x3c)modifiers&=~NSEventModifierFlagShift;
    if(down){input_hud_key_pressed_at[index]=timestamp;input_hud_key_modifiers[index]=modifiers;}
    else modifiers=input_hud_key_modifiers[index];

    void *manager=input_manager(),*bindings=field_reference(manager,"m_bindingInfos");bool ok=false,matched=false;
    void *iterator=invoke(bindings,"GetEnumerator",0,NULL,&ok);
    NSMutableArray *pending=[NSMutableArray array];NSMutableSet *actions=[NSMutableSet set];
    for(int i=0;iterator && i<2000;i++){
        void *moved=invoke(iterator,"MoveNext",0,NULL,&ok);if(!ok || !moved || !*(bool *)object_unbox(moved))break;
        void *pair=invoke(iterator,"get_Current",0,NULL,&ok),*binding=invoke(pair,"get_Value",0,NULL,&ok);
        NSString *action=managed_string(field_reference(binding,"playerActionId"));
        if(!action.length || [actions containsObject:action])continue;
        int level=0;void *args[]={string_new(action.UTF8String),&level};
        void *info=invoke_typed(manager,"GetPlayerActionInfo",2,"System.String",args,&ok);
        if(!ok || !info)continue;
        for(const char **name=(const char *[]){"primaryKeyboardInput","secondaryKeyboardInput",NULL};*name;name++){
            void *input=field_reference(info,*name);if(!input)continue;
            if(input_read_bool(input,"useCtrl")!=!!(modifiers&NSEventModifierFlagControl) ||
               input_read_bool(input,"useShift")!=!!(modifiers&NSEventModifierFlagShift) ||
               input_read_bool(input,"useAlt")!=!!(modifiers&NSEventModifierFlagOption))continue;
            void *keyField=class_get_field_from_name(object_get_class(input),"key"),*timingField=class_get_field_from_name(object_get_class(input),"timingType");
            int key=-1,timing=-1;if(keyField)field_get_value(input,keyField,&key);if(timingField)field_get_value(input,timingField,&timing);
            if(key!=input_hud_game_keys[index])continue;
            if(timing!=input_hud_timings[0] && timing!=input_hud_timings[1] && timing!=input_hud_timings[2])continue;
            void *idValue=invoke(binding,"get_id",0,NULL,&ok);if(!ok || !idValue)continue;
            int bindingId=*(int *)object_unbox(idValue);bool needLogError=false;void *enabledArgs[]={&bindingId,&needLogError};
            void *enabled=invoke(manager,"IsBindingEnabled",2,enabledArgs,&ok);
            if(!ok || !enabled || !*(bool *)object_unbox(enabled))continue;
            matched=true;
            bool atTiming=timing==input_hud_timings[down ? 0 : 1];
            if(!down && timing==input_hud_timings[2]){
                float longPressTime=0;void *field=class_get_field_from_name(object_get_class(input),"longPressTime");
                if(field)field_get_value(input,field,&longPressTime);
                atTiming=!cancelled && (longPressTime<=0 || timestamp-input_hud_key_pressed_at[index]<longPressTime);
            }
            fprintf(output,"input_hud_binding=%s timing=%d eligible=%d\n",action.UTF8String,timing,atTiming);
            if(!atTiming)continue;
            [pending addObject:@{@"handle":@(gchandle_new(binding,false)),@"id":@(bindingId),@"action":action}];
            [actions addObject:action];break;
        }
    }
    // Opening a menu changes m_bindingInfos. Finish enumeration before invoking callbacks.
    for(NSDictionary *entry in pending){
        uint32_t handle=[entry[@"handle"] unsignedIntValue];int bindingId=[entry[@"id"] intValue];
        bool needLogError=false;void *enabledArgs[]={&bindingId,&needLogError};
        void *enabled=invoke(manager,"IsBindingEnabled",2,enabledArgs,&ok);
        if(ok && enabled && *(bool *)object_unbox(enabled)){
            invoke(gchandle_get_target(handle),"Trigger",0,NULL,&ok);
            if(ok){input_hud_key_actions++;input_hud_last_action=entry[@"action"];}
        }
        gchandle_free(handle);
    }
    if(down)input_hud_keys_held[index]=matched;
    fprintf(output,"input_hud_key=%s down=%d matched=%d actions=%lu last=%s\n",input_hud_key_names[index],down,matched,(unsigned long)input_hud_key_actions,input_hud_last_action.UTF8String ?: "");
    return matched || wasHeld;
}

static void input_hud_keys_cancel(void) {
    for(int i=0;i<InputHudKeyCount;i++)if(input_hud_keys_held[i])
        input_hud_key_dispatch(i,false,input_hud_key_modifiers[i],NSProcessInfo.processInfo.systemUptime,true);
}

static bool input_hud_key_event(NSEvent *event) {
    if(!input_hud_keys_ready)return false;
    int index=-1;for(int i=0;i<InputHudKeyCount;i++)if(event.keyCode==input_hud_native_keys[i]){index=i;break;}
    if(index<0)return false;
    bool down;
    if(event.type==NSEventTypeFlagsChanged){
        NSUInteger mask=event.keyCode==0x38 ? 2 : event.keyCode==0x3c ? 4 : 0;
        if(!mask)return false;
        down=(event.modifierFlags & mask)!=0;
        if(down==input_hud_keys_held[index])return false;
    }else if(event.type==NSEventTypeKeyDown){
        if(event.isARepeat)return input_hud_keys_held[index];down=true;
    }else if(event.type==NSEventTypeKeyUp)down=false;
    else return false;
    // E must remain available to native text fields and the login SDK.
    if(input_read_bool(input_manager(),"m_hasFocusedInputFiled")){input_hud_keys_cancel();return false;}
    return input_hud_key_dispatch(index,down,event.modifierFlags,event.timestamp,false);
}
