/* Inspect the game's original settings model and its platform visibility rules. */
static NSMutableArray *game_settings_saved;
static NSMutableArray *game_settings_components;
static bool game_settings_resolution_bridge;
static uint32_t game_settings_dropdown,game_settings_original_update,game_settings_owned_update;
static int game_settings_last_width,game_settings_last_height;
static int game_settings_last_scale_tier;
static NSString *const game_settings_resolution_key=@"BetterMacFieldOutputResolution";
static NSString *const game_settings_scale_key=@"BetterMacFieldRenderingScaleTier";

static void game_settings_note_output(int width,int height) {
    if(!game_settings_resolution_bridge)return;
    bool ok=false;int size[2]={width,height};
    bool notify=false;
    void *setting=input_static(find_class("Beyond","GameSetting"),"get_videoResolutionSetting",0,NULL),*args[]={size,&notify};
    invoke(setting,"SetValue",2,args,&ok);
    if(!ok)return;
    game_settings_last_width=width;game_settings_last_height=height;
    [NSUserDefaults.standardUserDefaults setObject:@[@(width),@(height)] forKey:game_settings_resolution_key];
}

static bool game_settings_set_scale(double scale) {
    if(!game_settings_components)return false;
    float tier=round((1.0-scale)*10.0)+1.0;
    if(tier<1 || tier>5 || fabs(scale-(1.0-(tier-1)*0.1))>0.001)return false;
    override_parameter(get_hub(),@"RenderingScale|RenderTargetSettings",nil,true);
    void *args[]={string_new("video_rendering_scale_pc"),&tier};
    input_static(find_class("Beyond.Gameplay","GameSettingHelper"),"SetQualitySubSettingTierBySettingId",2,args);
    void *findArgs[]={string_new("GameSettingPanel/Main/ContentNode/SettingListNode/ScrollView/ViewContent/SettingContent/GameSettingItem_video_rendering_scale_pc/Content/ContentNode/ControlNode")};
    void *root=input_static(find_class("UnityEngine","GameObject"),"Find",1,findArgs);
    if(root){
        bool inactive=false,ok=false;
        void *componentArgs[]={type_get_object(class_get_type(find_class("Beyond.UI","UIDropdown"))),&inactive};
        void *array=invoke_typed(root,"GetComponentsInChildren",2,"System.Type",componentArgs,&ok);
        if(ok && array && array_length(array)==1){
            void *dropdown=array_item(array,0);int count=5,selected=(int)tier-1;bool notify=false;
            void *refresh[]={&count,&selected,&notify};invoke(dropdown,"Refresh",3,refresh,&ok);
        }
    }
    return true;
}

static void game_settings_release_dropdown(void) {
    if(game_settings_dropdown){
        void *dropdown=gchandle_get_target(game_settings_dropdown);
        if(input_unity_alive(dropdown) && field_reference(dropdown,"onUpdateCell")==gchandle_get_target(game_settings_owned_update))
            field_set_value_object(dropdown,class_get_field_from_name(object_get_class(dropdown),"onUpdateCell"),gchandle_get_target(game_settings_original_update));
        gchandle_free(game_settings_dropdown);gchandle_free(game_settings_original_update);gchandle_free(game_settings_owned_update);
        game_settings_dropdown=game_settings_original_update=game_settings_owned_update=0;
    }
}

static void game_settings_fill_resolutions(void *settings) {
    bool ok=false;
    for(NSNumber *number in @[@2160,@1440,@1080,@720]){
        int h=number.intValue,w=h*16/9;void *args[]={&w,&h};input_static(settings,"_AddScreenResolution",2,args);
    }
    void *list=input_static(settings,"get_availableScreenResolutionList",0,NULL);
    void *comparison=field_get_value_object(class_get_field_from_name(settings,"s_descendingOrderComparison"),NULL);
    void *args[]={comparison};invoke_typed(list,"Sort",1,"System.Comparison<Beyond.GameSetting.ScreenResolution>",args,&ok);
}

/* The original selection callback keeps the game's setting/save behavior. Only
   its mobile resolution enumeration and the missing output application are adapted. */
static void game_settings_tick(void) {
    if(!game_settings_resolution_bridge)return;
    display_finish_pending_resolution();
    bool ok=false;void *settings=find_class("Beyond","GameSetting");
    void *scaleArgs[]={string_new("video_rendering_scale_pc")};
    void *scale=input_static(find_class("Beyond.Gameplay","GameSettingHelper"),"GetQualitySubSettingTierBySettingId",1,scaleArgs);
    int tier=scale ? (int)round(*(float *)object_unbox(scale)) : 0;
    if(tier>=1 && tier<=5 && tier!=game_settings_last_scale_tier){
        if(game_settings_last_scale_tier && [NSUserDefaults.standardUserDefaults objectForKey:display_preset_key]){
            // An explicit change in the original settings takes precedence.
            override_parameter(get_hub(),@"RenderingScale|RenderTargetSettings",nil,true);
            [display_overrides removeObject:@"RenderingScale|RenderTargetSettings"];
            [NSUserDefaults.standardUserDefaults removeObjectForKey:display_preset_key];
        }
        game_settings_last_scale_tier=tier;
        [NSUserDefaults.standardUserDefaults setInteger:tier forKey:game_settings_scale_key];
    }
    void *resolution=input_static(settings,"get_videoResolution",0,NULL);
    if(!resolution)return;
    int *size=object_unbox(resolution),width=size[0],height=size[1];
    if(width!=game_settings_last_width || height!=game_settings_last_height){
        game_settings_last_width=width;game_settings_last_height=height;
        display_control(@{@"width":@(width),@"height":@(height)});
    }
    if(game_settings_dropdown && !input_unity_alive(gchandle_get_target(game_settings_dropdown)))game_settings_release_dropdown();
    void *dropdown=game_settings_dropdown ? gchandle_get_target(game_settings_dropdown) : NULL;
    if(!dropdown){
        void *args[]={string_new("GameSettingPanel/Main/ContentNode/SettingListNode/ScrollView/ViewContent/SettingContent/GameSettingItem_video_resolution/Content/ContentNode/ControlNode")};
        void *root=input_static(find_class("UnityEngine","GameObject"),"Find",1,args);
        if(!root)return;
        bool includeInactive=false;void *findArgs[]={type_get_object(class_get_type(find_class("Beyond.UI","UIDropdown"))),&includeInactive};
        void *array=invoke_typed(root,"GetComponentsInChildren",2,"System.Type",findArgs,&ok);
        if(!ok || !array || array_length(array)!=1)return;
        dropdown=array_item(array,0);
    }
    void *list=input_static(settings,"get_availableScreenResolutionList",0,NULL);
    if(game_settings_dropdown && object_int(list,"get_Count")>=4 && object_int(dropdown,"get_optionCount")>=4){
        int selected=object_int(dropdown,"get_selectedIndex");
        if(selected>=0 && selected<object_int(list,"get_Count")){
            void *at[]={&selected},*entry=invoke(list,"get_Item",1,at,&ok);
            int *pair=entry ? object_unbox(entry) : NULL;
            if(pair && pair[0]==width && pair[1]==height)return;
        }
    }
    game_settings_release_dropdown();
    void *original=field_reference(dropdown,"onUpdateCell"),*replacement=object_new(object_get_class(original));
    invoke(replacement,".ctor",0,NULL,&ok);if(!ok)return;
    game_settings_dropdown=gchandle_new(dropdown,false);
    game_settings_original_update=gchandle_new(original,false);game_settings_owned_update=gchandle_new(replacement,false);
    field_set_value_object(dropdown,class_get_field_from_name(object_get_class(dropdown),"onUpdateCell"),replacement);
    game_settings_fill_resolutions(settings);
    list=input_static(settings,"get_availableScreenResolutionList",0,NULL);
    int count=object_int(list,"get_Count"),selected=0;
    NSMutableArray *labels=[NSMutableArray array];
    for(int i=0;i<count;i++){
        void *at[]={&i},*entry=invoke(list,"get_Item",1,at,&ok);int *pair=object_unbox(entry);
        [labels addObject:[NSString stringWithFormat:@"%d×%d",pair[0],pair[1]]];
        if(pair[0]==width && pair[1]==height)selected=i;
    }
    bool notify=false;void *refresh[]={&count,&selected,&notify};invoke(dropdown,"Refresh",3,refresh,&ok);
    void *options=field_reference(dropdown,"m_options");
    for(int i=0;i<count;i++){
        void *at[]={&i},*option=invoke(options,"get_Item",1,at,&ok);
        void *text[]={string_new([labels[i] UTF8String])},*state[]={string_new("Normal")};
        invoke(option,"SetText",1,text,&ok);invoke(option,"SetState",1,state,&ok);
    }
    void *captionArgs[]={string_new([labels[selected] UTF8String])};
    invoke(field_reference(dropdown,"captionText"),"set_text",1,captionArgs,&ok);
    fprintf(output,"game_settings_resolution_dropdown=count:%d selected:%d\n",count,selected);
}
static void game_settings_attach_components(void *manager,bool enable) {
    bool ok=false;void *map=field_reference(manager,"m_currentQualityTierComponent");
    void *types=field_reference(manager,"m_qualityTierTypes"),*tiers=field_reference(manager,"m_qualityTiers");
    if(enable && !game_settings_components){
        game_settings_components=[NSMutableArray array];
        for(NSString *name in @[@"RenderingScaleQualityPC",@"UpscalerQuality"]){
            void *klass=find_class("Beyond.Scripts.Quality.Components",name.UTF8String),*type=type_get_object(class_get_type(klass));
            void *keyArgs[]={type},*exists=invoke(map,"ContainsKey",1,keyArgs,&ok);
            if(!ok || (exists && *(bool *)object_unbox(exists)))continue;
            void *component=object_new(klass);invoke(component,".ctor",0,NULL,&ok);if(!ok)continue;
            int index=[name isEqual:@"UpscalerQuality"] ? 1 : 0;
            set_field(component,"enumValue",&index);
            void *match=object_new(find_class("Beyond.Scripts.Quality.Components","QualityTierComponentMatch"));
            field_set_value_object(match,class_get_field_from_name(object_get_class(match),"component"),component);
            int tier=object_int(manager,"get_currentTier"),source=2;
            set_field(match,"matchQualityTier",&tier);set_field(match,"matchSource",&source);
            void *args[]={type,object_unbox(match)};invoke(map,"set_Item",2,args,&ok);if(!ok)continue;
            invoke(types,"Add",1,keyArgs,&ok);
            int count=object_int(tiers,"get_Count");
            for(int i=0;i<count;i++){
                void *at[]={&i},*entry=invoke(tiers,"get_Item",1,at,&ok),*add[]={component};
                invoke(field_reference(entry,"qualityTierComponents"),"Add",1,add,&ok);
            }
            [game_settings_components addObject:@{@"name":name,@"handle":@(gchandle_new(component,false))}];
        }
    }else if(!enable && game_settings_components){
        for(NSDictionary *record in game_settings_components){
            uint32_t handle=[record[@"handle"] unsignedIntValue];void *component=gchandle_get_target(handle);
            void *type=type_get_object(class_get_type(object_get_class(component))),*args[]={type};
            invoke(map,"Remove",1,args,&ok);invoke(types,"Remove",1,args,&ok);
            int count=object_int(tiers,"get_Count");
            for(int i=0;i<count;i++){
                void *at[]={&i},*entry=invoke(tiers,"get_Item",1,at,&ok),*remove[]={component};
                invoke(field_reference(entry,"qualityTierComponents"),"Remove",1,remove,&ok);
            }
            gchandle_free(handle);
        }
        game_settings_components=nil;
    }
}
static void game_settings(NSDictionary *command) {
    void *settings=find_class("Beyond","GameSetting");
    void *helper=find_class("Beyond.Gameplay","GameSettingHelper");
    void *manager=input_static(find_class("Beyond.Scripts.Quality","QualityManager"),"get_instance",0,NULL);
    if(command[@"components"])game_settings_attach_components(manager,[command[@"components"] boolValue]);
    if(command[@"resolutionBridge"]){
        bool starting=!game_settings_resolution_bridge && [command[@"resolutionBridge"] boolValue];
        game_settings_resolution_bridge=[command[@"resolutionBridge"] boolValue];
        if(starting){
            void *screen=find_class("UnityEngine","Screen");bool ok=false;
            void *w=input_static(screen,"get_width",0,NULL),*h=input_static(screen,"get_height",0,NULL);
            int size[2]={*(int *)object_unbox(w),*(int *)object_unbox(h)};
            game_settings_last_width=size[0];game_settings_last_height=size[1];
            NSArray *saved=[NSUserDefaults.standardUserDefaults arrayForKey:game_settings_resolution_key];
            if(saved.count==2 && [saved[0] intValue]>=640 && [saved[0] intValue]<=3840 && [saved[1] intValue]>=360 && [saved[1] intValue]<=2160){
                size[0]=[saved[0] intValue];size[1]=[saved[1] intValue];
            }
            bool notify=false;
            void *setting=input_static(settings,"get_videoResolutionSetting",0,NULL),*args[]={size,&notify};
            invoke(setting,"SetValue",2,args,&ok);
            int tier=(int)[NSUserDefaults.standardUserDefaults integerForKey:game_settings_scale_key];
            if(tier<1 || tier>5)tier=1;
            game_settings_set_scale(1.0-(tier-1)*0.1);
            game_settings_last_scale_tier=tier;
            NSString *preset=[NSUserDefaults.standardUserDefaults stringForKey:display_preset_key];
            if(display_preset_ratio(preset)>0)
                display_control(@{@"width":@(size[0]),@"height":@(size[1]),@"preset":preset,@"remember":@NO});
        }else if(!game_settings_resolution_bridge)game_settings_release_dropdown();
    }
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    void *earlyField=class_get_field_from_name(settings,"s_earlySettingRuleNodes");
    void *early=earlyField ? field_get_value_object(earlyField,NULL) : NULL;
    if(command[@"reveal"]){
        bool ok=false;
        if([command[@"reveal"] boolValue] && !game_settings_saved){
            game_settings_saved=[NSMutableArray array];
            for(NSString *identifier in @[@"video_resolution",@"video_rendering_scale_pc",@"video_quality_upscaler",@"video_rendering_scale_mobile"]){
                void *key=string_new(identifier.UTF8String),*keyArgs[]={key};
                void *contains=invoke(early,"ContainsKey",1,keyArgs,&ok),*saved=NULL;
                bool existed=ok && contains && *(bool *)object_unbox(contains);
                if(existed)saved=invoke(early,"get_Item",1,keyArgs,&ok);
                void *tuple[2]={NULL,NULL},*setArgs[]={key,tuple};
                if([identifier isEqual:@"video_rendering_scale_mobile"]){
                    void *(*nested)(void *,void **)=dlsym(RTLD_DEFAULT,"il2cpp_class_get_nested_types");
                    void *it=NULL,*klass;
                    while((klass=nested(settings,&it)))if(!strcmp(class_get_name(klass),"RuleNodeConst")){
                        tuple[0]=input_static(klass,"get_falseNode",0,NULL);break;
                    }
                }
                invoke(early,"set_Item",2,setArgs,&ok);
                [game_settings_saved addObject:@{@"id":identifier,@"existed":@(existed),@"value":@(saved ? gchandle_new(saved,false) : 0)}];
            }
        }else if(![command[@"reveal"] boolValue] && game_settings_saved){
            for(NSDictionary *entry in game_settings_saved){
                void *key=string_new([entry[@"id"] UTF8String]);uint32_t handle=[entry[@"value"] unsignedIntValue];
                if([entry[@"existed"] boolValue]){
                    void *args[]={key,object_unbox(gchandle_get_target(handle))};invoke(early,"set_Item",2,args,&ok);
                }else{void *args[]={key};invoke(early,"Remove",1,args,&ok);}
                if(handle)gchandle_free(handle);
            }
            game_settings_saved=nil;
        }
    }
    void *context=input_static(settings,"get_settingRuleContext",0,NULL);bool success=false;
    for(NSString *key in command[@"rules"]){
        void *args[]={string_new(key.UTF8String),string_new([command[@"rules"][key] UTF8String])};
        invoke(context,"SetValue",2,args,&success);
    }
    if(command[@"settingId"] && command[@"tier"]){
        float tier=[command[@"tier"] floatValue];void *args[]={string_new([command[@"settingId"] UTF8String]),&tier};
        input_static(helper,"SetQualitySubSettingTierBySettingId",2,args);
    }
    if([command[@"addResolutions"] boolValue]){
        game_settings_fill_resolutions(settings);
    }
    NSMutableArray *rows=[NSMutableArray array];
    for(NSString *constant in @[@"ID_VIDEO_RESOLUTION",@"ID_VIDEO_RENDERING_SCALE_PC",@"ID_VIDEO_RENDERING_SCALE_MOBILE",@"ID_VIDEO_QUALITY_UPSCALER"]){
        void *field=class_get_field_from_name(settings,constant.UTF8String);
        void *key=field ? field_get_value_object(field,NULL) : NULL;
        if(!key)continue;
        NSString *identifier=managed_string(key);
        NSMutableDictionary *row=[NSMutableDictionary dictionaryWithObjectsAndKeys:identifier,@"id",nil];
        for(NSString *name in @[@"IsSettingItemValid",@"IsSettingItemVisible"]){
            void *args[]={key};void *value=input_static(settings,name.UTF8String,1,args);
            if(value)row[name]=render_value(value,0);
        }
        void *itemClass=find_class("Beyond.Cfg","SettingItemData"),*qualityClass=find_class("Beyond.Cfg","QualitySubSettingData");
        int (*valueSize)(void *,uint32_t *)=dlsym(RTLD_DEFAULT,"il2cpp_class_value_size");
        void *(*box)(void *,void *)=dlsym(RTLD_DEFAULT,"il2cpp_value_box");
        uint32_t alignment=0;
        NSMutableData *itemStorage=[NSMutableData dataWithLength:valueSize(itemClass,&alignment)];
        NSMutableData *qualityStorage=[NSMutableData dataWithLength:valueSize(qualityClass,&alignment)];
        bool sub=false;void *args[]={key,&sub,itemStorage.mutableBytes,qualityStorage.mutableBytes};
        input_static(settings,"TryGetSettingItemData",4,args);
        void *data=box(sub ? qualityClass : itemClass,sub ? qualityStorage.mutableBytes : itemStorage.mutableBytes);
        for(NSString *name in sub ? @[@"settingId",@"settingText",@"validPlatform"] : @[@"settingId",@"settingText",@"validPlatform",@"validRuleJson",@"visibleRuleJson"]){
            void *value=invoke(data,[[@"get_" stringByAppendingString:name] UTF8String],0,NULL,&success);
            if(success && value)row[name]=render_value(value,0);
        }
        void *config=NULL,*configArgs[]={key,&config};
        input_static(helper,"GetQualitySubSettingConfigBySettingId",2,configArgs);
        if(config){
            float minTier=0;void *countArgs[]={key,&minTier};
            void *count=input_static(helper,"GetQualitySubSettingCountBySettingId",2,countArgs);
            if(count)row[@"tierCount"]=render_value(count,0);
            if([identifier isEqual:@"video_quality_upscaler"]){
                NSMutableArray *valid=[NSMutableArray array];
                for(int i=0;i<3;i++){
                    void *check[]={key,&i},*value=input_static(helper,"IsQualitySubSettingTierValid",2,check);
                    [valid addObject:value ? render_value(value,0) : NSNull.null];
                }
                row[@"validUpscalerModes"]=valid;
            }
            void *checkArgs[]={key};
            for(NSString *name in @[@"IsQualitySubSettingValid",@"GetQualitySubSettingTierBySettingId"]){
                void *value=input_static(helper,name.UTF8String,1,checkArgs);
                if(value)row[name]=render_value(value,0);
            }
            row[@"config"]=render_value(config,0);
            row[@"options"]=render_value(field_reference(field_reference(config,"dropdownUniversalOptionGroup"),"optionList"),0);
        }
        [rows addObject:row];
    }
    result[@"items"]=rows;
    result[@"qualityManager"]=render_value(manager,0);
    result[@"components"]=dictionary_entries(field_reference(manager,"m_currentQualityTierComponent"));
    result[@"rules"]=dictionary_entries(field_reference(context,"m_values"));
    void *resolutions=input_static(settings,"get_availableScreenResolutionList",0,NULL);
    result[@"resolutions"]=render_value(resolutions,0);
    [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:@"/private/tmp/bettermacfield-game-settings.json" atomically:YES];
    fprintf(output,"game_settings=complete items=%lu\n",(unsigned long)rows.count);
}
