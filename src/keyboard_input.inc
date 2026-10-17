/* Feed AppKit edges to the original InputSystem keyboard binding path. */
static uint32_t input_keyboard_handle,input_keyboard_original_handle,input_keyboard_state_handle;
static bool input_keyboard_experiment_enabled=true;
static NSMutableArray *input_keyboard_disabled_devices;
static int input_keyboard_device_id,input_keyboard_state_size,input_keyboard_format;
static int input_keyboard_codes[128];
static bool input_keyboard_held[128];
static NSUInteger input_keyboard_events;
static const void *input_keyboard_event_ctor,*input_keyboard_queue_event,*input_keyboard_set;
static uint32_t input_keyboard_escape_handle;
static const void *input_keyboard_escape_getters[3];
static _Atomic(uintptr_t) *input_keyboard_escape_slots[3];
static bool (*input_keyboard_escape_original[3])(int);

static bool input_keyboard_escape_read(int key,int kind) {
    if(key==27 && input_keyboard_escape_handle && NSApp.isActive && !input_native_overlay_visible()){
        void *exception=NULL;
        void *value=runtime_invoke(input_keyboard_escape_getters[kind],gchandle_get_target(input_keyboard_escape_handle),NULL,&exception);
        return value && !exception && *(bool *)object_unbox(value);
    }
    return input_keyboard_escape_original[kind](key);
}
static bool input_keyboard_escape_held(int key){return input_keyboard_escape_read(key,0);}
static bool input_keyboard_escape_up(int key){return input_keyboard_escape_read(key,1);}
static bool input_keyboard_escape_down(int key){return input_keyboard_escape_read(key,2);}

static bool input_keyboard_escape_start(bool enabled) {
    const uintptr_t hooks[]={(uintptr_t)input_keyboard_escape_held,(uintptr_t)input_keyboard_escape_up,(uintptr_t)input_keyboard_escape_down};
    for(int i=0;i<3;i++)if(input_keyboard_escape_slots[i]){
        uintptr_t expected=hooks[i];
        atomic_compare_exchange_strong(input_keyboard_escape_slots[i],&expected,(uintptr_t)input_keyboard_escape_original[i]);
        input_keyboard_escape_slots[i]=NULL;
    }
    if(input_keyboard_escape_handle){gchandle_free(input_keyboard_escape_handle);input_keyboard_escape_handle=0;}
    if(!enabled)return true;
    bool ok=false;void *control=invoke(gchandle_get_target(input_keyboard_handle),"get_escapeKey",0,NULL,&ok);
    if(!ok || !control)return false;
    const char *getters[]={"get_isPressed","get_wasReleasedThisFrame","get_wasPressedThisFrame"};
    for(int i=0;i<3;i++){
        input_keyboard_escape_getters[i]=class_get_method_from_name(object_get_class(control),getters[i],0);
        if(!input_keyboard_escape_getters[i])return false;
    }
    input_keyboard_escape_handle=gchandle_new(control,false);
    const char *names[]={"GetKeyInt","GetKeyUpInt","GetKeyDownInt"};
    const uint32_t expected[3][16]={
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0x90033714,0xf9435681,0xb50000c1,0xd0023dc0,0x91028400,0x941ddb17,0xaa0003e1,0xf9035680,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0x90033714,0xf9435e81,0xb50000c1,0xd0023dc0,0x91040800,0x941ddaf7,0xaa0003e1,0xf9035e80,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0x90033714,0xf9436681,0xb50000c1,0xd0023dc0,0x91059c00,0x941ddad7,0xaa0003e1,0xf9036680,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020}};
    // iOS 1.6.3; exact wrapper bytes from the verified UnityFramework build.
    const uint32_t expected_1_6_3[3][16]={
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9426281,0xb50000c1,0xd0026300,0x91319000,0x9427ce7f,0xaa0003e1,0xf9026280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9426a81,0xb50000c1,0xd0026300,0x91331400,0x9427ce5f,0xaa0003e1,0xf9026a80,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9427281,0xb50000c1,0xd0026300,0x9134a800,0x9427ce3f,0xaa0003e1,0xf9027280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020}};
    // CN 1.6.3, verified metadata methods and complete wrapper bodies.
    const uint32_t expected_cn_1_6_3[3][16]={
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9450281,0xb50000c1,0x90028100,0x9126d000,0x9427d2de,0xaa0003e1,0xf9050280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9450a81,0xb50000c1,0x90028100,0x91285400,0x9427d2be,0xaa0003e1,0xf9050a80,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9451281,0xb50000c1,0x90028100,0x9129e800,0x9427d29e,0xaa0003e1,0xf9051280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020}};
    void *klass=find_class("UnityEngine","Input");
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    for(int i=0;i<3;i++){
        const void *method=class_get_method_from_name(klass,names[i],1);
        const uint32_t *code=method ? *(const uint32_t **)method : NULL;
        if(!code || (memcmp(code,expected[i],sizeof expected[i]) && memcmp(code,expected_1_6_3[i],sizeof expected_1_6_3[i]) && memcmp(code,expected_cn_1_6_3[i],sizeof expected_cn_1_6_3[i]))){input_keyboard_escape_start(false);return false;}
        NSString *name=[NSString stringWithFormat:@"UnityEngine.Input::%s(UnityEngine.KeyCode)",names[i]];
        void *native=resolve ? resolve(name.UTF8String) : NULL;
        if(!native){input_keyboard_escape_start(false);return false;}
        int key=27;void *args[]={&key};input_static(klass,names[i],1,args);
        int64_t pages=((code[4]>>29)&3)|(((code[4]>>5)&0x7ffff)<<2);
        if(pages&(1<<20))pages-=1<<21;
        uintptr_t slot=(((uintptr_t)(code+4))&~(uintptr_t)4095)+pages*4096+((code[5]>>10)&4095)*8;
        _Atomic(uintptr_t) *cache=(_Atomic(uintptr_t) *)slot;uintptr_t original=(uintptr_t)native;
        input_keyboard_escape_original[i]=native;
        if(!atomic_compare_exchange_strong(cache,&original,hooks[i])){input_keyboard_escape_start(false);return false;}
        input_keyboard_escape_slots[i]=cache;
    }
    fprintf(output,"input_keyboard_escape=shared_state\n");return true;
}

static bool input_keyboard_queue(void) {
    if(!input_keyboard_handle || !input_keyboard_state_handle)return false;
    unsigned char bytes[88] __attribute__((aligned(8)))={0};
    int size=24+input_keyboard_state_size,type=0x53544154;double time=-1;
    void *event=object_new(find_class("UnityEngine.InputSystem.LowLevel","InputEvent"));
    void *args[]={&type,&size,&input_keyboard_device_id,&time},*exception=NULL;
    runtime_invoke(input_keyboard_event_ctor,object_unbox(event),args,&exception);
    if(exception)return false;
    memcpy(bytes,object_unbox(event),20);memcpy(bytes+20,&input_keyboard_format,4);
    memcpy(bytes+24,object_unbox(gchandle_get_target(input_keyboard_state_handle)),input_keyboard_state_size);
    void *pointer=bytes,*queueArgs[]={&pointer};
    runtime_invoke(input_keyboard_queue_event,NULL,queueArgs,&exception);
    if(exception)return false;
    input_keyboard_events++;return true;
}

static void input_keyboard_release(void) {
    if(!input_keyboard_state_handle)return;
    bool any=false;for(int i=0;i<128;i++)any|=input_keyboard_held[i];
    if(!any)return;
    memset(input_keyboard_held,0,sizeof input_keyboard_held);
    memset(object_unbox(gchandle_get_target(input_keyboard_state_handle)),0,input_keyboard_state_size);
    input_keyboard_queue();
}

static bool input_keyboard_start(bool enabled) {
    void *system=find_class("UnityEngine.InputSystem","InputSystem");bool ok=false;
    input_keyboard_escape_start(false);
    input_keyboard_release();
    if(input_keyboard_handle){
        void *args[]={gchandle_get_target(input_keyboard_handle)};
        input_static(system,"RemoveDevice",1,args);
        gchandle_free(input_keyboard_handle);input_keyboard_handle=0;
    }
    if(input_keyboard_state_handle){gchandle_free(input_keyboard_state_handle);input_keyboard_state_handle=0;}
    for(NSNumber *handle in input_keyboard_disabled_devices){
        void *args[]={gchandle_get_target(handle.unsignedIntValue)};
        input_static(system,"EnableDevice",1,args);gchandle_free(handle.unsignedIntValue);
    }
    input_keyboard_disabled_devices=nil;
    if(input_keyboard_original_handle){
        void *original=gchandle_get_target(input_keyboard_original_handle),*args[]={original};
        invoke(original,"MakeCurrent",0,NULL,&ok);
        gchandle_free(input_keyboard_original_handle);input_keyboard_original_handle=0;
    }
    if(!enabled)return true;
    input_hud_keys_cancel();
    void *stateClass=find_class("UnityEngine.InputSystem.LowLevel","KeyboardState");
    void *keys=stateClass ? class_get_field_from_name(stateClass,"keys") : NULL;uint32_t alignment=0;
    input_keyboard_state_size=stateClass ? class_value_size(stateClass,&alignment) : 0;
    if(!keys || field_get_offset(keys)!=16 || input_keyboard_state_size<=0 || input_keyboard_state_size>64)return false;
    void *format=input_static(stateClass,"get_Format",0,NULL);
    if(!format)return false;input_keyboard_format=*(int *)object_unbox(format);
    input_keyboard_event_ctor=typed_method(find_class("UnityEngine.InputSystem.LowLevel","InputEvent"),".ctor",4,"UnityEngine.InputSystem.Utilities.FourCC");
    input_keyboard_queue_event=typed_method(system,"QueueEvent",1,"UnityEngine.InputSystem.LowLevel.InputEventPtr");
    input_keyboard_set=typed_method(stateClass,"Set",2,"UnityEngine.InputSystem.Key");
    if(!input_keyboard_event_ctor || !input_keyboard_queue_event || !input_keyboard_set)return false;
    // Physical macOS virtual key codes; game action remapping stays in InputManager.
    const struct {int native;const char *name;} map[]={
        {0,"A"},{1,"S"},{2,"D"},{3,"F"},{4,"H"},{5,"G"},{6,"Z"},{7,"X"},{8,"C"},{9,"V"},
        {11,"B"},{12,"Q"},{13,"W"},{14,"E"},{15,"R"},{16,"Y"},{17,"T"},
        {18,"Digit1"},{19,"Digit2"},{20,"Digit3"},{21,"Digit4"},{22,"Digit6"},{23,"Digit5"},
        {24,"Equals"},{25,"Digit9"},{26,"Digit7"},{27,"Minus"},{28,"Digit8"},{29,"Digit0"},
        {30,"RightBracket"},{31,"O"},{32,"U"},{33,"LeftBracket"},{34,"I"},{35,"P"},
        {36,"Enter"},{37,"L"},{38,"J"},{39,"Quote"},{40,"K"},{41,"Semicolon"},{42,"Backslash"},
        {43,"Comma"},{44,"Slash"},{45,"N"},{46,"M"},{47,"Period"},{48,"Tab"},{49,"Space"},
        {50,"Backquote"},{51,"Backspace"},{53,"Escape"},{54,"RightMeta"},{55,"LeftMeta"},
        {56,"LeftShift"},{57,"CapsLock"},{58,"LeftAlt"},{59,"LeftCtrl"},{60,"RightShift"},
        {61,"RightAlt"},{62,"RightCtrl"},{65,"NumpadPeriod"},{67,"NumpadMultiply"},{69,"NumpadPlus"},
        {71,"NumLock"},{75,"NumpadDivide"},{76,"NumpadEnter"},{78,"NumpadMinus"},
        {82,"Numpad0"},{83,"Numpad1"},{84,"Numpad2"},{85,"Numpad3"},{86,"Numpad4"},
        {87,"Numpad5"},{88,"Numpad6"},{89,"Numpad7"},{91,"Numpad8"},{92,"Numpad9"},
        {96,"F5"},{97,"F6"},{98,"F7"},{99,"F3"},{100,"F8"},{101,"F9"},{103,"F11"},
        {109,"F10"},{111,"F12"},{114,"Insert"},{115,"Home"},{116,"PageUp"},{117,"Delete"},
        {118,"F4"},{119,"End"},{120,"F2"},{121,"PageDown"},{122,"F1"},
        {123,"LeftArrow"},{124,"RightArrow"},{125,"DownArrow"},{126,"UpArrow"}};
    memset(input_keyboard_codes,0,sizeof input_keyboard_codes);memset(input_keyboard_held,0,sizeof input_keyboard_held);
    void *keyClass=find_class("UnityEngine.InputSystem","Key");
    for(unsigned i=0;i<sizeof map/sizeof map[0];i++){
        void *field=class_get_field_from_name(keyClass,map[i].name);
        if(!field){fprintf(output,"input_keyboard_missing_key=%s\n",map[i].name);return false;}
        field_static_get_value(field,&input_keyboard_codes[map[i].native]);
        if(input_keyboard_codes[map[i].native]<=0 || input_keyboard_codes[map[i].native]>=input_keyboard_state_size*8)return false;
    }
    void *original=input_static(find_class("UnityEngine.InputSystem","Keyboard"),"get_current",0,NULL);
    if(original){
        input_keyboard_original_handle=gchandle_new(original,false);
    }
    // iOS can expose several native keyboards. Disabling only current leaves
    // another FastKeyboard free to take over on the next physical key event.
    input_keyboard_disabled_devices=[NSMutableArray array];
    void *devices=input_static(system,"get_devices",0,NULL);
    int count=object_int(devices,"get_Count");
    void *keyboardClass=find_class("UnityEngine.InputSystem","Keyboard");
    for(int i=0;i<count;i++){
        void *indexArgs[]={&i};void *device=invoke(devices,"get_Item",1,indexArgs,&ok);
        void *type=device ? object_get_class(device) : NULL;
        while(type && type!=keyboardClass)type=class_get_parent(type);
        if(!type)continue;
        void *value=invoke(device,"get_enabled",0,NULL,&ok);
        if(!ok || !value || !*(bool *)object_unbox(value))continue;
        [input_keyboard_disabled_devices addObject:@(gchandle_new(device,false))];
        bool keepEvents=false;void *disableArgs[]={device,&keepEvents};
        input_static(system,"DisableDevice",2,disableArgs);
        value=invoke(device,"get_enabled",0,NULL,&ok);
        fprintf(output,"input_keyboard_native_disabled=%d name=%s verified=%d\n",
            object_int(device,"get_deviceId"),object_name(device).UTF8String,ok && value && !*(bool *)object_unbox(value));
        if(!ok || !value || *(bool *)object_unbox(value)){input_keyboard_start(false);return false;}
    }
    void *args[]={string_new("Keyboard"),string_new("MacNativeKeyboard"),NULL};
    void *keyboard=input_static(system,"AddDevice",3,args);
    if(!keyboard){input_keyboard_start(false);return false;}
    input_keyboard_handle=gchandle_new(keyboard,false);
    void *idValue=invoke(keyboard,"get_deviceId",0,NULL,&ok);
    if(!ok || !idValue){input_keyboard_start(false);return false;}
    input_keyboard_device_id=*(int *)object_unbox(idValue);
    input_keyboard_state_handle=gchandle_new(object_new(stateClass),false);
    invoke(keyboard,"MakeCurrent",0,NULL,&ok);
    if(!ok || input_static(find_class("UnityEngine.InputSystem","Keyboard"),"get_current",0,NULL)!=keyboard){input_keyboard_start(false);return false;}
    if(!input_keyboard_escape_start(true)){input_keyboard_start(false);return false;}
    input_keyboard_queue();
    fprintf(output,"input_keyboard=enabled device=%d state_bytes=%d\n",input_keyboard_device_id,input_keyboard_state_size);
    return true;
}

static bool input_keyboard_set_state(int native,bool down) {
    if(!input_keyboard_handle || native>=128 || !input_keyboard_codes[native] || down==input_keyboard_held[native])return false;
    int code=input_keyboard_codes[native];void *args[]={&code,&down},*exception=NULL;
    runtime_invoke(input_keyboard_set,object_unbox(gchandle_get_target(input_keyboard_state_handle)),args,&exception);
    if(exception)return false;
    input_keyboard_held[native]=down;return true;
}

static void input_keyboard_modifiers(NSEvent *event) {
    if(!input_keyboard_handle)return;
    if(!NSApp.isActive || (event.modifierFlags & NSEventModifierFlagCommand)){input_keyboard_release();return;}
    // A key event can arrive after focus changes without its modifier's earlier
    // flagsChanged event. Reconcile before direct WASD/skill paths consume it.
    const struct {int left,right;NSUInteger leftMask,rightMask,aggregate;} modifiers[]={
        {56,60,2,4,NSEventModifierFlagShift},
        {59,62,1,0x2000,NSEventModifierFlagControl},
        {58,61,0x20,0x40,NSEventModifierFlagOption}};
    bool changed=false;
    for(unsigned i=0;i<sizeof modifiers/sizeof modifiers[0];i++){
        bool active=(event.modifierFlags & modifiers[i].aggregate)!=0;
        bool left=active && (event.modifierFlags & modifiers[i].leftMask);
        bool right=active && (event.modifierFlags & modifiers[i].rightMask);
        if(active && !left && !right){
            right=input_keyboard_held[modifiers[i].right];
            left=input_keyboard_held[modifiers[i].left] || !right;
        }
        changed|=input_keyboard_set_state(modifiers[i].left,left);
        changed|=input_keyboard_set_state(modifiers[i].right,right);
    }
    changed|=input_keyboard_set_state(57,(event.modifierFlags & NSEventModifierFlagCapsLock)!=0);
    if(changed)input_keyboard_queue();
}

static void input_keyboard_event(NSEvent *event) {
    if(!input_keyboard_handle || event.keyCode>=128 || !input_keyboard_codes[event.keyCode])return;
    if(!NSApp.isActive || (event.modifierFlags & NSEventModifierFlagCommand)){input_keyboard_release();return;}
    bool down;
    if(event.type==NSEventTypeKeyDown){if(event.isARepeat)return;down=true;}
    else if(event.type==NSEventTypeKeyUp)down=false;
    else return;
    if(input_keyboard_set_state(event.keyCode,down))input_keyboard_queue();
}
