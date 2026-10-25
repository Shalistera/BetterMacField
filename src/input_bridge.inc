/* Inspect and select the client's existing keyboard input mode. */
static int input_original_mode;
static bool input_original_mode_saved;
static dispatch_source_t input_trace_timer;
static dispatch_source_t input_capture_timer;
static id input_mouse_monitor;
static bool input_cursor_captured;
static NSString *input_capture_reason;
static NSUInteger input_capture_ticks;
static double input_native_mouse_x,input_native_mouse_y;
static NSUInteger input_native_mouse_events;
static NSWindow *input_capture_window;
static BOOL input_previous_mouse_moved;
static bool input_mouse_bridge_enabled;
static bool input_mouse_bridge_to_camera;
static NSUInteger input_camera_calls;
static double input_camera_scale=1.0;
static NSUInteger input_mouse_events_queued;
static void input_queue_mouse(double x,double y);
static void input_neutral_camera(void);
static void *input_rewired_player(void);
static void *input_active_joystick(void);
static NSDictionary *input_camera_settings(void);
static bool input_move_enabled,input_move_active,input_joystick_was_enabled;
static bool input_move_keys[4]; // W, A, S, D
static uint32_t input_move_joystick_handle,input_move_player_handle;
static int input_move_forbid_type;
static id input_key_monitor;
static dispatch_source_t input_move_timer;
static NSUInteger input_move_updates,input_key_transitions;
static double input_key_dispatch_ms;
static float input_move_x,input_move_y;
static uint32_t input_scroll_panel_handle;
static NSUInteger input_scroll_events,input_zoom_calls;
static double input_last_scroll_delta;
static bool input_button_bridge_enabled,input_button_held[2];
static int input_mouse_key_codes[2],input_button_timings[2];
static NSUInteger input_button_events,input_button_actions;
static NSString *input_last_button_action;
static NSDictionary *input_last_button_state;
static int input_attack_forbid_type;
typedef enum { InputAttackNone,InputAttackNormal,InputAttackThrow,InputAttackCustom } InputAttackRoute;
static InputAttackRoute input_attack_route;
static uint32_t input_attack_owner_handle;
static void input_release_buttons(void);
static bool input_legacy_mouse_key_read(int key,int kind,bool *value);
static bool input_watch_fit_enabled;
static uint32_t input_watch_transform_handle;
static float input_watch_original_scale[3];
static bool input_ui_layout_enabled;

static void *input_camera_touch_panel(void) {
    void *resources=find_class("UnityEngine","Resources"),*type=find_class("Beyond.UI","UITouchPanel");
    const void *find=class_get_method_from_name(resources,"FindObjectsOfTypeAll",1);
    void *args[]={type_get_object(class_get_type(type))},*exception=NULL;
    void *array=runtime_invoke(find,NULL,args,&exception),*selected=NULL;int matches=0;bool ok=false;
    if(array && !exception)for(uintptr_t i=0;i<array_length(array);i++){
        void *panel=array_item(array,i),*go=invoke(panel,"get_gameObject",0,NULL,&ok);
        void *active=invoke(go,"get_activeInHierarchy",0,NULL,&ok);
        if(!ok || !active || !*(bool *)object_unbox(active))continue;
        // Map UI also owns UITouchPanel components. Only the scene Touch prefab
        // supplies the camera zoom and gameplay input group used by this bridge.
        void *name=invoke(panel,"get_name",0,NULL,&ok);
        if(!ok || ![managed_string(name) isEqual:@"UITouchPanel"])continue;
        void *transform=invoke(go,"get_transform",0,NULL,&ok);
        void *parent=invoke(transform,"get_parent",0,NULL,&ok);
        void *parentName=invoke(parent,"get_name",0,NULL,&ok);
        if(!ok || ![managed_string(parentName) isEqual:@"TouchPanel"])continue;
        void *event=field_reference(panel,"onZoom"),*listeners=field_reference(event,"m_calls");
        void *count=invoke(listeners,"get_Count",0,NULL,&ok);
        if(!ok || !count || *(int *)object_unbox(count)<=0)continue;
        fprintf(output,"input_scroll_panel=%s listeners=%d\n",managed_string(name).UTF8String,*(int *)object_unbox(count));
        selected=panel;matches++;
    }
    return matches==1 ? selected : NULL;
}

static bool input_queue_scroll(NSEvent *event) {
    input_scroll_events++;
    if(!input_scroll_panel_handle)return false;
    void *panel=gchandle_get_target(input_scroll_panel_handle);bool ok=false;
    void *enabled=invoke(panel,"get_groupEnabled",0,NULL,&ok);
    if(!ok || !enabled || !*(bool *)object_unbox(enabled))return false;
    void *field=class_get_field_from_name(object_get_class(panel),"_mouseZoomSensitivity");
    if(!field)return false;
    float sensitivity=0;field_get_value(panel,field,&sensitivity);
    // UITouchPanel.OnScroll multiplies scrollDelta.y by this authored value.
    // deltaY supplies line-sized increments for a wheel and fractional trackpad steps.
    float delta=(float)(event.deltaY*sensitivity);if(delta==0)return false;
    void *callback=field_reference(panel,"onZoom"),*args[]={&delta};
    invoke_typed(callback,"Invoke",1,"System.Single",args,&ok);
    if(ok){input_zoom_calls++;input_last_scroll_delta=delta;}
    return ok;
}


static void input_set_captured(bool captured) {
    if(captured==input_cursor_captured)return;
    if(captured) {
        CGError error=CGAssociateMouseAndMouseCursorPosition(false);
        if(error!=kCGErrorSuccess){fprintf(output,"input_capture_error=%d\n",error);return;}
        [NSCursor hide];
    } else {
        input_release_buttons();
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
        if(input_mouse_bridge_to_camera)input_neutral_camera();
    }
    input_cursor_captured=captured;
    fprintf(output,"input_cursor_captured=%d\n",captured);
}

static NSDictionary *input_playtools_state(void) {
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    Class klass=NSClassFromString(@"PlayTools.PlaySettings");
    result[@"classFound"]=@(klass!=Nil);
    if(klass){
        @try {
            id settings=[klass valueForKey:@"shared"];
            result[@"sharedFound"]=@(settings!=nil);
            unsigned int count=0;Ivar *ivars=class_copyIvarList(klass,&count);
            NSMutableArray *fields=[NSMutableArray array];
            for(unsigned int i=0;i<count;i++) {
                const char *name=ivar_getName(ivars[i]);
                if(!strcasestr(name,"keymapping") && !strcasestr(name,"mouse"))continue;
                ptrdiff_t offset=ivar_getOffset(ivars[i]);
                unsigned char byte=*((unsigned char *)(__bridge void *)settings+offset);
                [fields addObject:@{@"name":@(name),@"encoding":@(ivar_getTypeEncoding(ivars[i]) ?: "?"),@"firstByte":@(byte)}];
            }
            free(ivars);result[@"inputIvars"]=fields;
            result[@"disableBuiltinMouse"]=[settings valueForKey:@"disableBuiltinMouse"] ?: NSNull.null;
        } @catch(NSException *e){result[@"exception"]=e.reason ?: e.name;}
    }
    return result;
}

static void input_ui(bool refresh) {
    void *resources=find_class("UnityEngine","Resources"),*klass=find_class("Beyond.UI","UICanvasScaleHelper");
    if(!resources || !klass){fprintf(output,"input_ui=unavailable\n");return;}
    const void *find=class_get_method_from_name(resources,"FindObjectsOfTypeAll",1);
    void *args[]={type_get_object(class_get_type(klass))},*exception=NULL;
    void *array=runtime_invoke(find,NULL,args,&exception);NSMutableArray *rows=[NSMutableArray array];
    if(array && !exception)for(uintptr_t i=0;i<array_length(array) && i<100;i++) {
        void *helper=array_item(array,i);bool ok=false;
        if(refresh){
            void *canvases=field_reference(helper,"m_canvasList"),*count=invoke(canvases,"get_Count",0,NULL,&ok);
            if(ok && count && *(int *)object_unbox(count)>0){
                // _OnInputTypeChanged skips WorldSpace canvases. UpdateCanvas also
                // recalculates their transform against the current UI camera.
                invoke(helper,"UpdateCanvas",0,NULL,&ok);
                invoke(helper,"ForceCanvasUpdate",0,NULL,&ok);
            }
        }
        NSMutableDictionary *row=[NSMutableDictionary dictionaryWithObjectsAndKeys:object_name(helper),@"name",render_value(helper,1),@"fields",nil];
        const char *getters[]={"get_standardHorizontalResolution","get_standardVerticalResolution","get_standardResolutionScale","GetProperCanvasResolution","CalcScreenCanvasParams"};
        for(int j=0;j<5;j++){void *value=invoke(helper,getters[j],0,NULL,&ok);if(ok)row[@(getters[j])]=render_value(value,1);}
        NSMutableArray *scalers=[NSMutableArray array];
        void *set=field_reference(helper,"m_canvasScalerList"),*iterator=invoke(set,"GetEnumerator",0,NULL,&ok);
        for(int j=0;iterator && j<100;j++){
            void *more=invoke(iterator,"MoveNext",0,NULL,&ok);if(!ok || !more || !*(bool *)object_unbox(more))break;
            void *scaler=invoke(iterator,"get_Current",0,NULL,&ok);if(!ok || !scaler)break;
            NSMutableDictionary *entry=[NSMutableDictionary dictionaryWithObjectsAndKeys:object_name(scaler),@"name",render_value(scaler,1),@"fields",nil];
            void *transform=invoke(scaler,"get_transform",0,NULL,&ok);
            for(const char **g=(const char *[]) {"get_rect","get_localPosition","get_position","get_localScale","get_lossyScale",NULL};*g;g++){
                void *v=invoke(transform,*g,0,NULL,&ok);if(ok && v)entry[@(*g)]=render_value(v,1);
            }
            [scalers addObject:entry];
        }
        row[@"scalers"]=scalers;
        void *camera=field_reference(helper,"uiCamera");
        if(camera){
            NSMutableDictionary *cameraState=[NSMutableDictionary dictionary];
            for(const char **g=(const char *[]) {"get_fieldOfView","get_orthographicSize","get_pixelRect","get_rect",NULL};*g;g++){
                void *v=invoke(camera,*g,0,NULL,&ok);if(ok && v)cameraState[@(*g)]=render_value(v,1);
            }
            row[@"camera"]=cameraState;
        }
        [rows addObject:row];
    }
    [[NSJSONSerialization dataWithJSONObject:rows options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-input-ui.json") atomically:YES];
    NSMutableArray *cameras=[NSMutableArray array];
    const void *allCameras=class_get_method_from_name(find_class("UnityEngine","Camera"),"get_allCameras",0);
    void *cameraArray=allCameras ? runtime_invoke(allCameras,NULL,NULL,&exception) : NULL;
    if(cameraArray && !exception)for(uintptr_t i=0;i<array_length(cameraArray);i++){
        void *camera=array_item(cameraArray,i);bool ok=false;
        NSMutableDictionary *entry=[NSMutableDictionary dictionaryWithObject:object_name(camera) forKey:@"name"];
        for(const char **g=(const char *[]) {"get_fieldOfView","get_projectionMatrix","get_cullingMask","get_depth","get_orthographic","get_rect",NULL};*g;g++){
            void *v=invoke(camera,*g,0,NULL,&ok);if(ok && v)entry[@(*g)]=render_value(v,1);
        }
        [cameras addObject:entry];
    }
    [[NSJSONSerialization dataWithJSONObject:cameras options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-input-ui-cameras.json") atomically:YES];
    fprintf(output,"input_ui=complete helpers=%lu refreshed=%d\n",(unsigned long)rows.count,refresh);
}

static void *input_static(void *klass,const char *name,int argc,void **args) {
    if(!klass)return NULL;
    const void *method=class_get_method_from_name(klass,name,argc);
    if(!method){fprintf(output,"input_missing_method=%s\n",name);return NULL;}
    if(!(method_get_flags(method,NULL)&0x10)){fprintf(output,"input_rejected_instance_method=%s\n",name);return NULL;}
    void *exception=NULL,*value=runtime_invoke(method,NULL,args,&exception);
    if(exception){fprintf(output,"input_exception=%s method=%s\n",class_get_name(object_get_class(exception)),name);return NULL;}
    return value;
}

static void *input_manager(void) {
    return input_static(find_class("Beyond.Input","InputManager"),"get_instance",0,NULL);
}

static bool input_unity_alive(void *object) {
    if(!object)return false;
    void *args[]={object};
    void *alive=input_static(find_class("UnityEngine","Object"),"op_Implicit",1,args);
    return alive && *(bool *)object_unbox(alive);
}

static void *input_game_controller(void) {
    return input_static(find_class("Beyond.Gameplay","GameInstance"),"get_playerController",0,NULL);
}

static bool input_read_bool(void *object,const char *name) {
    void *field=object ? class_get_field_from_name(object_get_class(object),name) : NULL;
    bool value=false;if(field)field_get_value(object,field,&value);return value;
}

#include "hud_keys.inc"
#include "skill_keys.inc"
#include "keyboard_input.inc"

static bool input_button_actions_for(int button,bool down,bool trigger,NSMutableArray *rows) {
    void *manager=input_manager(),*bindings=field_reference(manager,"m_bindingInfos");bool ok=false;
    void *iterator=invoke(bindings,"GetEnumerator",0,NULL,&ok);
    NSMutableSet *triggered=[NSMutableSet set];bool handled=false;
    for(int i=0;iterator && i<2000;i++){
        void *moved=invoke(iterator,"MoveNext",0,NULL,&ok);if(!ok || !moved || !*(bool *)object_unbox(moved))break;
        void *pair=invoke(iterator,"get_Current",0,NULL,&ok),*binding=invoke(pair,"get_Value",0,NULL,&ok);
        NSString *action=managed_string(field_reference(binding,"playerActionId"));
        NSString *lower=action.lowercaseString;
        if(![lower containsString:@"attack"] && ![lower containsString:@"sprint"] && ![lower containsString:@"dash"])continue;
        if([lower containsString:@"debug"])continue;
        int level=0;void *args[]={string_new(action.UTF8String),&level};
        void *info=invoke_typed(manager,"GetPlayerActionInfo",2,"System.String",args,&ok);
        if(!ok || !info)continue;
        for(const char **fieldName=(const char *[]){"primaryKeyboardInput","secondaryKeyboardInput",NULL};*fieldName;fieldName++){
            void *input=field_reference(info,*fieldName);
            if(!input || input_read_bool(input,"useCtrl") || input_read_bool(input,"useShift") || input_read_bool(input,"useAlt"))continue;
            void *keyField=class_get_field_from_name(object_get_class(input),"key"),*timingField=class_get_field_from_name(object_get_class(input),"timingType");
            int key=-1,timing=-1;if(keyField)field_get_value(input,keyField,&key);if(timingField)field_get_value(input,timingField,&timing);
            if(key!=input_mouse_key_codes[button] || timing!=input_button_timings[down ? 0 : 1])continue;
            void *idValue=invoke(binding,"get_id",0,NULL,&ok);if(!ok || !idValue)continue;
            int bindingId=*(int *)object_unbox(idValue);bool needLogError=false;void *enabledArgs[]={&bindingId,&needLogError};
            void *enabled=invoke(manager,"IsBindingEnabled",2,enabledArgs,&ok);
            bool canTrigger=ok && enabled && *(bool *)object_unbox(enabled);
            if(!trigger && canTrigger)handled=true;
            if(rows)[rows addObject:@{@"action":action,@"binding":@(bindingId),@"button":@(button),@"down":@(down),@"enabled":@(canTrigger)}];
            if(trigger && canTrigger && ![triggered containsObject:action]){
                invoke(binding,"Trigger",0,NULL,&ok);
                if(ok){handled=true;input_button_actions++;input_last_button_action=action;[triggered addObject:action];}
            }
        }
    }
    return handled;
}

static void *input_battle_manager(void) {
    void *world=find_class("Beyond.Gameplay.Core","GameWorld");
    void *field=world ? class_get_field_from_name(world,"battle") : NULL,*battle=NULL;
    if(field && (field_get_flags(field)&0x10))field_static_get_value(field,&battle);
    return battle;
}

static bool input_in_throw_mode(void) {
    bool ok=false;void *value=invoke(input_battle_manager(),"get_inThrowMode",0,NULL,&ok);
    return ok && value && *(bool *)object_unbox(value);
}

static bool input_trigger_button(int button,bool down,bool cancelled) {
    bool ok=false;
    if(button==0){
        void *panel=input_scroll_panel_handle ? gchandle_get_target(input_scroll_panel_handle) : NULL;
        void *player=input_game_controller();if(!player)return false;
        if(down){
            void *enabled=invoke(panel,"get_groupEnabled",0,NULL,&ok);
            if(!ok || !enabled || !*(bool *)object_unbox(enabled) || !input_button_actions_for(0,true,false,nil))return false;
            void *character=field_reference(player,"mainCharacter");
            if(input_in_throw_mode()){
                void *owner=invoke(character,"get_interactiveInstigatorCtrl",0,NULL,&ok);
                if(!ok || !owner || !class_get_method_from_name(object_get_class(owner),"CastThrowSkill",0))return false;
                input_attack_owner_handle=gchandle_new(owner,false);input_attack_route=InputAttackThrow;
                input_last_button_action=@"native_throw_arm";
            }else{
                void *custom=invoke(character,"get_customAbilityCom",0,NULL,&ok);
                void *active=invoke(custom,"IsInCustomAbility",0,NULL,&ok);
                if(ok && active && *(bool *)object_unbox(active)){
                    invoke(custom,"UseAbility",0,NULL,&ok);
                    if(ok){input_attack_owner_handle=gchandle_new(custom,false);input_attack_route=InputAttackCustom;input_last_button_action=@"native_custom_press";}
                }else{
                    void *gamePlayer=input_static(find_class("Beyond.Gameplay","GameInstance"),"get_player",0,NULL);
                    void *forbids=field_reference(gamePlayer,"forbidSystem"),*args[]={&input_attack_forbid_type};
                    void *forbidden=invoke(forbids,"IsForbidden",1,args,&ok);
                    if(!ok || !forbidden || *(bool *)object_unbox(forbidden))return false;
                    void *canPress=invoke(player,"get_canPressAttackButton",0,NULL,&ok);
                    if(!ok || !canPress || !*(bool *)object_unbox(canPress))return false;
                    invoke(player,"StartCastNormalAttack",0,NULL,&ok);
                    if(ok){input_attack_route=InputAttackNormal;input_last_button_action=@"native_attack_press";}
                }
            }
        }else{
            void *owner=input_attack_owner_handle ? gchandle_get_target(input_attack_owner_handle) : NULL;
            if(input_attack_route==InputAttackNormal){
                invoke(player,"EndCastNormalAttack",0,NULL,&ok);input_last_button_action=@"native_attack_release";
            }else if(input_attack_route==InputAttackThrow){
                void *current=invoke(field_reference(player,"mainCharacter"),"get_interactiveInstigatorCtrl",0,NULL,&ok);
                if(!cancelled && owner && current==owner && input_in_throw_mode()){
                    invoke(owner,"CastThrowSkill",0,NULL,&ok);input_last_button_action=@"native_throw_release";
                }else{ok=true;input_last_button_action=@"native_throw_cancel";}
            }else if(input_attack_route==InputAttackCustom){
                invoke(owner,"StopAbility",0,NULL,&ok);input_last_button_action=@"native_custom_release";
            }
            if(input_attack_owner_handle){gchandle_free(input_attack_owner_handle);input_attack_owner_handle=0;}
            input_attack_route=InputAttackNone;
        }
        if(ok)input_button_actions++;
    }else ok=input_button_actions_for(button,down,true,nil);
    if(ok){
        NSMutableDictionary *state=[NSMutableDictionary dictionaryWithObjectsAndKeys:@(button),@"button",@(down),@"down",nil];
        state[@"action"]=input_last_button_action ?: @"";
        state[@"route"]=@(input_attack_route);
        void *player=input_game_controller();bool readOK=false;
        const char *getters[]={"get_castingNormalAttack","get_canPressAttackButton","get_currentDashCount","get_isMainCharacterSprinting"};
        for(int i=0;i<4;i++){void *value=invoke(player,getters[i],0,NULL,&readOK);if(readOK && value)state[@(getters[i])]=render_value(value,0);}
        input_last_button_state=state;
        NSData *json=[NSJSONSerialization dataWithJSONObject:state options:0 error:nil];
        fprintf(output,"input_button_state=%s\n",[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
    }
    return ok;
}

static void input_release_buttons(void) {
    for(int i=0;i<2;i++)if(input_button_held[i]){
        input_button_held[i]=false;input_trigger_button(i,false,true);
    }
}

static void input_buttons(bool enabled) {
    input_release_buttons();input_button_bridge_enabled=false;
    if(enabled){
        void *forbidField=class_get_field_from_name(find_class("Beyond.Gameplay","ForbidType"),"ForbidAttack");
        if(!forbidField){fprintf(output,"input_buttons=forbid_unavailable\n");return;}
        field_static_get_value(forbidField,&input_attack_forbid_type);
        void *keys=find_class("Beyond.Input","KeyboardKeyCode"),*timings=find_class("Beyond.Input","InputTimingType");
        const char *keyNames[]={"Mouse0","Mouse1"},*timingNames[]={"OnPress","OnRelease"};
        for(int i=0;i<2;i++){
            void *key=class_get_field_from_name(keys,keyNames[i]),*timing=class_get_field_from_name(timings,timingNames[i]);
            if(!key || !timing){fprintf(output,"input_buttons=enum_unavailable\n");return;}
            field_static_get_value(key,&input_mouse_key_codes[i]);field_static_get_value(timing,&input_button_timings[i]);
        }
        input_button_bridge_enabled=true;
    }
    NSMutableArray *rows=[NSMutableArray array];
    if(enabled)for(int i=0;i<2;i++)for(int down=0;down<2;down++)input_button_actions_for(i,down,false,rows);
    [[NSJSONSerialization dataWithJSONObject:rows options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-input-button-bindings.json") atomically:YES];
    fprintf(output,"input_buttons=%s bindings=%lu\n",enabled ? "enabled" : "disabled",(unsigned long)rows.count);
}

static void input_hud(void) {
    void *resource=find_class("Beyond.Resource","ResourceManager");
    const void *exists=typed_method(resource,"CheckExists",2,"System.String");
    int category=0;void *categoryField=class_get_field_from_name(find_class("Beyond.Resource","RootCategory"),"Main");
    if(categoryField)field_static_get_value(categoryField,&category);
    NSMutableArray *assets=[NSMutableArray array],*components=[NSMutableArray array];
    for(NSString *name in @[@"MainHud",@"BattleAction",@"SquadIcon",@"Touch",@"Joystick"]){
        NSString *folder=[@[@"BattleAction",@"SquadIcon"] containsObject:name] ? @"Battle" : ([@[@"Touch",@"Joystick"] containsObject:name] ? @"Common" : name);
        for(NSString *suffix in @[@"",@"_PC",@"_Controller"]){
            NSString *path=[NSString stringWithFormat:@"Assets/Beyond/DynamicAssets/Gameplay/UI/Prefabs/%@/%@Panel%@.prefab",folder,name,suffix];
            void *args[]={string_new(path.UTF8String),&category},*exception=NULL;
            void *value=exists ? runtime_invoke(exists,NULL,args,&exception) : NULL;
            [assets addObject:@{@"path":path,@"exists":@(value && !exception && *(bool *)object_unbox(value))}];
        }
    }
    void *resources=find_class("UnityEngine","Resources");
    const void *find=class_get_method_from_name(resources,"FindObjectsOfTypeAll",1);
    for(NSString *name in @[@"SkillButton",@"SquadIcon"]){
        void *type=find_class("Beyond.UI",name.UTF8String),*args[]={type_get_object(class_get_type(type))},*exception=NULL;
        void *array=runtime_invoke(find,NULL,args,&exception);
        if(!array || exception)continue;
        for(uintptr_t i=0;i<array_length(array) && i<100;i++){
            void *object=array_item(array,i);bool ok=false;
            void *go=invoke(object,"get_gameObject",0,NULL,&ok),*active=invoke(go,"get_activeInHierarchy",0,NULL,&ok);
            if(!active || !*(bool *)object_unbox(active))continue;
            NSMutableArray *path=[NSMutableArray array];void *transform=invoke(go,"get_transform",0,NULL,&ok);
            for(int j=0;transform && j<12;j++){
                void *n=invoke(transform,"get_name",0,NULL,&ok);if(n)[path insertObject:managed_string(n) atIndex:0];
                transform=invoke(transform,"get_parent",0,NULL,&ok);
            }
            NSMutableDictionary *row=[NSMutableDictionary dictionaryWithObjectsAndKeys:name,@"type",[path componentsJoinedByString:@"/"],@"path",nil];
            for(NSString *fieldName in @[@"m_panelType",@"m_charIndex",@"m_actionId"]){
                void *field=class_get_field_from_name(type,fieldName.UTF8String);
                if(field)row[fieldName]=render_value(field_get_value_object(field,object),0);
            }
            [components addObject:row];
        }
    }
    NSDictionary *result=@{@"assets":assets,@"components":components};
    [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-input-hud.json") atomically:YES];
    fprintf(output,"input_hud=complete components=%lu\n",(unsigned long)components.count);
}

static void *input_game_camera(void) {
    void *klass=find_class("Beyond.Gameplay","GameInstance"),*camera=NULL;
    void *field=klass ? class_get_field_from_name(klass,"cameraManager") : NULL;
    if(field)field_static_get_value(field,&camera);
    return camera;
}

/* Rendering controls remain available while menus release the gameplay cursor. */
static bool input_scene_is_loaded(void) {
    void *player=input_game_controller();
    return player && field_reference(player,"mainCharacter") && input_game_camera();
}

static bool input_scene_is_ready(void) {
    void *player=input_game_controller(),*manager=input_manager();
    void *args[]={string_new("MainHudPanel")};
    void *hud=input_static(find_class("UnityEngine","GameObject"),"Find",1,args);
    bool ok=false;
    void *cursor=invoke(field_reference(manager,"m_realCursorManager"),"DumpState",0,NULL,&ok);
    NSString *active=ok && cursor ? [managed_string(cursor) componentsSeparatedByString:@"\n"].firstObject : @"";
    return player && field_reference(player,"mainCharacter") && input_game_camera() && hud &&
        input_read_bool(manager,"needProcessTryChange") && [active containsString:@"show=False"] &&
        [active containsString:@"isUsingVirtualMouse=False"];
}

static void input_scene_ready(void) {
    fprintf(output,"input_scene_ready=%d\n",input_scene_is_ready());
}

static void input_neutral_camera(void) {
    float zero=0;void *args[]={&zero,&zero};bool ok=false;
    invoke(input_game_camera(),"OnInput",2,args,&ok);
}

static void *input_active_joystick(void) {
    void *resources=find_class("UnityEngine","Resources"),*type=find_class("Beyond.UI","UIJoystick");
    const void *find=class_get_method_from_name(resources,"FindObjectsOfTypeAll",1);
    void *args[]={type_get_object(class_get_type(type))},*exception=NULL;
    void *array=runtime_invoke(find,NULL,args,&exception);bool ok=false;
    if(array && !exception)for(uintptr_t i=0;i<array_length(array);i++){
        void *candidate=array_item(array,i),*gameobject=invoke(candidate,"get_gameObject",0,NULL,&ok);
        void *active=invoke(gameobject,"get_activeInHierarchy",0,NULL,&ok);
        if(ok && active && *(bool *)object_unbox(active))return candidate;
    }
    return NULL;
}

static bool input_move_allowed(void *joystick,void *player) {
    if(!NSApp.isActive || (NSEvent.modifierFlags & NSEventModifierFlagCommand))return false;
    void *mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!mode || !*(bool *)object_unbox(mode))return false;
    bool ok=false;
    void *parent=field_reference(joystick,"<parent>k__BackingField");
    void *enabled=invoke(parent,"get_groupEnabled",0,NULL,&ok);
    if(!ok || !enabled || !*(bool *)object_unbox(enabled))return false;
    void *blocked=invoke(player,"get_blockPlayerInput",0,NULL,&ok);
    if(!ok || !blocked || *(bool *)object_unbox(blocked))return false;
    void *gameplayer=input_static(find_class("Beyond.Gameplay","GameInstance"),"get_player",0,NULL);
    void *forbids=field_reference(gameplayer,"forbidSystem"),*args[]={&input_move_forbid_type};
    void *forbidden=invoke(forbids,"IsForbidden",1,args,&ok);
    return ok && forbidden && !*(bool *)object_unbox(forbidden);
}

static void input_move_apply(bool allow) {
    if(!input_move_enabled)return;
    void *joystick=gchandle_get_target(input_move_joystick_handle);
    void *player=gchandle_get_target(input_move_player_handle);bool ok=false;
    if(!allow || !input_move_allowed(joystick,player))memset(input_move_keys,0,sizeof(input_move_keys));
    float axis[2]={(float)input_move_keys[3]-input_move_keys[1],(float)input_move_keys[0]-input_move_keys[2]};
    if(axis[0] && axis[1]){axis[0]*=0.70710678f;axis[1]*=0.70710678f;}
    bool moving=axis[0]!=0 || axis[1]!=0;
    if(axis[0]==input_move_x && axis[1]==input_move_y && moving==input_move_active)return;
    if(moving && !input_move_active){invoke(player,"ProduceMoveCommand",0,NULL,&ok);if(!ok)return;}
    void *args[]={axis};
    // The existing Lua update reads jsValue each frame. Keep its data synchronized
    // while the disabled component no longer generates pointer/drag events.
    invoke_typed(joystick,"set_jsValue",1,"UnityEngine.Vector2",args,&ok);
    if(!ok)return;
    invoke_typed(player,"UpdateMoveCommand",1,"UnityEngine.Vector2",args,&ok);
    if(!ok)return;
    if(!moving && input_move_active)invoke(player,"ConsumeMoveCommand",0,NULL,&ok);
    input_move_active=moving;input_move_x=axis[0];input_move_y=axis[1];input_move_updates++;
}

static void input_move(bool enabled) {
    input_skill_keys_cancel();
    input_hud_keys_cancel();
    input_keyboard_start(false);
    if(input_key_monitor){[NSEvent removeMonitor:input_key_monitor];input_key_monitor=nil;}
    input_hud_keys_ready=false;
    if(input_move_timer){dispatch_source_cancel(input_move_timer);input_move_timer=NULL;}
    if(input_move_enabled){
        input_move_apply(false);bool ok=false;void *args[]={&input_joystick_was_enabled};
        invoke(gchandle_get_target(input_move_joystick_handle),"set_enabled",1,args,&ok);
    }
    input_move_enabled=false;input_move_active=false;input_move_x=0;input_move_y=0;
    memset(input_move_keys,0,sizeof(input_move_keys));
    if(input_move_joystick_handle)gchandle_free(input_move_joystick_handle);
    if(input_move_player_handle)gchandle_free(input_move_player_handle);
    input_move_joystick_handle=0;input_move_player_handle=0;
    if(!enabled){fprintf(output,"input_move=disabled\n");return;}
    void *joystick=input_active_joystick(),*player=input_game_controller();bool ok=false;
    void *forbidClass=find_class("Beyond.Gameplay","ForbidType");
    void *forbidField=forbidClass ? class_get_field_from_name(forbidClass,"ForbidMove") : NULL;
    if(!joystick || !player || !forbidField ||
       !typed_method(object_get_class(player),"UpdateMoveCommand",1,"UnityEngine.Vector2") ||
       !typed_method(object_get_class(joystick),"set_jsValue",1,"UnityEngine.Vector2")){
        fprintf(output,"input_move=scene_unavailable\n");return;
    }
    field_static_get_value(forbidField,&input_move_forbid_type);
    void *old=invoke(joystick,"get_enabled",0,NULL,&ok);
    if(!ok || !old){fprintf(output,"input_move=joystick_unavailable\n");return;}
    input_joystick_was_enabled=*(bool *)object_unbox(old);
    bool disable=false;void *args[]={&disable};invoke(joystick,"set_enabled",1,args,&ok);
    if(!ok){fprintf(output,"input_move=disable_failed\n");return;}
    input_move_joystick_handle=gchandle_new(joystick,false);input_move_player_handle=gchandle_new(player,false);
    input_move_enabled=true;
    input_hud_keys_init();
    if(input_keyboard_experiment_enabled)
        fprintf(output,"input_keyboard_ready=%d\n",input_keyboard_start(true));
    input_key_monitor=[NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown|NSEventMaskKeyUp|NSEventMaskFlagsChanged) handler:^NSEvent *(NSEvent *event){
        if(input_native_overlay_visible()){
            memset(input_move_keys,0,sizeof(input_move_keys));input_move_apply(false);input_skill_keys_cancel();input_hud_keys_cancel();input_keyboard_release();return event;
        }
        input_keyboard_modifiers(event);
        // The shared keyboard candidate owns E/Shift through native InputManager.
        bool sharedCombatKey=input_keyboard_handle && (event.keyCode==0x0e || event.keyCode==0x38 || event.keyCode==0x3c);
        if(!sharedCombatKey && input_hud_key_event(event))return nil;
        if(input_skill_key_event(event))return nil;
        int index=-1;
        switch(event.keyCode){case 13:index=0;break;case 0:index=1;break;case 1:index=2;break;case 2:index=3;break;}
        if(index>=0 && !input_move_allowed(gchandle_get_target(input_move_joystick_handle),gchandle_get_target(input_move_player_handle))){
            // Photo controls and focused UI keep their original WASD bindings
            // while gameplay movement is disabled by the game's own groups.
            input_move_apply(false);input_keyboard_event(event);return event;
        }
        if(index>=0 && (event.type==NSEventTypeKeyDown || event.type==NSEventTypeKeyUp)){
            bool down=event.type==NSEventTypeKeyDown;
            if(input_move_keys[index]!=down){
                input_move_keys[index]=down;input_key_transitions++;input_move_apply(true);
                input_key_dispatch_ms=(NSProcessInfo.processInfo.systemUptime-event.timestamp)*1000;
            }
        }else if(event.type==NSEventTypeFlagsChanged)input_move_apply(true);
        // Existing direct movement/skill/HUD bridges already own their keys.
        if(index<0 || (event.type==NSEventTypeKeyUp && input_keyboard_held[event.keyCode]))input_keyboard_event(event);
        return event;
    }];
    input_move_timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    dispatch_source_set_timer(input_move_timer,DISPATCH_TIME_NOW,NSEC_PER_SEC/120,NSEC_PER_MSEC);
    dispatch_source_set_event_handler(input_move_timer,^{ @autoreleasepool {
        if(input_move_active)input_move_apply(true);
        if(!NSApp.isActive){input_keyboard_release();input_hud_keys_cancel();}
        if(!NSApp.isActive || (NSEvent.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand)))input_skill_keys_cancel();
    }});
    dispatch_resume(input_move_timer);
    fprintf(output,"input_move=enabled joystick_tick_disabled=1 forbid_type=%d\n",input_move_forbid_type);
}

static void input_watch_restore(void) {
    if(!input_watch_transform_handle)return;
    bool ok=false;void *args[]={input_watch_original_scale};
    invoke(gchandle_get_target(input_watch_transform_handle),"set_localScale",1,args,&ok);
    gchandle_free(input_watch_transform_handle);input_watch_transform_handle=0;
    fprintf(output,"input_watch_restore=%d\n",ok);
}

static void input_watch_fit_update(void) {
    if(!input_watch_fit_enabled)return;
    void *mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!mode || !*(bool *)object_unbox(mode)){input_watch_restore();return;}
    void *args[]={string_new("WatchPanel")};
    void *panel=input_static(find_class("UnityEngine","GameObject"),"Find",1,args);
    if(!panel)return;
    bool ok=false;void *transform=invoke(panel,"get_transform",0,NULL,&ok);
    if(!ok || !transform)return;
    bool same=input_watch_transform_handle && gchandle_get_target(input_watch_transform_handle)==transform;
    if(!same)input_watch_restore();
    void *scale=invoke(transform,"get_localScale",0,NULL,&ok);
    if(!ok || !scale)return;
    if(!same)memcpy(input_watch_original_scale,object_unbox(scale),sizeof(input_watch_original_scale));
    float fitted[3];for(int i=0;i<3;i++)fitted[i]=input_watch_original_scale[i]*0.75f;
    if(same && memcmp(object_unbox(scale),fitted,sizeof(fitted))==0)return;
    void *scaleArgs[]={fitted};invoke(transform,"set_localScale",1,scaleArgs,&ok);
    if(ok && !same)input_watch_transform_handle=gchandle_new(transform,false);
    fprintf(output,"input_watch_fit=%d multiplier=0.75\n",ok);
}

#include "menu_input.inc"
#include "squad_tips.inc"
#include "ui_layout.inc"
#include "hud_visibility.inc"

static void input_capture(bool enabled) {
    if(input_capture_timer){dispatch_source_cancel(input_capture_timer);input_capture_timer=NULL;}
    if(input_mouse_monitor){[NSEvent removeMonitor:input_mouse_monitor];input_mouse_monitor=nil;}
    input_set_captured(false);
    input_window_edge_drag=false;
    input_watch_restore();
    input_ui_layout_restore();
    input_squad_tips_reset();
    input_hud_visibility_reset();
    if(!enabled)input_menu_start(false);
    if(input_capture_window){input_capture_window.acceptsMouseMovedEvents=input_previous_mouse_moved;input_capture_window=nil;}
    if(enabled) {
        input_mouse_monitor=[NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskMouseMoved|NSEventMaskLeftMouseDragged|NSEventMaskRightMouseDragged|NSEventMaskOtherMouseDragged|NSEventMaskScrollWheel|NSEventMaskLeftMouseDown|NSEventMaskLeftMouseUp|NSEventMaskRightMouseDown|NSEventMaskRightMouseUp) handler:^NSEvent *(NSEvent *event){
            if(input_window_event(event))return event;
            if(input_native_overlay_visible()){input_set_captured(false);input_menu_release();input_release_buttons();return event;}
            if(event.type==NSEventTypeLeftMouseDown || event.type==NSEventTypeLeftMouseUp || event.type==NSEventTypeRightMouseDown || event.type==NSEventTypeRightMouseUp){
                int button=(event.type==NSEventTypeRightMouseDown || event.type==NSEventTypeRightMouseUp) ? 1 : 0;
                bool down=event.type==NSEventTypeLeftMouseDown || event.type==NSEventTypeRightMouseDown;
                bool allowed=input_button_bridge_enabled && input_cursor_captured && NSApp.isActive && !(event.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand));
                if((down && allowed) || (!down && input_button_held[button])){
                    input_button_events++;bool handled=input_trigger_button(button,down,!down && !allowed);
                    input_button_held[button]=down && handled;if(handled)return nil;
                }
                return input_menu_event(event) ? nil : event;
            }
            if(event.type==NSEventTypeScrollWheel){
                if(input_mouse_bridge_enabled && input_mouse_bridge_to_camera && input_cursor_captured && NSApp.isActive && !(event.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand))){
                    if(input_queue_scroll(event))return nil;
                }
                return input_menu_event(event) ? nil : event;
            }
            if(input_menu_event(event))return nil;
            input_native_mouse_x+=event.deltaX;input_native_mouse_y+=event.deltaY;input_native_mouse_events++;
            if(input_mouse_bridge_enabled && input_cursor_captured && NSApp.isActive && !(event.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand)))input_queue_mouse(event.deltaX,-event.deltaY);
            return event;
        }];
        input_capture_timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
        dispatch_source_set_timer(input_capture_timer,DISPATCH_TIME_NOW,NSEC_PER_SEC/30,NSEC_PER_MSEC);
        __block unsigned int layoutTick=0;
        dispatch_source_set_event_handler(input_capture_timer,^{ @autoreleasepool {
            input_capture_ticks++;
            if(layoutTick++%15==0){input_ui_layout_update();input_watch_fit_update();input_squad_tips_update();input_hud_visibility_update();}
            bool desired=false;
            NSString *reason=!NSApp.isActive ? @"app_inactive" : (!NSApp.keyWindow ? @"no_key_window" : @"modifier_release");
            if(NSApp.isActive && NSApp.keyWindow && !input_native_overlay_visible() && !(NSEvent.modifierFlags & (NSEventModifierFlagOption|NSEventModifierFlagCommand))) {
                void *mode=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
                void *real=field_reference(input_manager(),"m_realCursorManager");bool ok=false;
                void *value=invoke(real,"DumpState",0,NULL,&ok);
                reason=@"device_or_cursor_unavailable";
                if(mode && *(bool *)object_unbox(mode) && ok && value) {
                    NSString *active=[managed_string(value) componentsSeparatedByString:@"\n"].firstObject;
                    desired=[active containsString:@"show=False"] && [active containsString:@"isUsingVirtualMouse=False"];
                    reason=desired ? @"scene_capture" : [@"game_request: " stringByAppendingString:active ?: @""];
                }
                if((desired || input_menu_mouse_handle) && input_capture_window!=NSApp.keyWindow) {
                    if(input_capture_window)input_capture_window.acceptsMouseMovedEvents=input_previous_mouse_moved;
                    input_capture_window=NSApp.keyWindow;input_previous_mouse_moved=input_capture_window.acceptsMouseMovedEvents;
                    input_capture_window.acceptsMouseMovedEvents=YES;
                }
            }
            if(![input_capture_reason isEqual:reason]){
                input_capture_reason=reason;
                fprintf(output,"input_capture_reason=%s app_active=%d key_window=%ld modifiers=%lu\n",reason.UTF8String,
                    NSApp.isActive,(long)NSApp.keyWindow.windowNumber,(unsigned long)NSEvent.modifierFlags);
            }
            input_set_captured(desired && !input_window_edge_drag);
            if(desired || !NSApp.isActive || input_native_overlay_visible())atomic_store(&input_legacy_pointer_active,false);
            if(!NSApp.isActive || input_native_overlay_visible())input_menu_release();
        }});
        dispatch_resume(input_capture_timer);
    }
    fprintf(output,"input_capture=%s\n",enabled ? "enabled" : "disabled");
}

static NSDictionary *input_type_description(void *klass,void *instance) {
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    if(!klass)return result;
    NSMutableArray *methods=[NSMutableArray array];void *iter=NULL;const void *method;
    while((method=class_get_methods(klass,&iter))) {
        const char *name=method_get_name(method);
        if(!strstr(name,"Input") && !strstr(name,"Keyboard") && !strstr(name,"Mouse") && !strstr(name,"Controller") && !strstr(name,"Touch") && !strstr(name,"GetKey") && strcmp(name,"GetAxis"))continue;
        NSMutableArray *params=[NSMutableArray array];
        for(uint32_t p=0;p<method_get_param_count(method);p++){char *s=type_get_name(method_get_param(method,p));[params addObject:@(s ?: "?")];if(s && runtime_free)runtime_free(s);}
        char *ret=type_get_name(method_get_return_type(method));
        [methods addObject:@{@"name":@(name),@"return":@(ret ?: "?"),@"parameters":params}];
        if(ret && runtime_free)runtime_free(ret);
    }
    result[@"methods"]=methods;
    NSMutableDictionary *fields=[NSMutableDictionary dictionary];iter=NULL;void *field;
    while((field=class_get_fields(klass,&iter))) {
        const char *name=field_get_name(field);bool isstatic=(field_get_flags(field)&0x10)!=0;
        if(!instance && !isstatic)continue;
        if(strcmp(class_get_name(klass),"InputManager") && !strcasestr(name,"input") && !strcasestr(name,"keyboard") && !strcasestr(name,"mouse") && !strcasestr(name,"controller") && !strcasestr(name,"touch"))continue;
        char *type=type_get_name(field_get_type(field));
        if(type && (!strcmp(type,"System.Boolean") || !strcmp(type,"System.Int32") || !strcmp(type,"System.Single"))) {
            unsigned char value[8]={0};if(isstatic)field_static_get_value(field,value);else field_get_value(instance,field,value);
            if(!strcmp(type,"System.Boolean"))fields[@(name)]=@(*(bool *)value);
            else if(!strcmp(type,"System.Int32"))fields[@(name)]=@(*(int *)value);
            else fields[@(name)]=@(*(float *)value);
        } else fields[@(name)]=@(type ?: "?");
        if(type && runtime_free)runtime_free(type);
    }
    result[@"fields"]=fields;return result;
}

static void dump_input(void) {
    void *device=find_class("Beyond","DeviceInfo"),*manager=input_manager();
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    const char *getters[]={"get_inputType","get_usingKeyboard","get_usingTouch","get_usingController"};
    for(int i=0;i<4;i++){void *value=input_static(device,getters[i],0,NULL);if(value)result[@(getters[i])]=render_value(value,1);}
    result[@"device"]=input_type_description(device,NULL);
    result[@"manager"]=input_type_description(find_class("Beyond.Input","InputManager"),manager);
    result[@"manager_available"]=@(manager!=NULL);
    result[@"playtools"]=input_playtools_state();
    result[@"nativeCaptureEnabled"]=@(input_capture_timer!=NULL);
    result[@"nativeCaptureTicks"]=@(input_capture_ticks);
    result[@"nativeAppState"]=@{@"active":@(NSApp.isActive),
        @"keyWindow":@(NSApp.keyWindow.windowNumber),@"mainWindow":@(NSApp.mainWindow.windowNumber),
        @"modifierFlags":@(NSEvent.modifierFlags)};
    result[@"watchFitEnabled"]=@(input_watch_fit_enabled);
    result[@"watchFitApplied"]=@(input_watch_transform_handle!=0);
    result[@"pcUILayoutEnabled"]=@(input_ui_layout_enabled);
    result[@"pcUIReferenceScale"]=@(input_ui_reference_scale);
    result[@"pcUILayoutCanvases"]=@(input_ui_layout_canvases);
    result[@"squadTipCount"]=input_squad_tip_ids ? @(input_squad_tip_ids.count) : [NSNull null];
    result[@"menuMouseEnabled"]=@(input_menu_mouse_handle!=0);
    result[@"legacyMouseButtonsReady"]=@(input_legacy_button_slots[0]!=NULL);
    result[@"menuMouseEvents"]=@(input_menu_events);
    result[@"legacyPointerReady"]=@(input_legacy_pointer_slot!=NULL);
    result[@"legacyPointerActive"]=@(atomic_load(&input_legacy_pointer_active));
    result[@"menuMousePosition"]=@[@(input_menu_position[0]),@(input_menu_position[1])];
    result[@"menuMouseButtons"]=@(input_menu_buttons);
    result[@"nativeCursorCaptured"]=@(input_cursor_captured);
    result[@"nativeMouseEvents"]=@(input_native_mouse_events);
    result[@"mouseBridgeEnabled"]=@(input_mouse_bridge_enabled);
    result[@"rewiredMouseWrites"]=@(input_mouse_events_queued);
    result[@"mouseBridgeTarget"]=input_mouse_bridge_to_camera ? @"camera" : @"rewired";
    result[@"cameraInputCalls"]=@(input_camera_calls);
    result[@"cameraInputScale"]=@(input_camera_scale);
    result[@"cameraSettings"]=input_camera_settings();
    result[@"nativeMoveEnabled"]=@(input_move_enabled);
    result[@"nativeMoveActive"]=@(input_move_active);
    result[@"nativeMoveUpdates"]=@(input_move_updates);
    result[@"nativeKeyTransitions"]=@(input_key_transitions);
    result[@"nativeKeyboardDevice"]=@(input_keyboard_handle ? input_keyboard_device_id : 0);
    result[@"nativeKeyboardEvents"]=@(input_keyboard_events);
    void *currentKeyboard=input_static(find_class("UnityEngine.InputSystem","Keyboard"),"get_current",0,NULL);
    result[@"currentKeyboardDevice"]=currentKeyboard ? @(object_int(currentKeyboard,"get_deviceId")) : @0;
    result[@"disabledNativeKeyboards"]=@(input_keyboard_disabled_devices.count);
    result[@"nativeHudKeysReady"]=@(input_hud_keys_ready);
    result[@"nativeHudKeyEvents"]=@(input_hud_key_events);
    result[@"nativeHudKeyActions"]=@(input_hud_key_actions);
    result[@"lastNativeHudKeyAction"]=input_hud_last_action ?: @"";
    result[@"lastKeyDispatchMilliseconds"]=@(input_key_dispatch_ms);
    result[@"nativeMoveAxis"]=@[@(input_move_x),@(input_move_y)];
    result[@"scrollBridgeReady"]=@(input_scroll_panel_handle!=0);
    result[@"nativeScrollEvents"]=@(input_scroll_events);
    result[@"cameraZoomCalls"]=@(input_zoom_calls);
    result[@"lastCameraScrollDelta"]=@(input_last_scroll_delta);
    result[@"nativeButtonsEnabled"]=@(input_button_bridge_enabled);
    result[@"nativeButtonEvents"]=@(input_button_events);
    result[@"nativeButtonActions"]=@(input_button_actions);
    result[@"nativeButtonsHeld"]=@[@(input_button_held[0]),@(input_button_held[1])];
    result[@"nativeAttackRoute"]=@(input_attack_route);
    result[@"inThrowMode"]=@(input_in_throw_mode());
    result[@"lastNativeButtonAction"]=input_last_button_action ?: @"";
    result[@"lastNativeButtonState"]=input_last_button_state ?: @{};
    void *gameCursor=input_static(find_class("Beyond.Input","InputManager"),"get_cursorVisible",0,NULL);
    if(gameCursor)result[@"gameCursorVisible"]=render_value(gameCursor,0);
    void *cursor=find_class("UnityEngine","Cursor");
    void *locked=input_static(cursor,"get_lockState",0,NULL),*visible=input_static(cursor,"get_visible",0,NULL);
    if(locked)result[@"unityCursorLock"]=render_value(locked,1);
    if(visible)result[@"unityCursorVisible"]=render_value(visible,1);
    if(manager) {
        const char *names[]={"get_usingController","get_autoUpdateInputBindingMask","get_isInputSuppressedByDeviceSwitch","get_inChangingInputDevice"};
        for(int i=0;i<4;i++){bool ok=false;void *v=invoke(manager,names[i],0,NULL,&ok);if(ok)result[@(names[i])]=render_value(v,1);}
        void *dict=field_reference(manager,"m_defaultSetting");bool ok=false;
        void *count=invoke(dict,"get_Count",0,NULL,&ok);if(ok && count)result[@"default_actions"]=render_value(count,1);
        void *real=field_reference(manager,"m_realCursorManager"),*description=invoke(real,"DumpState",0,NULL,&ok);
        if(ok && description)result[@"realCursorRequests"]=managed_string(description);
    }
    [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:runtime_path(@"bettermacfield-input.json") atomically:YES];
    fprintf(output,"input_dump=complete\n");
}

static bool input_device_switch_pending(void) {
    bool ok=false;void *value=invoke(input_manager(),"get_inChangingInputDevice",0,NULL,&ok);
    return ok && value && *(bool *)object_unbox(value);
}

static void select_input_mode(NSString *mode) {
    void *device=find_class("Beyond","DeviceInfo"),*manager=input_manager();
    if(!manager){fprintf(output,"input_result=manager_unavailable\n");return;}
    // Every device transition must let the game unload/rebuild its UI together.
    const void *method=class_get_method_from_name(object_get_class(manager),"_TryChangeInputDeviceType",1);
    void *(*class_from_type_fn)(const void *)=dlsym(RTLD_DEFAULT,"il2cpp_class_from_type");
    if(!method || !class_from_type_fn){fprintf(output,"input_result=method_unavailable\n");return;}
    void *current=input_static(device,"get_inputType",0,NULL);
    if(!current){fprintf(output,"input_result=current_mode_unavailable\n");return;}
    if(!input_original_mode_saved){input_original_mode=*(int *)object_unbox(current);input_original_mode_saved=true;}
    int mode_value=input_original_mode;
    if(![mode isEqual:@"restore"]) {
        const char *name=([mode isEqual:@"keyboard"] || [mode isEqual:@"keyboard-ui"]) ? "Keyboard" : ([mode isEqual:@"touch"] ? "Touch" : NULL);
        void *enum_class=class_from_type_fn(method_get_param(method,0));
        void *field=name && enum_class ? class_get_field_from_name(enum_class,name) : NULL;
        if(!field){fprintf(output,"input_result=enum_unavailable\n");return;}
        field_static_get_value(field,&mode_value);
    }
    if(input_device_switch_pending()){
        fprintf(output,"input_result=mode_pending requested=%d\n",mode_value);dump_input();return;
    }
    if(*(int *)object_unbox(current)==mode_value){
        fprintf(output,"input_result=mode_verified requested=%d actual=%d\n",mode_value,mode_value);dump_input();return;
    }
    void *args[]={&mode_value},*exception=NULL;
    runtime_invoke(method,manager,args,&exception);
    void *after=input_static(device,"get_inputType",0,NULL);
    fprintf(output,"input_result=%s requested=%d actual=%d\n",exception ? "exception" : (after && *(int *)object_unbox(after)==mode_value ? "mode_verified" : "mode_mismatch"),mode_value,after ? *(int *)object_unbox(after) : -1);
    dump_input();
}

static void input_tune(NSDictionary *command) {
    if(command[@"keyboardDevice"]){
        input_keyboard_experiment_enabled=[command[@"keyboardDevice"] boolValue];
        bool ready=input_keyboard_start(input_keyboard_experiment_enabled);
        fprintf(output,"input_keyboard_experiment=%d ready=%d\n",input_keyboard_experiment_enabled,ready);
    }
    void *manager=input_manager();bool ok=false;
    if(command[@"controller"] && manager){
        bool enabled=[command[@"controller"] boolValue];
        bool changed=set_field(manager,"enableController",&enabled);
        fprintf(output,"input_controller_enabled=%d applied=%d\n",enabled,changed);
    }
    if(command[@"bindings"] && manager){
        bool enabled=[command[@"bindings"] boolValue];void *a[]={&enabled};
        input_static(object_get_class(manager),"set_autoUpdateInputBindingMask",1,a);
        void *device=find_class("Beyond","DeviceInfo"),*mode=input_static(device,"get_inputType",0,NULL);
        if(mode){void *m[]={object_unbox(mode)};invoke(manager,"_UpdateInputBindingMask",1,m,&ok);}
        fprintf(output,"input_binding_mask_refresh=%d\n",ok);
    }
    if(command[@"cursor"]){
        int locked=[command[@"cursor"] boolValue] ? 1 : 0;bool visible=!locked;
        void *cursor=find_class("UnityEngine","Cursor"),*a[]={&locked},*b[]={&visible};
        input_static(cursor,"set_lockState",1,a);input_static(cursor,"set_visible",1,b);
        void *state=input_static(cursor,"get_lockState",0,NULL);
        fprintf(output,"input_cursor_requested=%d actual=%d\n",locked,state ? *(int *)object_unbox(state) : -1);
    }
    if([command[@"uiRefresh"] boolValue])input_ui(true);
    if(command[@"menuMouse"]){
        bool enabled=[command[@"menuMouse"] boolValue];
        bool ok=input_menu_start(enabled && input_capture_timer!=NULL);
        fprintf(output,"input_menu_mouse_enabled=%d ready=%d\n",input_menu_mouse_handle!=0,ok);
    }
    if(command[@"watchFit"]){
        input_watch_fit_enabled=[command[@"watchFit"] boolValue];
        if(input_watch_fit_enabled){input_watch_fit_update();input_squad_tips_update();}
        else{input_watch_restore();input_squad_tips_reset();}
        fprintf(output,"input_watch_fit_enabled=%d\n",input_watch_fit_enabled);
    }
    if(command[@"pcLayout"] || command[@"uiReferenceScale"]){
        if(command[@"pcLayout"])input_ui_layout_enabled=[command[@"pcLayout"] boolValue];
        float scale=command[@"uiReferenceScale"] ? [command[@"uiReferenceScale"] floatValue] : input_ui_reference_scale;
        if(scale>=1.f && scale<=2.f)input_ui_reference_scale=scale;
        if(input_ui_layout_enabled){input_ui_layout_update();input_watch_fit_update();}
        else{input_ui_layout_restore();input_watch_fit_update();}
        fprintf(output,"input_ui_layout_enabled=%d reference_scale=%g\n",input_ui_layout_enabled,input_ui_reference_scale);
    }
    if([command[@"mouseDevice"] boolValue]){
        void *mouseClass=find_class("UnityEngine.InputSystem","Mouse");
        void *mouse=input_static(mouseClass,"get_current",0,NULL);
        if(!mouse){
            void *args[]={string_new("Mouse"),NULL,NULL};
            mouse=input_static(find_class("UnityEngine.InputSystem","InputSystem"),"AddDevice",3,args);
        }
        void *current=input_static(mouseClass,"get_current",0,NULL);
        fprintf(output,"input_ui_mouse_device=%d current=%d\n",mouse!=NULL,current!=NULL);
    }
    if(command[@"watchScale"]){
        input_watch_fit_enabled=false;input_watch_restore();
        float scale=[command[@"watchScale"] floatValue];
        if(scale>=0.1f && scale<=2.f){
            void *args[]={string_new("WatchPanel")};
            void *panel=input_static(find_class("UnityEngine","GameObject"),"Find",1,args);
            void *transform=invoke(panel,"get_transform",0,NULL,&ok);
            float values[3]={scale,scale,scale};void *scaleArgs[]={values};
            invoke(transform,"set_localScale",1,scaleArgs,&ok);
            fprintf(output,"input_watch_scale=%g applied=%d\n",scale,ok);
        }
    }
    fprintf(output,"input_tune=complete\n");
}

static NSArray<NSString *> *input_rewired_axis_names(void) {
    bool ok=false;NSMutableArray *names=[NSMutableArray array];
    void *mapping=input_static(find_class("Rewired","ReInput"),"get_mapping",0,NULL);
    void *actions=invoke(mapping,"get_Actions",0,NULL,&ok);
    void *iterator=invoke(actions,"GetEnumerator",0,NULL,&ok);
    for(int i=0;iterator && i<512;i++){
        void *more=invoke(iterator,"MoveNext",0,NULL,&ok);
        if(!ok || !more || !*(bool *)object_unbox(more))break;
        void *action=invoke(iterator,"get_Current",0,NULL,&ok);
        void *type=invoke(action,"get_type",0,NULL,&ok);
        if(!ok || !type || *(int *)object_unbox(type)!=0)continue;
        void *name=invoke(action,"get_name",0,NULL,&ok);
        if(ok && name)[names addObject:managed_string(name)];
    }
    return names;
}

static NSArray<NSNumber *> *input_rewired_axis_handles;
static void input_bridge(bool enabled,bool camera_target) {
    if(input_mouse_bridge_to_camera)input_neutral_camera();
    input_mouse_bridge_enabled=false;
    for(NSNumber *handle in input_rewired_axis_handles)gchandle_free(handle.unsignedIntValue);
    input_rewired_axis_handles=nil;
    input_mouse_bridge_to_camera=false;
    if(input_scroll_panel_handle){gchandle_free(input_scroll_panel_handle);input_scroll_panel_handle=0;}
    if(!enabled){fprintf(output,"input_bridge=disabled\n");return;}
    bool ok=false;
    if(camera_target){
        void *camera=input_game_camera();
        void *controller=invoke(camera,"get_curController",0,NULL,&ok);
        if(!ok || !controller || !typed_method(object_get_class(camera),"OnInput",2,"System.Single")){
            fprintf(output,"input_bridge=game_camera_unavailable\n");return;
        }
        void *panel=input_camera_touch_panel();
        if(!panel){fprintf(output,"input_bridge=game_camera_scene_panel_unavailable\n");return;}
        input_scroll_panel_handle=gchandle_new(panel,false);
        input_mouse_bridge_to_camera=true;input_mouse_bridge_enabled=true;
        fprintf(output,"input_bridge=game_camera_enabled scale=%g controller=%s scroll_ready=%d\n",input_camera_scale,class_get_name(object_get_class(controller)),panel!=NULL);
        return;
    }
    void *controllers=input_static(find_class("Rewired","ReInput"),"get_controllers",0,NULL);
    void *mouse=invoke(controllers,"get_Mouse",0,NULL,&ok);
    void *source=field_reference(mouse,"vjQjIfQHqXAJIEOzLKIquQLRUNjr");
    if(!source || strcmp(class_get_name(object_get_class(source)),"UnityUnifiedMouseSource")){
        fprintf(output,"input_bridge=unexpected_mouse_source\n");return;
    }
    void *sets=field_reference(source,"WUdSzUHMtOvMttpWWOCyBCGsVBWf");
    void *array=field_reference(sets,"WUdSzUHMtOvMttpWWOCyBCGsVBWf");
    NSMutableArray *handles=[NSMutableArray array];
    if(array && array_length(array)<=8)for(uintptr_t i=0;i<array_length(array);i++){
        void *entry=array_item(array,i),*state=field_reference(entry,"kirDvaHQIIttuypLhXNlLYICPKsC");
        void *axes=field_reference(state,"TXagAXQtkwjdbbcimAzxIKbKnPuC");
        if(axes && !strcmp(class_get_name(object_get_class(axes)),"Single[]") && array_length(axes)==4)
            [handles addObject:@(gchandle_new(axes,false))];
    }
    if(!handles.count){fprintf(output,"input_bridge=no_rewired_mouse_buffers\n");return;}
    input_rewired_axis_handles=handles;input_mouse_bridge_enabled=true;
    fprintf(output,"input_bridge=rewired_enabled buffers=%lu scale=0.1\n",(unsigned long)handles.count);
}

static void input_queue_mouse(double x,double y) {
    if(x==0 && y==0)return;
    if(input_mouse_bridge_to_camera){
        void *screen=find_class("UnityEngine","Screen");
        void *width=input_static(screen,"get_width",0,NULL),*height=input_static(screen,"get_height",0,NULL);
        if(!width || !height)return;
        int w=*(int *)object_unbox(width),h=*(int *)object_unbox(height);
        if(w<=0 || h<=0)return;
        // PC LevelCameraCtrl._UpdateCamera and UIUtils.getNormalizedScreenX/Y.
        // CameraControlManual applies the game's sensitivity curves afterwards.
        float horizontal=(float)(x*0.9*input_camera_scale/w),vertical=(float)(y*0.63*input_camera_scale/h);
        void *args[]={&horizontal,&vertical};bool ok=false;
        invoke(input_game_camera(),"OnInput",2,args,&ok);
        if(ok)input_camera_calls++;
        return;
    }
    for(NSNumber *handle in input_rewired_axis_handles){
        void *array=gchandle_get_target(handle.unsignedIntValue);
        float *values=(float *)((char *)array+array_object_header_size());
        values[0]+=(float)x*0.1f;values[1]+=(float)y*0.1f;
    }
    input_mouse_events_queued++;
}

static NSDictionary *input_camera_settings(void) {
    NSMutableDictionary *result=[NSMutableDictionary dictionary];bool ok=false;
    void *setting=find_class("Beyond","GameSetting"),*screen=find_class("UnityEngine","Screen");
    const char *getters[]={"get_controllerCameraSpeedX","get_controllerCachedCameraSpeedX","get_controllerCameraSpeedY","get_controllerCachedCameraSpeedY","get_controllerCachedCameraReverseX","get_controllerCachedCameraReverseY"};
    for(int i=0;i<6;i++){void *v=input_static(setting,getters[i],0,NULL);if(v)result[@(getters[i])]=render_value(v,1);}
    const char *dimensions[]={"get_width","get_height"};
    for(int i=0;i<2;i++){void *v=input_static(screen,dimensions[i],0,NULL);if(v)result[@(dimensions[i])]=render_value(v,1);}
    void *controller=invoke(input_game_camera(),"get_curController",0,NULL,&ok);
    void *manual=field_reference(controller,"<manualModule>k__BackingField");
    void *config=invoke(manual,"get_gameplayCameraSetting",0,NULL,&ok);
    const char *curves[]={"get_cameraSettingControlSpeedXCurve","get_cameraSettingControlSpeedYCurve"};
    for(int i=0;config && i<2;i++){
        void *curve=invoke(config,curves[i],0,NULL,&ok);
        void *value=input_static(setting,getters[i*2+1],0,NULL);
        if(!curve || !value)continue;
        float speed=*(float *)object_unbox(value);void *args[]={&speed};
        void *gain=invoke_typed(curve,"Evaluate",1,"System.Single",args,&ok);
        if(ok && gain)result[i==0 ? @"curveGainX" : @"curveGainY"]=render_value(gain,1);
        NSMutableArray *keys=[NSMutableArray array];
        void *length=invoke(curve,"get_length",0,NULL,&ok);
        int count=ok && length ? *(int *)object_unbox(length) : 0;
        for(int k=0;k<count && k<8;k++){void *a[]={&k};void *key=invoke_typed(curve,"get_Item",1,"System.Int32",a,&ok);if(ok)[keys addObject:render_value(key,1)];}
        result[i==0 ? @"curveKeysX" : @"curveKeysY"]=keys;
    }
    return result;
}

static void *input_rewired_player(void) {
    bool ok=false;int index=0;void *args[]={&index};
    void *players=input_static(find_class("Rewired","ReInput"),"get_players",0,NULL);
    return players ? invoke_typed(players,"GetPlayer",1,"System.Int32",args,&ok) : NULL;
}
