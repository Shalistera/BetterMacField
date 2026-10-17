/* Activate the accepted scene keyboard bridge from local physical input. */
static id input_auto_monitor;
static NSWindow *input_auto_window;
static BOOL input_auto_previous_mouse_moved;
static double input_auto_last_check;
static NSUInteger input_auto_switches;

static void input_auto_window_update(void) {
    bool bridged=input_capture_timer || input_move_enabled || input_mouse_bridge_enabled || input_button_bridge_enabled;
    bool nativeOverlay=input_auto_monitor && input_native_overlay_visible();
    void *mode=nativeOverlay ? input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL) : NULL;
    if(nativeOverlay && (bridged || (mode && *(bool *)object_unbox(mode)))){
        input_buttons(false);input_move(false);input_bridge(false,false);input_capture(false);
        select_input_mode(@"touch");
        fprintf(output,"input_auto_return=touch_native_overlay\n");
    }else if(input_auto_monitor && bridged){
        void *player=input_game_controller();
        if(!player || !field_reference(player,"mainCharacter") || !input_game_camera()){
            input_buttons(false);input_move(false);input_bridge(false,false);input_capture(false);
            select_input_mode(@"touch");
            fprintf(output,"input_auto_return=touch_scene_unloaded\n");
        }
    }
    NSWindow *window=input_auto_monitor && NSApp.isActive ? NSApp.keyWindow : nil;
    if(input_auto_window==window)return;
    if(input_auto_window)input_auto_window.acceptsMouseMovedEvents=input_auto_previous_mouse_moved;
    input_auto_window=window;
    if(window){input_auto_previous_mouse_moved=window.acceptsMouseMovedEvents;window.acceptsMouseMovedEvents=YES;}
}

static bool input_auto_activate(void) {
    if(input_native_overlay_visible())return false;
    void *mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    bool keyboard=mode && *(bool *)object_unbox(mode);
    if(keyboard && input_capture_timer && input_mouse_bridge_to_camera && input_mouse_bridge_enabled &&
       input_move_enabled && input_button_bridge_enabled &&
       gchandle_get_target(input_move_player_handle)==input_game_controller())return false;
    if(!input_scene_is_ready())return false;
    input_buttons(false);input_move(false);input_bridge(false,false);input_capture(false);
    input_tune(@{@"bindings":@YES,@"controller":@NO});
    // Restoring a lost bridge must not recreate an already selected keyboard HUD.
    if(!keyboard)select_input_mode(@"keyboard-ui");
    mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!mode || !*(bool *)object_unbox(mode))return false;
    input_capture(true);input_bridge(true,true);input_move(true);input_buttons(true);
    input_tune(@{@"menuMouse":@YES,@"watchFit":@YES,@"pcLayout":@YES});
    bool ready=input_capture_timer && input_mouse_bridge_enabled && input_move_enabled &&
        input_button_bridge_enabled && input_menu_mouse_handle;
    if(ready)input_auto_switches++;
    fprintf(output,"input_auto_activation=%s switches=%lu\n",ready ? "ready" : "incomplete",(unsigned long)input_auto_switches);
    return ready;
}

static void input_auto_configure(NSDictionary *command) {
    if(command[@"enabled"]){
        if(input_auto_monitor){[NSEvent removeMonitor:input_auto_monitor];input_auto_monitor=nil;}
        input_auto_window_update();
        if([command[@"enabled"] boolValue]){
            input_auto_last_check=0;
            NSEventMask mask=NSEventMaskKeyDown|NSEventMaskFlagsChanged|NSEventMaskMouseMoved|
                NSEventMaskLeftMouseDown|NSEventMaskRightMouseDown|NSEventMaskOtherMouseDown|NSEventMaskScrollWheel;
            input_auto_monitor=[NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *event){
                if(!NSApp.isActive || !event.window || event.window!=NSApp.keyWindow ||
                   (event.modifierFlags & NSEventModifierFlagCommand))return event;
                double now=NSProcessInfo.processInfo.systemUptime;
                if(now-input_auto_last_check<0.25)return event;
                input_auto_last_check=now;
                if(input_auto_activate() && event.type==NSEventTypeKeyDown){
                    // Keep the first held movement key; its key-up reaches the new monitor.
                    int index=-1;
                    switch(event.keyCode){case 13:index=0;break;case 0:index=1;break;case 1:index=2;break;case 2:index=3;break;}
                    if(index>=0){input_move_keys[index]=true;input_move_apply(true);}
                }
                return event;
            }];
            input_auto_window_update();
        }
    }
    fprintf(output,"input_auto=%s switches=%lu\n",input_auto_monitor ? "armed" : "disabled",(unsigned long)input_auto_switches);
}
