// Scene-local AppKit input and named Unity/game interfaces.
#include <math.h>
static uintptr_t (*input172Retain)(void *, bool);
static void *(*input172Target)(uintptr_t);
static void (*input172Free)(uintptr_t);
static void *(*input172Field)(void *, const char *);
static void *(*input172FieldObject)(void *, void *);
static void *(*input172Parameter)(const void *, unsigned);
static void *(*input172ClassForType)(void *);
static void (*input172StaticValue)(void *, void *);
static void *input172Game, *input172UnityObject, *input172GameObject, *input172Device, *input172Input;
static void *input172Keyboard, *input172Mouse, *input172Screen, *input172Cursor, *input172LegacyInput;
static BOOL input172Bindings, input172Native, input172Ready, input172Scene, input172Gameplay;
static NSUInteger input172MenuTracking;
static NSUInteger input172Keys, input172LookEvents;
static BOOL input172ModeOwned;
static int input172OriginalMode, input172DesiredMode;

static void *input172Class(const char *space, const char *name) {
    size_t count=0;const void **list=assemblies(domainGet(),&count);
    for(size_t index=0;index<count;index++){
        const void *image=assemblyImage(list[index]);void *cls=image ? lookupClass(image,space,name) : NULL;
        if(cls)return cls;
    }
    return NULL;
}

static BOOL input172Bind(void) {
#define INPUT_API(variable,symbol) if(!(variable=dlsym(RTLD_DEFAULT,"il2cpp_" #symbol)))return NO
    INPUT_API(input172Retain,gchandle_new);
    INPUT_API(input172Target,gchandle_get_target);
    INPUT_API(input172Free,gchandle_free);
    INPUT_API(input172Field,class_get_field_from_name);
    INPUT_API(input172FieldObject,field_get_value_object);
    INPUT_API(input172Parameter,method_get_param);
    INPUT_API(input172ClassForType,class_from_type);
    INPUT_API(input172StaticValue,field_static_get_value);
#undef INPUT_API
    input172Game=input172Class("Beyond.Gameplay","GameInstance");
    input172UnityObject=classNamed("UnityEngine.CoreModule.dll","UnityEngine","Object");
    input172GameObject=classNamed("UnityEngine.CoreModule.dll","UnityEngine","GameObject");
    input172Screen=classNamed("UnityEngine.CoreModule.dll","UnityEngine","Screen");
    input172Cursor=classNamed("UnityEngine.CoreModule.dll","UnityEngine","Cursor");
    input172LegacyInput=input172Class("UnityEngine","Input");
    input172Input=input172Class("Beyond.Input","InputManager");
    input172Device=input172Class("Beyond","DeviceInfo");
    input172Keyboard=input172Class("UnityEngine.InputSystem","Keyboard");
    input172Mouse=input172Class("UnityEngine.InputSystem","Mouse");
    return input172Game && input172UnityObject && input172GameObject;
}

static void *input172Member(void *cls,void *receiver,const char *name) {
    void *field=cls ? input172Field(cls,name) : NULL;
    return field ? input172FieldObject(field,receiver) : NULL;
}
static void *input172Manager(void) {return input172Member(input172Game,NULL,"cameraManager");}
static BOOL input172Loaded(void) {
    bool ok=false;void *player=callClass(input172Game,NULL,"get_playerController",0,NULL,&ok);
    return player && input172Member(objectClass(player),player,"mainCharacter") && input172Manager();
}
static BOOL input172HasHUD(void) {
    if (!input172GameObject)
        input172GameObject = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    bool ok = false;
    void *args[] = {newString("MainHudPanel")};
    return callClass(input172GameObject, NULL, "Find", 1, args, &ok) != NULL && ok;
}

static BOOL input172PlayableScene(BOOL continuingScene) {
    // The title already renders through Unity. Wait for its exploration HUD
    // before touching game/input singletons. An open inventory may hide that
    // HUD later without changing the player or leaving the current scene.
    if (!continuingScene && !input172HasHUD()) return NO;
    static void *cameraClass;
    if (!cameraClass) cameraClass = classNamed("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    bool ok = false;
    void *camera = callClass(cameraClass, NULL, "get_main", 0, NULL, &ok);
    if (![renderNumber(call(camera, "get_isActiveAndEnabled", 0, NULL, &ok), @YES) boolValue]) return NO;
    if (!input172Bindings) input172Bindings = input172Bind();
    return input172Bindings && input172Loaded();
}

static BOOL input172SceneInteraction(void) {
    if(!input172Loaded() || !input172HasHUD())return NO;
    bool ok=false;
    void *player=callClass(input172Game,NULL,"get_playerController",0,NULL,&ok);
    NSNumber *blocked=renderNumber(call(player,"get_blockPlayerInput",0,NULL,&ok),@YES);
    return blocked && !blocked.boolValue;
}
static BOOL input172NativePanel(id view) {
    if(!view || [[view valueForKey:@"hidden"] boolValue] || [[view valueForKey:@"alpha"] doubleValue]<0.01)return NO;
    for(NSString *name in @[@"WKWebView",@"UITextField",@"UITextView"]){
        Class cls=NSClassFromString(name);
        if(cls && [view isKindOfClass:cls] && ([name isEqual:@"WKWebView"] ||
           [[view valueForKey:@"isFirstResponder"] boolValue]))return YES;
    }
    for(id child in [view valueForKey:@"subviews"])if(input172NativePanel(child))return YES;
    return NO;
}
static BOOL input172Overlay(void) {
    @try{
        id app=[NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        for(id window in [app valueForKey:@"windows"]){
            id controller=[window valueForKey:@"rootViewController"];
            while(controller){
                if([controller isKindOfClass:NSClassFromString(@"UIAlertController")])return YES;
                controller=[controller valueForKey:@"presentedViewController"];
            }
            if(input172NativePanel(window))return YES;
        }
    }@catch(NSException *exception){return YES;}
    return NO;
}
#include "input172.devices.inc"
#include "input172.shared.inc"
static void input172ReleaseMovement(void);
static void input172ReleaseMovement(void) {
    input172NeutralDevices();
}

// While the game hides its cursor, hold the Mac pointer and turn its motion
// into camera input. Option asks the game for its cursor, like Alt on PC.
static BOOL input172ShowOwned;
static __weak NSWindow *input172ResizeWindow;
static float input172LookX,input172LookY;
static int input172LookFrame;

static void input172Look(double dx,double dy) {
    static void *timeClass;
    if(!timeClass)timeClass=classNamed("UnityEngine.CoreModule.dll","UnityEngine","Time");
    bool ok=false;
    int frame=[integerValue(callClass(timeClass,NULL,"get_frameCount",0,NULL,&ok)) intValue];
    if(frame!=input172LookFrame){input172LookFrame=frame;input172LookX=input172LookY=0;}
    // Half of PC LevelCameraCtrl._UpdateCamera's scale at 1080p. The camera keeps
    // only the latest OnInput of a frame, so pass the frame's running total.
    input172LookX+=(float)(dx*0.45/1920);input172LookY+=(float)(dy*0.315/1080);
    void *args[]={&input172LookX,&input172LookY};call(input172Manager(),"OnInput",2,args,&ok);
    if(ok)input172LookEvents++;
}

static void input172SetCaptured(BOOL captured) {
    if(captured==input172Captured)return;
    if(captured){
        if(CGAssociateMouseAndMouseCursorPosition(false)!=kCGErrorSuccess)return;
        [NSCursor hide];
    }else{
        CGAssociateMouseAndMouseCursorPosition(true);[NSCursor unhide];
        if(input172Ready){float zero=0;void *args[]={&zero,&zero};bool ok=false;call(input172Manager(),"OnInput",2,args,&ok);}
    }
    input172Captured=captured;
}

static void input172ShowCursor(BOOL show) {
    if(show==input172ShowOwned)return;
    bool ok=false,value=show;
    void *manager=callClass(input172Input,NULL,"get_instance",0,NULL,&ok);
    if(!manager){input172ShowOwned=NO;return;}
    void *args[]={newString("BetterMacField"),&value};
    call(manager,"SetCursorShowRequest",2,args,&ok);
    if(ok)input172ShowOwned=show;
}

static void input172UpdateCapture(void) {
    // AppKit can consume the mouse-up inside its resize tracking loop.
    if(input172ResizeWindow && !(NSEvent.pressedMouseButtons & 1) && !input172ResizeWindow.inLiveResize)
        input172ResizeWindow=nil;
    NSEventModifierFlags flags=NSEvent.modifierFlags;
    BOOL live=input172Ready && input172Native && input172Scene && NSApp.isActive && NSApp.keyWindow &&
        !input172MenuTracking && !input172ResizeWindow;
    input172ShowCursor(live && (flags & NSEventModifierFlagOption));
    bool ok=false;
    BOOL hidden=live && !(flags & (NSEventModifierFlagOption|NSEventModifierFlagCommand)) &&
        [renderNumber(callClass(input172Device,NULL,"get_usingKeyboard",0,NULL,&ok),@YES) boolValue];
    if(hidden){
        void *mode=callClass(input172Cursor,NULL,"get_lockState",0,NULL,&ok);
        hidden=ok && mode && *(int *)unbox(mode)==1;
    }
    input172SetCaptured(hidden);
}

static BOOL input172WindowEvent(NSEvent *event) {
    NSEventType type=event.type;
    // Scroll and keyboard input must keep working when the pointer rests at an edge.
    if(type!=NSEventTypeMouseMoved && type!=NSEventTypeLeftMouseDown &&
       type!=NSEventTypeLeftMouseDragged && type!=NSEventTypeLeftMouseUp)return NO;
    if(input172ResizeWindow){
        if(type==NSEventTypeLeftMouseUp)input172ResizeWindow=nil;
        return YES;
    }
    NSWindow *window=event.window;
    if(!window || !(window.styleMask & NSWindowStyleMaskResizable) ||
       (window.styleMask & NSWindowStyleMaskFullScreen))return NO;
    NSView *view=window.contentView;
    NSPoint point=[view convertPoint:event.locationInWindow fromView:nil];
    BOOL edge=!NSPointInRect(point,NSInsetRect(view.bounds,6,6));
    if(edge && type==NSEventTypeLeftMouseDown){
        input172ResizeWindow=window;
        input172ReleaseMovement();
        input172SetCaptured(NO);
    }
    return edge;
}

static void input172MouseWindow(void) {
    static __weak NSWindow *tracked;
    static BOOL previous;
    NSWindow *next=(input172Native && input172Scene) && NSApp.isActive ? NSApp.keyWindow : nil;
    if(next==tracked)return;
    if(tracked && tracked.acceptsMouseMovedEvents)tracked.acceptsMouseMovedEvents=previous;
    tracked=next;
    if(tracked){previous=tracked.acceptsMouseMovedEvents;tracked.acceptsMouseMovedEvents=YES;}
}

static void input172SwitchDevice(BOOL enable) {
    bool ok=false;void *manager=callClass(input172Input,NULL,"get_instance",0,NULL,&ok);
    void *current=callClass(input172Device,NULL,"get_inputType",0,NULL,&ok);
    if(!manager || !current)return;
    const void *method=lookupMethod(objectClass(manager),"_TryChangeInputDeviceType",1);
    void *type=method ? input172ClassForType(input172Parameter(method,0)) : NULL;
    if(type!=objectClass(current))return;
    int mode=*(int *)unbox(current), next;
    if(enable){
        void *entry=type ? input172Field(type,"Keyboard") : NULL;
        if(!entry)return;
        input172StaticValue(entry,&next);
        if(mode==next)return;
        if(!input172ModeOwned)input172OriginalMode=mode;
        input172DesiredMode=next;
    }else{
        if(!input172ModeOwned || mode!=input172DesiredMode){input172ModeOwned=NO;return;}
        next=input172OriginalMode;
    }
    void *args[]={&next};call(manager,"_TryChangeInputDeviceType",1,args,&ok);
    if(ok)input172ModeOwned=enable;
}

static void input172Events(void) {
    static id monitor; if(monitor)return;
    NSNotificationCenter *notifications=NSNotificationCenter.defaultCenter;
    [notifications addObserverForName:NSMenuDidBeginTrackingNotification object:nil queue:nil usingBlock:^(__unused NSNotification *n){
        input172MenuTracking++;input172ReleaseMovement();
        input172UpdateCapture();
    }];
    [notifications addObserverForName:NSMenuDidEndTrackingNotification object:nil queue:nil usingBlock:^(__unused NSNotification *n){
        if(input172MenuTracking)input172MenuTracking--;
    }];
    [notifications addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:nil usingBlock:^(__unused NSNotification *n){
        input172ResizeWindow=nil;
        input172ReleaseMovement();
        input172UpdateCapture();
    }];
    static dispatch_source_t capture;
    capture=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    dispatch_source_set_timer(capture,DISPATCH_TIME_NOW,NSEC_PER_SEC/30,NSEC_PER_MSEC*5);
    dispatch_source_set_event_handler(capture,^{@autoreleasepool{
        if(input172Native || input172Captured || input172ShowOwned)input172UpdateCapture();
    }});
    dispatch_resume(capture);
    NSEventMask keys=NSEventMaskKeyDown|NSEventMaskKeyUp|NSEventMaskFlagsChanged;
    NSEventMask motion=NSEventMaskMouseMoved|NSEventMaskLeftMouseDragged|NSEventMaskRightMouseDragged|NSEventMaskOtherMouseDragged;
    NSEventMask buttonDown=NSEventMaskLeftMouseDown|NSEventMaskRightMouseDown|NSEventMaskOtherMouseDown;
    NSEventMask buttonUp=NSEventMaskLeftMouseUp|NSEventMaskRightMouseUp|NSEventMaskOtherMouseUp;
    monitor=[NSEvent addLocalMonitorForEventsMatchingMask:keys|motion|buttonDown|buttonUp|NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event){
        if(!input172Ready || !NSApp.isActive || !event.window || event.window!=NSApp.keyWindow || input172MenuTracking ||
           [event.window.firstResponder isKindOfClass:NSTextView.class])return event;
        if(input172WindowEvent(event))return event;
        if(event.modifierFlags & NSEventModifierFlagCommand){input172ReleaseMovement();return event;}
        NSEventMask kind=NSEventMaskFromType(event.type);
        if(!input172Native || !input172Scene)return event;
        if(kind & keys){
            if(event.type==NSEventTypeFlagsChanged)input172UpdateCapture();
            if(event.type==NSEventTypeKeyDown && !event.isARepeat){
                // The game rebuilds its input bindings and HUD together.
                if(input172Gameplay){input172SwitchDevice(YES);input172Keys++;}
            }
            if(input172KeyName(event.keyCode) && input172OwnDevice(NO)){
                if(event.type==NSEventTypeKeyDown && event.isARepeat)return nil;
                BOOL down=event.type==NSEventTypeKeyDown;
                if(event.type==NSEventTypeFlagsChanged){
                    // Left and right modifiers report through their own device bits.
                    NSUInteger flags=event.modifierFlags;
                    switch(event.keyCode){
                        case 56: down=(flags & 0x2)!=0;break;
                        case 60: down=(flags & 0x4)!=0;break;
                        case 58: down=(flags & 0x20)!=0;break;
                        case 61: down=(flags & 0x40)!=0;break;
                        case 59: down=(flags & 0x1)!=0;break;
                        case 62: down=(flags & 0x2000)!=0;break;
                        case 57: down=(flags & NSEventModifierFlagCapsLock)!=0;break;
                        default:return event;
                    }
                }
                input172Held[@(event.keyCode)]=@(down);
                if(event.keyCode==53)input172SetEscape(down);
                if(input172KeyboardEvent())return nil;
            }
            return event;
        }
        if(input172Captured && (kind & motion)){input172Look(event.deltaX,-event.deltaY);return nil;}
        static NSTimeInterval switched;
        if(input172Gameplay && event.timestamp-switched>0.25){switched=event.timestamp;input172SwitchDevice(YES);}
        if(input172OwnDevice(YES)){
            if((kind & (buttonDown|buttonUp)) && event.buttonNumber<5)
                input172SetButton(event.buttonNumber,(kind & buttonDown)!=0);
            if(input172MouseEvent(event))return nil;
        }
        // The held pointer has no position to click with as a touch.
        return input172Captured ? nil : event;
    }];
}

static void updateInput172(NSDictionary *selected,NSMutableDictionary *state,BOOL sceneLoaded) {
    BOOL wasReady=input172Ready;
    input172Ready=sceneLoaded;
    if(!sceneLoaded){
        input172Scene=NO;
        input172Gameplay=NO;
        if(wasReady){
            input172SetCaptured(NO);input172ShowCursor(NO);
            if(input172ModeOwned)input172SwitchDevice(NO);
            if(input172VirtualMouse || input172VirtualKeyboard)input172RemoveDevices();
            input172MouseWindow();
        }
        state[@"input172"]=@{@"route":selected[@"inputRoute"] ?: @"playcover", @"scene":@NO,
                             @"sceneLoaded":@NO, @"gameplay":@NO};
    }
    if(input172Ready){
        input172Events();
        input172Native=[selected[@"inputRoute"] isEqual:@"native"] && [(selected[@"keyboard"] ?: @YES) boolValue];
        input172Scene=!input172Overlay();
        input172Gameplay=input172Scene && input172SceneInteraction();
        input172MouseWindow();
        input172ShareQueries(input172Native);
        if(!input172Scene || !input172Native){
            if(input172ModeOwned)input172SwitchDevice(NO);
            if(input172VirtualMouse || input172VirtualKeyboard)input172RemoveDevices();
        }
        bool ok=false;
        BOOL keyboard=callClass(input172Keyboard,NULL,"get_current",0,NULL,&ok)!=NULL;
        BOOL mouse=callClass(input172Mouse,NULL,"get_current",0,NULL,&ok)!=NULL;
        NSNumber *gameMode=renderNumber(callClass(input172Device,NULL,"get_usingKeyboard",0,NULL,&ok),@YES);
        state[@"input172"]=@{@"route":selected[@"inputRoute"] ?: @"playcover", @"scene":@(input172Scene), @"sceneLoaded":@(sceneLoaded), @"gameplay":@(input172Gameplay),
            @"keyboardDevice":@(keyboard),@"mouseDevice":@(mouse),@"keyboardHUD":gameMode ?: @NO,
            @"physicalKeyDowns":@(input172Keys),@"cameraEvents":@(input172LookEvents),
            @"pointerHeld":@(input172Captured),@"sharedQueries":input172ShareState,
            @"queuedKeyEvents":@(input172KeyEvents),@"queuedMouseEvents":@(input172MouseEvents),
            @"virtualKeyboard":@(input172VirtualKeyboard!=0),@"virtualMouse":@(input172VirtualMouse!=0),
            @"queueMessage":input172QueueError ?: @""};
    }
}
