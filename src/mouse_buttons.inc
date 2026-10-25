/* Share InputSystem mouse edges with legacy consumers on the verified client. */
static uint32_t input_legacy_button_handles[2];
static const void *input_legacy_button_getters[3];
static _Atomic(uintptr_t) *input_legacy_button_slots[3];
static bool (*input_legacy_button_original[3])(int);

static bool input_legacy_mouse_key_read(int key,int kind,bool *value) {
    int button=key-323;
    if(button<0 || button>=2 || !input_legacy_button_handles[button] ||
       !NSApp.isActive || input_native_overlay_visible())return false;
    // GetKey's hook order is held/up/down; the mouse API is held/down/up.
    int mouseKind=kind==0 ? 0 : 3-kind;
    void *exception=NULL;
    void *boxed=runtime_invoke(input_legacy_button_getters[mouseKind],
        gchandle_get_target(input_legacy_button_handles[button]),NULL,&exception);
    if(!boxed || exception)return false;
    *value=*(bool *)object_unbox(boxed);return true;
}

static bool input_legacy_button_read(int button,int kind) {
    if(button>=0 && button<2 && input_legacy_button_handles[button] &&
       NSApp.isActive && !input_native_overlay_visible()){
        void *exception=NULL;
        void *value=runtime_invoke(input_legacy_button_getters[kind],
            gchandle_get_target(input_legacy_button_handles[button]),NULL,&exception);
        if(value && !exception)return *(bool *)object_unbox(value);
    }
    return input_legacy_button_original[kind](button);
}
static bool input_legacy_button_held(int button){return input_legacy_button_read(button,0);}
static bool input_legacy_button_down(int button){return input_legacy_button_read(button,1);}
static bool input_legacy_button_up(int button){return input_legacy_button_read(button,2);}

static bool input_legacy_buttons_start(bool enabled) {
    const uintptr_t hooks[]={(uintptr_t)input_legacy_button_held,(uintptr_t)input_legacy_button_down,(uintptr_t)input_legacy_button_up};
    for(int i=0;i<3;i++)if(input_legacy_button_slots[i]){
        uintptr_t expected=hooks[i];
        atomic_compare_exchange_strong(input_legacy_button_slots[i],&expected,(uintptr_t)input_legacy_button_original[i]);
        input_legacy_button_slots[i]=NULL;
    }
    for(int i=0;i<2;i++)if(input_legacy_button_handles[i]){
        gchandle_free(input_legacy_button_handles[i]);input_legacy_button_handles[i]=0;
    }
    if(!enabled)return true;
    const uint32_t expected[3][16]={
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9428e81,0xb50000c1,0xd0026300,0x9139d000,0x9427cdcf,0xaa0003e1,0xf9028e80,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9429281,0xb50000c1,0xd0026300,0x913a9000,0x9427cdbf,0xaa0003e1,0xf9029280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb0037e14,0xf9429681,0xb50000c1,0xd0026300,0x913b6000,0x9427cdaf,0xaa0003e1,0xf9029680,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020}};
    const uint32_t expected_cn[3][16]={
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9452e81,0xb50000c1,0x90028100,0x912f1000,0x9427d22e,0xaa0003e1,0xf9052e80,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9453281,0xb50000c1,0x90028100,0x912fd000,0x9427d21e,0xaa0003e1,0xf9053280,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020},
        {0xa9be4ff4,0xa9017bfd,0x910043fd,0xaa0003f3,0xb003a0f4,0xf9453681,0xb50000c1,0x90028100,0x9130a000,0x9427d20e,0xaa0003e1,0xf9053680,0xaa1303e0,0xa9417bfd,0xa8c24ff4,0xd61f0020}};
    void *klass=find_class("UnityEngine","Input");
    const char *names[]={"GetMouseButton","GetMouseButtonDown","GetMouseButtonUp"};
    const uint32_t *codes[3];
    for(int i=0;i<3;i++){
        const void *method=class_get_method_from_name(klass,names[i],1);
        codes[i]=method ? *(const uint32_t **)method : NULL;
        if(!codes[i] || (memcmp(codes[i],expected[i],sizeof expected[i]) &&
                        memcmp(codes[i],expected_cn[i],sizeof expected_cn[i])))return false;
    }
    bool ok=false;
    const char *controls[]={"get_leftButton","get_rightButton"};
    for(int i=0;i<2;i++){
        void *control=invoke(gchandle_get_target(input_menu_mouse_handle),controls[i],0,NULL,&ok);
        if(!ok || !control){input_legacy_buttons_start(false);return false;}
        input_legacy_button_handles[i]=gchandle_new(control,false);
    }
    void *control=gchandle_get_target(input_legacy_button_handles[0]);
    const char *getters[]={"get_isPressed","get_wasPressedThisFrame","get_wasReleasedThisFrame"};
    for(int i=0;i<3;i++){
        input_legacy_button_getters[i]=class_get_method_from_name(object_get_class(control),getters[i],0);
        if(!input_legacy_button_getters[i]){input_legacy_buttons_start(false);return false;}
    }
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    for(int i=0;i<3;i++){
        NSString *name=[NSString stringWithFormat:@"UnityEngine.Input::%s(System.Int32)",names[i]];
        void *native=resolve ? resolve(name.UTF8String) : NULL;
        if(!native){input_legacy_buttons_start(false);return false;}
        int button=0;void *args[]={&button};input_static(klass,names[i],1,args);
        const uint32_t *code=codes[i];
        int64_t pages=((code[4]>>29)&3)|(((code[4]>>5)&0x7ffff)<<2);
        if(pages&(1<<20))pages-=1<<21;
        uintptr_t slot=(((uintptr_t)(code+4))&~(uintptr_t)4095)+pages*4096+((code[5]>>10)&4095)*8;
        _Atomic(uintptr_t) *cache=(_Atomic(uintptr_t) *)slot;uintptr_t original=(uintptr_t)native;
        input_legacy_button_original[i]=native;
        if(!atomic_compare_exchange_strong(cache,&original,hooks[i])){input_legacy_buttons_start(false);return false;}
        input_legacy_button_slots[i]=cache;
    }
    return true;
}
