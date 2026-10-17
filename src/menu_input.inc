/* Deliver real AppKit pointer events to the existing InputSystem UI module. */
static uint32_t input_menu_mouse_handle;
static bool input_menu_mouse_owned;
static int input_menu_device_id,input_menu_previous_lock;
static uint16_t input_menu_buttons;
static float input_menu_position[2];
static NSUInteger input_menu_events;
static const void *input_menu_event_ctor,*input_menu_queue_event;
static bool input_window_edge_drag;
static _Atomic(uintptr_t) *input_legacy_pointer_slot;
static void (*input_legacy_pointer_original)(float *);
static _Atomic(bool) input_legacy_pointer_active;
static _Atomic(float) input_legacy_pointer_x,input_legacy_pointer_y;

static void input_legacy_pointer(float *position) {
    if(atomic_load(&input_legacy_pointer_active)){
        position[0]=atomic_load(&input_legacy_pointer_x);
        position[1]=atomic_load(&input_legacy_pointer_y);
        position[2]=0;
    }else input_legacy_pointer_original(position);
}

static bool input_legacy_pointer_start(bool enabled) {
    atomic_store(&input_legacy_pointer_active,false);
    if(input_legacy_pointer_slot){
        uintptr_t expected=(uintptr_t)input_legacy_pointer;
        atomic_compare_exchange_strong(input_legacy_pointer_slot,&expected,(uintptr_t)input_legacy_pointer_original);
        input_legacy_pointer_slot=NULL;
    }
    if(!enabled)return true;
    const void *method=class_get_method_from_name(find_class("UnityEngine","Input"),"get_mousePosition",0);
    if(!method)return false;
    const uint32_t *code=*(const uint32_t **)method;
    // Current-build wrapper identity; only its writable icall cache is replaced.
    const uint32_t expected[]={0xd100c3ff,0xa9014ff4,0xa9027bfd,0x910083fd,0xb9000bff,0xf90003ff,
        0xf00336f3,0xf9441668,0xb50000c8,0xb0023dc0,0x9125bc00,0x941dd8ee,0xaa0003e8,0xf9041660,
        0x910003e0,0xd63f0100,0x2d4007e0,0xbd400be2,0xa9427bfd,0xa9414ff4,0x9100c3ff,0xd65f03c0};
    // iOS 1.6.3; keep the same icall cache swap and exact byte guard.
    const uint32_t expected_1_6_3[]={0xd100c3ff,0xa9014ff4,0xa9027bfd,0x910083fd,0xb9000bff,0xf90003ff,0xb0037e13,0xf9432268,0xb50000c8,0xf0026300,0x9114c800,0x9427cc56,0xaa0003e8,0xf9032260,0x910003e0,0xd63f0100,0x2d4007e0,0xbd400be2,0xa9427bfd,0xa9414ff4,0x9100c3ff,0xd65f03c0};
    if(!code || (memcmp(code,expected,sizeof expected) && memcmp(code,expected_1_6_3,sizeof expected_1_6_3)))return false;
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    void *native=resolve ? resolve("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)") : NULL;
    if(!native)return false;
    input_static(find_class("UnityEngine","Input"),"get_mousePosition",0,NULL);
    int64_t pages=((code[6]>>29)&3)|(((code[6]>>5)&0x7ffff)<<2);
    if(pages & (1<<20))pages-=1<<21;
    uintptr_t slot=(((uintptr_t)(code+6))&~(uintptr_t)4095)+pages*4096+((code[7]>>10)&4095)*8;
    _Atomic(uintptr_t) *cache=(_Atomic(uintptr_t) *)slot;
    uintptr_t original=(uintptr_t)native;
    input_legacy_pointer_original=native;
    if(!atomic_compare_exchange_strong(cache,&original,(uintptr_t)input_legacy_pointer))return false;
    input_legacy_pointer_slot=cache;
    return true;
}

static bool input_window_event(NSEvent *event) {
    // Let AppKit receive the complete resize gesture, including its mouse-up.
    if(input_window_edge_drag){
        if(event.type==NSEventTypeLeftMouseUp)input_window_edge_drag=false;
        return true;
    }
    NSWindow *window=event.window;
    if(!window || !(window.styleMask & NSWindowStyleMaskResizable))return false;
    NSView *view=window.contentView;
    NSPoint point=[view convertPoint:event.locationInWindow fromView:nil];
    bool edge=!NSPointInRect(point,NSInsetRect(view.bounds,6,6));
    if(edge && event.type==NSEventTypeLeftMouseDown){
        input_window_edge_drag=true;
        input_set_captured(false);
    }
    return edge;
}

static bool input_menu_queue(float scrollX,float scrollY,unsigned short clicks) {
    if(!input_menu_mouse_handle)return false;
    // Current-build metadata: InputEvent is 20 bytes; StateEvent's payload starts at 24.
    unsigned char bytes[56] __attribute__((aligned(8)))={0};int size=54,type=0x53544154;double time=-1;
    void *base=object_new(find_class("UnityEngine.InputSystem.LowLevel","InputEvent"));
    void *args[]={&type,&size,&input_menu_device_id,&time},*exception=NULL;
    runtime_invoke(input_menu_event_ctor,object_unbox(base),args,&exception);
    if(exception)return false;
    memcpy(bytes,object_unbox(base),20);
    uint32_t format=0x4d4f5553;memcpy(bytes+20,&format,4);
    memcpy(bytes+24,input_menu_position,8);
    float scroll[2]={scrollX,scrollY};memcpy(bytes+40,scroll,8);
    memcpy(bytes+48,&input_menu_buttons,2);memcpy(bytes+52,&clicks,2);
    void *pointer=bytes,*queueArgs[]={&pointer};
    runtime_invoke(input_menu_queue_event,NULL,queueArgs,&exception);
    if(exception)return false;
    input_menu_events++;return true;
}

static void input_menu_release(void) {
    if(input_menu_buttons){input_menu_buttons=0;input_menu_queue(0,0,0);}
}

static bool input_menu_start(bool enabled) {
    input_legacy_pointer_start(false);
    if(input_menu_mouse_handle){
        input_menu_release();
        if(input_menu_mouse_owned){
            void *args[]={gchandle_get_target(input_menu_mouse_handle)};
            input_static(find_class("UnityEngine.InputSystem","InputSystem"),"RemoveDevice",1,args);
        }
        gchandle_free(input_menu_mouse_handle);input_menu_mouse_handle=0;
        void *args[]={&input_menu_previous_lock};
        input_static(find_class("UnityEngine","Cursor"),"set_lockState",1,args);
    }
    if(!enabled)return true;
    // Verify the value-type ABI against this build before emitting any state packets.
    const char *types[]={"StateEvent","StateEvent","MouseState","MouseState","MouseState","MouseState"};
    const char *fields[]={"stateFormat","stateData","position","scroll","buttons","clickCount"};
    const int offsets[]={36,40,16,32,40,44};
    for(int i=0;i<6;i++){
        void *klass=find_class("UnityEngine.InputSystem.LowLevel",types[i]);
        void *field=klass ? class_get_field_from_name(klass,fields[i]) : NULL;
        if(!field || field_get_offset(field)!=offsets[i])return false;
    }
    void *system=find_class("UnityEngine.InputSystem","InputSystem");
    input_menu_event_ctor=typed_method(find_class("UnityEngine.InputSystem.LowLevel","InputEvent"),".ctor",4,"UnityEngine.InputSystem.Utilities.FourCC");
    input_menu_queue_event=typed_method(system,"QueueEvent",1,"UnityEngine.InputSystem.LowLevel.InputEventPtr");
    if(!input_menu_event_ctor || !input_menu_queue_event)return false;
    void *mouse=input_static(find_class("UnityEngine.InputSystem","Mouse"),"get_current",0,NULL);
    input_menu_mouse_owned=mouse==NULL;
    if(!mouse){void *args[]={string_new("Mouse"),string_new("MacNativeMouse"),NULL};mouse=input_static(system,"AddDevice",3,args);}
    if(!mouse)return false;
    bool ok=false;void *device=invoke(mouse,"get_deviceId",0,NULL,&ok);
    if(!ok || !device)return false;
    input_menu_device_id=*(int *)object_unbox(device);
    input_menu_mouse_handle=gchandle_new(mouse,false);
    void *lock=input_static(find_class("UnityEngine","Cursor"),"get_lockState",0,NULL);
    input_menu_previous_lock=lock ? *(int *)object_unbox(lock) : 0;
    // macOS capture already owns cursor locking. UI raycasts need Unity's cursor unlocked.
    int unlocked=0;void *args[]={&unlocked};
    input_static(find_class("UnityEngine","Cursor"),"set_lockState",1,args);
    fprintf(output,"input_legacy_pointer_ready=%d\n",input_legacy_pointer_start(true));
    fprintf(output,"input_menu_mouse_ready=1 device=%d owned=%d\n",input_menu_device_id,input_menu_mouse_owned);
    return true;
}

static bool input_menu_event(NSEvent *event) {
    if(!input_menu_mouse_handle)return false;
    void *mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(input_cursor_captured || !NSApp.isActive || !mode || !*(bool *)object_unbox(mode) || (event.modifierFlags & NSEventModifierFlagCommand)){
        input_menu_release();return false;
    }
    NSView *view=event.window.contentView;
    if(!view || view.bounds.size.width<=0 || view.bounds.size.height<=0)return false;
    NSPoint point=[view convertPoint:event.locationInWindow fromView:nil];
    void *screen=find_class("UnityEngine","Screen");
    void *width=input_static(screen,"get_width",0,NULL),*height=input_static(screen,"get_height",0,NULL);
    if(!width || !height)return false;
    input_menu_position[0]=(point.x-view.bounds.origin.x)/view.bounds.size.width * *(int *)object_unbox(width);
    double y=(point.y-view.bounds.origin.y)/view.bounds.size.height;
    input_menu_position[1]=(view.isFlipped ? 1-y : y) * *(int *)object_unbox(height);
    atomic_store(&input_legacy_pointer_x,input_menu_position[0]);
    atomic_store(&input_legacy_pointer_y,input_menu_position[1]);
    atomic_store(&input_legacy_pointer_active,true);
    int button=-1;bool down=false;
    switch(event.type){
        case NSEventTypeLeftMouseDown:button=0;down=true;break;
        case NSEventTypeLeftMouseUp:button=0;break;
        case NSEventTypeRightMouseDown:button=1;down=true;break;
        case NSEventTypeRightMouseUp:button=1;break;
        default:break;
    }
    if(button>=0){if(down)input_menu_buttons|=1u<<button;else input_menu_buttons&=~(1u<<button);}
    float scrollX=0,scrollY=0;
    if(event.type==NSEventTypeScrollWheel){scrollX=event.deltaX*20;scrollY=event.deltaY*20;}
    return input_menu_queue(scrollX,scrollY,button>=0 ? (unsigned short)event.clickCount : 0);
}
