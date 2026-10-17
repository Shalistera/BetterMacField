/* Forward physical number-key edges to the original PC skill press handler. */
static uint32_t input_skill_key_handles[4];
static double input_skill_key_started[4];

static void input_skill_keys_cancel(void) {
    for(int i=0;i<4;i++)if(input_skill_key_handles[i]){
        bool ok=false;void *cell=gchandle_get_target(input_skill_key_handles[i]);
        invoke(cell,"ClearBtnClick",0,NULL,&ok);
        gchandle_free(input_skill_key_handles[i]);input_skill_key_handles[i]=0;
        fprintf(output,"input_skill_key=%d cancelled=%d\n",i+1,ok);
    }
}

static bool input_skill_key_event(NSEvent *event) {
    if(event.type!=NSEventTypeKeyDown && event.type!=NSEventTypeKeyUp)return false;
    int index=-1;
    switch(event.keyCode){case 18:index=0;break;case 19:index=1;break;case 20:index=2;break;case 21:index=3;break;}
    if(index<0)return false;
    double receivedAt=NSProcessInfo.processInfo.systemUptime;
    bool down=event.type==NSEventTypeKeyDown,ok=false;
    if(!down){
        if(!input_skill_key_handles[index])return false;
        void *cell=gchandle_get_target(input_skill_key_handles[index]),*args[]={NULL};
        invoke_typed(cell,"OnPressSkillEnd",1,"UnityEngine.EventSystems.PointerEventData",args,&ok);
        gchandle_free(input_skill_key_handles[index]);input_skill_key_handles[index]=0;
        fprintf(output,"input_skill_key=%d down=0 ok=%d held_ms=%.2f queue_ms=%.2f handler_ms=%.2f\n",index+1,ok,
            (event.timestamp-input_skill_key_started[index])*1000,
            (receivedAt-event.timestamp)*1000,(NSProcessInfo.processInfo.systemUptime-receivedAt)*1000);
        return true;
    }
    if(input_skill_key_handles[index])return true;
    if(event.isARepeat || !NSApp.isActive || (event.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagShift)))return false;
    void *keyboard=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!keyboard || !*(bool *)object_unbox(keyboard))return false;
    void *klass=find_class("Beyond.UI","SkillButton");
    void *args[]={type_get_object(class_get_type(klass))};
    void *array=input_static(find_class("UnityEngine","Resources"),"FindObjectsOfTypeAll",1,args);
    if(!array)return false;
    for(uintptr_t i=0;i<array_length(array);i++){
        void *cell=array_item(array,i);int slot=-1,panel=-1;
        field_get_value(cell,class_get_field_from_name(klass,"m_charIndex"),&slot);
        field_get_value(cell,class_get_field_from_name(klass,"m_panelType"),&panel);
        if(slot!=index || panel!=0)continue;
        void *active=invoke(cell,"get_isActiveAndEnabled",0,NULL,&ok);
        if(!ok || !active || !*(bool *)object_unbox(active))continue;
        void *button=field_reference(cell,"normalButton");
        void *enabled=invoke(button,"get_groupEnabled",0,NULL,&ok);
        if(!ok || !enabled || !*(bool *)object_unbox(enabled))continue;
        void *interactable=invoke(button,"Beyond.Input.IBindingView.get_interactable",0,NULL,&ok);
        if(!ok || !interactable || !*(bool *)object_unbox(interactable))continue;
        void *pressArgs[]={NULL};
        invoke_typed(cell,"OnPressSkillStart",1,"UnityEngine.EventSystems.PointerEventData",pressArgs,&ok);
        if(!ok)return false;
        input_skill_key_handles[index]=gchandle_new(cell,false);input_skill_key_started[index]=event.timestamp;
        fprintf(output,"input_skill_key=%d down=1 ok=1 queue_ms=%.2f handler_ms=%.2f\n",index+1,
            (receivedAt-event.timestamp)*1000,(NSProcessInfo.processInfo.systemUptime-receivedAt)*1000);
        return true;
    }
    return false;
}
