// For a Mac with no Unity keyboard/mouse device, feed the normal InputSystem
// event queue. The game keeps ownership of action maps, bindings and UI focus.
static const void *(*input172Methods)(void *, void **);
static const char *(*input172MethodName)(const void *);
static uint32_t (*input172ParamCount)(const void *);
static void *(*input172Box)(void *,void *);
static bool (*input172Assignable)(void *,void *);
static void *input172System,*input172StateEvent,*input172Extensions,*input172Single,*input172Vector,*input172Key;
static const void *input172From,*input172Queue,*input172Write,*input172Add,*input172KeyControl;
static int input172Allocator;
static uintptr_t input172VirtualKeyboard,input172VirtualMouse;
static NSMutableDictionary<NSNumber *,NSNumber *> *input172Held;
static NSMutableDictionary<NSNumber *,NSNumber *> *input172Buttons;
static NSMutableDictionary<NSNumber *,NSNumber *> *input172KeyCodes;
static NSString *input172QueueError;
static NSUInteger input172KeyEvents,input172MouseEvents;
static void input172SetButton(NSUInteger button,BOOL down);
static void input172SetEscape(BOOL down);
static void input172ShareWheel(double x,double y);
static float input172Pointer[2],input172Size[2];

static const void *input172Overload(void *cls,const char *name,uint32_t count,const char *firstType) {
    if(!cls)return NULL;
    void *iterator=NULL;const void *method;
    while((method=input172Methods(cls,&iterator))){
        if(strcmp(input172MethodName(method),name) || input172ParamCount(method)!=count)continue;
        void *first=input172ClassForType(input172Parameter(method,0));
        if(first && !strcmp(className(first),firstType))return method;
    }
    return NULL;
}

static BOOL input172EventsAPI(void) {
    if(input172Queue)return YES;
    input172Methods=dlsym(RTLD_DEFAULT,"il2cpp_class_get_methods");
    input172MethodName=dlsym(RTLD_DEFAULT,"il2cpp_method_get_name");
    input172ParamCount=dlsym(RTLD_DEFAULT,"il2cpp_method_get_param_count");
    input172Box=dlsym(RTLD_DEFAULT,"il2cpp_value_box");
    input172Assignable=dlsym(RTLD_DEFAULT,"il2cpp_class_is_assignable_from");
    if(!input172Methods || !input172MethodName || !input172ParamCount || !input172Box || !input172Assignable)return NO;
    input172System=input172Class("UnityEngine.InputSystem","InputSystem");
    input172StateEvent=input172Class("UnityEngine.InputSystem.LowLevel","StateEvent");
    input172Extensions=input172Class("UnityEngine.InputSystem","InputControlExtensions");
    input172Single=input172Class("System","Single");input172Vector=input172Class("UnityEngine","Vector2");
    input172Key=input172Class("UnityEngine.InputSystem","Key");
    void *allocator=input172Class("Unity.Collections","Allocator");
    void *temp=allocator ? input172Field(allocator,"Temp") : NULL;
    if(!temp || !input172Single || !input172Vector || !input172Key)return NO;
    input172StaticValue(temp,&input172Allocator);
    input172From=input172Overload(input172StateEvent,"From",3,"InputDevice");
    input172Write=input172Overload(input172Extensions,"WriteValueFromObjectIntoEvent",3,"InputControl");
    input172Add=input172Overload(input172System,"AddDevice",3,"String");
    input172KeyControl=input172Overload(input172Keyboard,"get_Item",1,"Key");
    if(!input172From || !input172Write || !input172Add || !input172KeyControl)return NO;
    input172Queue=input172Overload(input172System,"QueueEvent",1,"InputEventPtr");
    return input172Queue!=NULL;
}

// The iOS keyboard backend delays or loses physical key edges. Its devices
// stay silent while the virtual keyboard, fed from AppKit, is in use.
static NSMutableArray<NSNumber *> *input172NativeKeyboards;
static uintptr_t input172NativeCurrent;

static void input172RestoreKeyboards(void) {
    bool ok=false;
    for(NSNumber *handle in input172NativeKeyboards){
        void *device=input172Target(handle.unsignedLongValue),*args[]={device};
        if(device)callClass(input172System,NULL,"EnableDevice",1,args,&ok);
        input172Free(handle.unsignedLongValue);
    }
    input172NativeKeyboards=nil;
    if(input172NativeCurrent){
        void *device=input172Target(input172NativeCurrent);
        if(device)call(device,"MakeCurrent",0,NULL,&ok);
        input172Free(input172NativeCurrent);input172NativeCurrent=0;
    }
}

static BOOL input172SilenceKeyboards(void) {
    bool ok=false;
    void *current=callClass(input172Keyboard,NULL,"get_current",0,NULL,&ok);
    void *devices=callClass(input172System,NULL,"get_devices",0,NULL,&ok);
    const void *count=devices ? lookupMethod(objectClass(devices),"get_Count",0) : NULL;
    const void *item=devices ? lookupMethod(objectClass(devices),"get_Item",1) : NULL;
    void *exception=NULL,*length=count ? invoke(count,unbox(devices),NULL,&exception) : NULL;
    if(!item || !length || exception)return NO;
    input172NativeKeyboards=[NSMutableArray array];
    if(current)input172NativeCurrent=input172Retain(current,false);
    // iOS can expose several keyboards; any enabled one takes over on its next key.
    for(int i=0,n=*(int *)unbox(length);i<n && i<64;i++){
        void *args[]={&i},*device=invoke(item,unbox(devices),args,&exception);
        if(exception || !device || !input172Assignable(input172Keyboard,objectClass(device)) ||
           ![renderNumber(call(device,"get_enabled",0,NULL,&ok),@YES) boolValue])continue;
        bool keepEvents=false;void *disable[]={device,&keepEvents};
        callClass(input172System,NULL,"DisableDevice",2,disable,&ok);
        [input172NativeKeyboards addObject:@(input172Retain(device,false))];
        if(!ok || [renderNumber(call(device,"get_enabled",0,NULL,&ok),@YES) boolValue])return NO;
    }
    return YES;
}

static BOOL input172OwnDevice(BOOL mouse) {
    uintptr_t *handle=mouse ? &input172VirtualMouse : &input172VirtualKeyboard;
    if(*handle)return YES;
    bool ok=false;
    if(!input172EventsAPI()){input172QueueError=@"Unity 输入事件接口未就绪";return NO;}
    if(mouse && callClass(input172Mouse,NULL,"get_current",0,NULL,&ok))return NO;
    if(!mouse && !input172SilenceKeyboards()){input172RestoreKeyboards();input172QueueError=@"Unity 键盘设备未能停用";return NO;}
    void *args[]={newString(mouse ? "Mouse" : "Keyboard"),newString(mouse ? "BetterMacFieldMouse" : "BetterMacFieldKeyboard"),NULL};
    void *exception=NULL,*device=invoke(input172Add,NULL,args,&exception);
    if(!exception && device)*handle=input172Retain(device,false);
    if(!*handle){
        if(device){void *remove[]={device};callClass(input172System,NULL,"RemoveDevice",1,remove,&ok);}
        if(!mouse)input172RestoreKeyboards();
        input172QueueError=@"Unity 未建立输入设备";return NO;
    }
    if(mouse)input172Buttons=[NSMutableDictionary dictionary];
    else{
        input172Held=[NSMutableDictionary dictionary];input172KeyCodes=[NSMutableDictionary dictionary];
        call(device,"MakeCurrent",0,NULL,&ok);
    }
    return YES;
}

static NSString *input172KeyName(unsigned short code) {
    static NSDictionary *names;
    if(!names)names=@{
        @0:@"A",@1:@"S",@2:@"D",@3:@"F",@4:@"H",@5:@"G",@6:@"Z",@7:@"X",@8:@"C",@9:@"V",
        @11:@"B",@12:@"Q",@13:@"W",@14:@"E",@15:@"R",@16:@"Y",@17:@"T",@18:@"Digit1",@19:@"Digit2",
        @20:@"Digit3",@21:@"Digit4",@22:@"Digit6",@23:@"Digit5",@24:@"Equals",@25:@"Digit9",@26:@"Digit7",
        @27:@"Minus",@28:@"Digit8",@29:@"Digit0",@30:@"RightBracket",@31:@"O",@32:@"U",@33:@"LeftBracket",
        @34:@"I",@35:@"P",@36:@"Enter",@37:@"L",@38:@"J",@39:@"Quote",@40:@"K",@41:@"Semicolon",@42:@"Backslash",
        @43:@"Comma",@44:@"Slash",@45:@"N",@46:@"M",@47:@"Period",@48:@"Tab",@49:@"Space",@50:@"Backquote",
        @51:@"Backspace",@53:@"Escape",@56:@"LeftShift",@57:@"CapsLock",@58:@"LeftAlt",@59:@"LeftCtrl",
        @60:@"RightShift",@61:@"RightAlt",@62:@"RightCtrl",@65:@"NumpadPeriod",@67:@"NumpadMultiply",
        @69:@"NumpadPlus",@71:@"NumLock",@75:@"NumpadDivide",@76:@"NumpadEnter",@78:@"NumpadMinus",
        @82:@"Numpad0",@83:@"Numpad1",@84:@"Numpad2",@85:@"Numpad3",@86:@"Numpad4",@87:@"Numpad5",
        @88:@"Numpad6",@89:@"Numpad7",@91:@"Numpad8",@92:@"Numpad9",@96:@"F5",@97:@"F6",@98:@"F7",
        @99:@"F3",@100:@"F8",@101:@"F9",@103:@"F11",@109:@"F10",@111:@"F12",@114:@"Insert",@115:@"Home",
        @116:@"PageUp",@117:@"Delete",@118:@"F4",@119:@"End",@120:@"F2",@121:@"PageDown",@122:@"F1",
        @123:@"LeftArrow",@124:@"RightArrow",@125:@"DownArrow",@126:@"UpArrow"};
    return names[@(code)];
}

static BOOL input172WriteControl(void *control,void *event,void *boxed) {
    if(!control || !event || !boxed)return NO;
    void *args[]={control,&event,boxed},*exception=NULL;
    invoke(input172Write,NULL,args,&exception);return exception==NULL;
}

static BOOL input172Submit(uintptr_t deviceHandle, BOOL (^write)(void *,void *)) {
    void *device=input172Target(deviceHandle),*event=NULL,*exception=NULL;
    void *args[]={device,&event,&input172Allocator};
    void *buffer=invoke(input172From,NULL,args,&exception);
    if(exception || !buffer || !event)return NO;
    BOOL success=write(device,event);
    if(success){void *queueArgs[]={&event};invoke(input172Queue,NULL,queueArgs,&exception);success=!exception;}
    const void *dispose=lookupMethod(objectClass(buffer),"Dispose",0);
    if(dispose)invoke(dispose,unbox(buffer),NULL,&exception);
    if(!success)input172QueueError=@"Unity 拒绝了输入事件";
    else input172QueueError=nil;
    return success;
}

static BOOL input172KeyboardEvent(void) {
    if(!input172VirtualKeyboard)return NO;
    BOOL success=input172Submit(input172VirtualKeyboard,^BOOL(void *device,void *event){
        for(NSNumber *native in input172Held){
            NSNumber *code=input172KeyCodes[native];
            if(!code){
                NSString *name=input172KeyName(native.unsignedShortValue);
                void *field=name ? input172Field(input172Key,name.UTF8String) : NULL;
                if(!field)continue;
                int value=0;input172StaticValue(field,&value);code=@(value);input172KeyCodes[native]=code;
            }
            int key=code.intValue;void *args[]={&key},*exception=NULL;
            void *control=invoke(input172KeyControl,device,args,&exception);
            float value=input172Held[native].boolValue ? 1.f : 0.f;
            if(exception || !input172WriteControl(control,event,input172Box(input172Single,&value)))return NO;
        }
        return YES;
    });
    if(success)input172KeyEvents++;
    return success;
}

static BOOL input172MouseEvent(NSEvent *native) {
    if(!input172VirtualMouse)return NO;
    BOOL success=input172Submit(input172VirtualMouse,^BOOL(void *device,void *event){
        bool ok=false;
        for(NSNumber *button in input172Buttons){
            const char *getters[]={"get_leftButton","get_rightButton","get_middleButton","get_backButton","get_forwardButton"};
            if(button.unsignedIntegerValue>=5)continue;
            float pressed=input172Buttons[button].boolValue ? 1.f : 0.f;
            if(!input172WriteControl(call(device,getters[button.unsignedIntegerValue],0,NULL,&ok),event,input172Box(input172Single,&pressed)))return NO;
        }
        if(!native)return YES;
        NSView *view=native.window.contentView;
        NSPoint p=[view convertPoint:native.locationInWindow fromView:nil];NSSize size=view.bounds.size;
        int width=[integerValue(callClass(input172Screen,NULL,"get_width",0,NULL,&ok)) intValue];
        int height=[integerValue(callClass(input172Screen,NULL,"get_height",0,NULL,&ok)) intValue];
        if(size.width<=0 || size.height<=0 || width<=0 || height<=0)return NO;
        float position[]={(float)(p.x*width/size.width),(float)((view.isFlipped ? size.height-p.y : p.y)*height/size.height)};
        input172Pointer[0]=position[0];input172Pointer[1]=position[1];input172Size[0]=width;input172Size[1]=height;
        float delta[]={0,0},scroll[]={0,0};
        if(native.type==NSEventTypeScrollWheel){
            CGFloat factor=native.hasPreciseScrollingDeltas ? 1.0 : 120.0;
            scroll[0]=native.scrollingDeltaX*factor;scroll[1]=native.scrollingDeltaY*factor;
            input172ShareWheel(native.deltaX,native.deltaY);
        }else{delta[0]=native.deltaX*width/size.width;delta[1]=-native.deltaY*height/size.height;}
        return input172WriteControl(call(device,"get_position",0,NULL,&ok),event,input172Box(input172Vector,position)) &&
            input172WriteControl(call(device,"get_delta",0,NULL,&ok),event,input172Box(input172Vector,delta)) &&
            input172WriteControl(call(device,"get_scroll",0,NULL,&ok),event,input172Box(input172Vector,scroll));
    });
    if(success)input172MouseEvents++;
    return success;
}

static void input172NeutralDevices(void) {
    BOOL keys=NO,buttons=NO;
    for(NSNumber *native in input172Held.allKeys)if(input172Held[native].boolValue){input172Held[native]=@NO;keys=YES;}
    for(NSNumber *native in input172Buttons.allKeys)if(input172Buttons[native].boolValue){input172SetButton(native.unsignedIntegerValue,NO);buttons=YES;}
    input172SetEscape(NO);
    if(keys)input172KeyboardEvent();
    if(buttons)input172MouseEvent(nil);
}

static void input172RemoveDevices(void) {
    input172NeutralDevices();
    uintptr_t *devices[]={&input172VirtualKeyboard,&input172VirtualMouse};
    for(int i=0;i<2;i++)if(*devices[i]){
        bool ok=false;void *device=input172Target(*devices[i]),*args[]={device};
        if(device)callClass(input172System,NULL,"RemoveDevice",1,args,&ok);
        input172Free(*devices[i]);*devices[i]=0;
    }
    input172RestoreKeyboards();
}
