/* Expose the native custom-keyboard settings and restore their original rules. */
static NSMutableArray *game_settings_keyboard_saved;
static NSString *game_settings_keyboard_rule;
static uint32_t game_settings_keyboard_original_rule;
static bool game_settings_keyboard_rule_existed;

static NSString *game_settings_keyboard_string(void *owner,const char *getter) {
    bool ok=false;void *value=invoke(owner,getter,0,NULL,&ok);
    return ok ? (value ? managed_string(value) : @"") : nil;
}

static NSArray *game_settings_keyboard_strings(void *owner,const char *getter) {
    bool ok=false;void *list=invoke(owner,getter,0,NULL,&ok);
    if(!ok)return nil;
    if(!list)return @[];
    void *size=invoke(list,"get_Count",0,NULL,&ok);
    if(!ok || !size)return nil;
    int count=*(int *)object_unbox(size);
    if(count<0 || count>48)return nil;
    NSMutableArray *result=[NSMutableArray array];
    for(int i=0;i<count;i++){
        void *args[]={&i},*value=invoke(list,"get_Item",1,args,&ok);
        if(!ok || !value)return nil;
        [result addObject:managed_string(value)];
    }
    return result;
}

static void *game_settings_keyboard_tab(void) {
    void *table=input_static(find_class("Beyond.Cfg","Tables"),"get_settingTabTable",0,NULL);
    bool ok=false;void *args[]={string_new("gameSetting_key_hint")};
    return table ? invoke(table,"get_Item",1,args,&ok) : NULL;
}

static void *game_settings_keyboard_dictionary(void *settings,const char *name) {
    void *field=class_get_field_from_name(settings,name);
    return field ? field_get_value_object(field,NULL) : NULL;
}

static bool game_settings_keyboard_restore(void *settings) {
    if(!game_settings_keyboard_saved)return true;
    void *early=game_settings_keyboard_dictionary(settings,"s_earlySettingRuleNodes");
    void *cache=game_settings_keyboard_dictionary(settings,"s_ruleCache");
    if(!early || !cache)return false;
    bool ok=false,restored=true;
    for(NSDictionary *entry in game_settings_keyboard_saved){
        void *key=string_new([entry[@"id"] UTF8String]);
        uint32_t handle=[entry[@"value"] unsignedIntValue];
        if([entry[@"existed"] boolValue]){
            void *saved=handle ? gchandle_get_target(handle) : NULL;
            if(!saved){restored=false;continue;}
            void *args[]={key,object_unbox(saved)};invoke(early,"set_Item",2,args,&ok);
        }else{void *args[]={key};invoke(early,"Remove",1,args,&ok);}
        restored &= ok;
    }
    void *key=string_new(game_settings_keyboard_rule.UTF8String);
    if(game_settings_keyboard_rule_existed){
        void *args[]={key,game_settings_keyboard_original_rule ? gchandle_get_target(game_settings_keyboard_original_rule) : NULL};
        invoke(cache,"set_Item",2,args,&ok);
    }else{void *args[]={key};invoke(cache,"Remove",1,args,&ok);}
    restored &= ok;
    if(restored){
        for(NSDictionary *entry in game_settings_keyboard_saved){
            uint32_t handle=[entry[@"value"] unsignedIntValue];if(handle)gchandle_free(handle);
        }
        if(game_settings_keyboard_original_rule)gchandle_free(game_settings_keyboard_original_rule);
        game_settings_keyboard_original_rule=0;game_settings_keyboard_rule=nil;game_settings_keyboard_saved=nil;
    }
    return restored;
}

static NSString *game_settings_keyboard_control(void *settings,bool enabled) {
    if(!enabled)return game_settings_keyboard_restore(settings) ? @"disabled" : @"restore_failed";
    if(game_settings_keyboard_saved)return @"already_enabled";
    NSBundle *bundle=NSBundle.mainBundle;
    if(![@[@"com.gryphline.endfield.ios",@"com.hypergryph.endfield"] containsObject:bundle.bundleIdentifier] ||
       ![[bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] isEqual:@"1.6.3"])
        return @"unsupported_build";
    void *tab=game_settings_keyboard_tab();
    if(!tab || ![game_settings_keyboard_strings(tab,"get_validPlatform") isEqual:@[@"WindowsPlayer"]] ||
       ![game_settings_keyboard_strings(tab,"get_validInputTypes") isEqual:@[@"Keyboard"]] ||
       ![game_settings_keyboard_strings(tab,"get_validGameStates") isEqual:@[@"Main"]] ||
       ![game_settings_keyboard_string(tab,"get_validateFunction") isEqual:@"ValidateKeyHint"])
        return @"tab_rules_changed";
    NSString *originalRule=game_settings_keyboard_string(tab,"get_visibleRuleJson");
    id rule=originalRule ? [NSJSONSerialization JSONObjectWithData:[originalRule dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil] : nil;
    NSDictionary *platform=@{@"key":@"Platform",@"in":@[@"WindowsPlayer"]};
    NSDictionary *keyboard=@{@"key":@"InputType",@"equal":@"Keyboard"};
    NSDictionary *main=@{@"key":@"GameState",@"in":@[@"Main"]};
    NSArray *nodes=[rule isKindOfClass:NSDictionary.class] ? rule[@"and"] : nil;
    if(![nodes isKindOfClass:NSArray.class] || [rule count]!=1 || nodes.count!=3 ||
       ![[NSSet setWithArray:nodes] isEqual:[NSSet setWithArray:@[platform,keyboard,main]]])
        return @"tab_expression_changed";
    bool ok=false;void *items=invoke(tab,"get_tabItems",0,NULL,&ok);
    void *count=items ? invoke(items,"get_Count",0,NULL,&ok) : NULL;
    if(!ok || !count || *(int *)object_unbox(count)!=48)return @"item_count_changed";
    void *early=game_settings_keyboard_dictionary(settings,"s_earlySettingRuleNodes");
    void *cache=game_settings_keyboard_dictionary(settings,"s_ruleCache");
    if(!early || !cache)return @"rules_unavailable";
    NSMutableArray *saved=[NSMutableArray array];NSString *failure=nil;
    for(int i=0;i<48;i++){
        void *at[]={&i},*item=invoke(items,"GetValueByIndex",1,at,&ok);
        NSString *identifier=ok && item ? game_settings_keyboard_string(item,"get_settingId") : nil;
        if(![identifier hasPrefix:@"key_"] ||
           ![game_settings_keyboard_strings(item,"get_validPlatform") isEqual:@[@"WindowsPlayer"]]){failure=@"item_platform_changed";break;}
        for(const char **getter=(const char*[]){"get_validGameStates","get_validRegions","get_validServerChannels","get_validUserPlatform",NULL};*getter;getter++)
            if(![game_settings_keyboard_strings(item,*getter) isEqual:@[]])failure=@"item_rules_changed";
        for(const char **getter=(const char*[]){"get_validRuleJson","get_visibleRuleJson","get_validateFunction",NULL};*getter;getter++)
            if(![game_settings_keyboard_string(item,*getter) isEqual:@""])failure=@"item_rules_changed";
        if(failure)break;
        void *key=string_new(identifier.UTF8String),*args[]={key};
        void *contains=invoke(early,"ContainsKey",1,args,&ok);
        if(!ok || !contains){failure=@"item_rule_read_failed";break;}
        bool existed=*(bool *)object_unbox(contains);void *value=existed ? invoke(early,"get_Item",1,args,&ok) : NULL;
        if(!ok || (existed && !value)){failure=@"item_rule_read_failed";break;}
        if(value && (!class_get_field_from_name(object_get_class(value),"Item1") ||
                     !class_get_field_from_name(object_get_class(value),"Item2"))){failure=@"item_rule_layout_changed";break;}
        [saved addObject:@{@"id":identifier,@"existed":@(existed),@"value":@(value ? gchandle_new(value,false) : 0)}];
    }
    void *key=string_new(originalRule.UTF8String),*keyArgs[]={key};
    void *contains=failure ? NULL : invoke(cache,"ContainsKey",1,keyArgs,&ok);
    if(!failure && (!ok || !contains))failure=@"tab_rule_read_failed";
    bool existed=contains && *(bool *)object_unbox(contains);
    void *original=existed ? invoke(cache,"get_Item",1,keyArgs,&ok) : NULL;
    if(!failure && !ok)failure=@"tab_rule_read_failed";
    NSData *json=[NSJSONSerialization dataWithJSONObject:@{@"and":@[keyboard,main]} options:0 error:nil];
    NSString *newRule=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    void *parseArgs[]={string_new(newRule.UTF8String)};
    void *replacement=failure ? NULL : input_static(settings,"_ParseRule",1,parseArgs);
    if(!replacement && !failure)failure=@"tab_rule_parse_failed";
    if(failure){
        for(NSDictionary *entry in saved){uint32_t handle=[entry[@"value"] unsignedIntValue];if(handle)gchandle_free(handle);}
        return failure;
    }
    uint32_t replacementHandle=gchandle_new(replacement,false);
    game_settings_keyboard_saved=saved;game_settings_keyboard_rule=originalRule;
    game_settings_keyboard_rule_existed=existed;
    game_settings_keyboard_original_rule=original ? gchandle_new(original,false) : 0;
    void *setRuleArgs[]={key,gchandle_get_target(replacementHandle)};invoke(cache,"set_Item",2,setRuleArgs,&ok);
    if(!ok)failure=@"tab_rule_write_failed";
    for(NSDictionary *entry in saved){
        if(failure)break;
        uint32_t handle=[entry[@"value"] unsignedIntValue];
        void *old=handle ? gchandle_get_target(handle) : NULL;
        // The verified items have only a platform validity rule; retain the visibility node.
        void *tuple[2]={NULL,old ? field_reference(old,"Item2") : NULL};
        void *args[]={string_new([entry[@"id"] UTF8String]),tuple};invoke(early,"set_Item",2,args,&ok);
        if(!ok)failure=@"item_rule_write_failed";
    }
    gchandle_free(replacementHandle);
    if(!failure)for(NSDictionary *entry in saved){
        void *args[]={string_new([entry[@"id"] UTF8String])};
        void *value=input_static(settings,"IsSettingItemValid",1,args);
        if(!value || !*(bool *)object_unbox(value)){failure=@"item_rules_not_applied";break;}
    }
    if(!failure){
        void *context=input_static(settings,"get_settingRuleContext",0,NULL);
        void *evaluateArgs[]={string_new(newRule.UTF8String),context};
        void *expected=context ? input_static(settings,"_EvaluateRule",2,evaluateArgs) : NULL;
        void *tabArgs[]={string_new("gameSetting_key_hint")};
        void *visible=input_static(settings,"IsSettingTabVisible",1,tabArgs);
        if(!expected || !visible || *(bool *)object_unbox(expected)!=*(bool *)object_unbox(visible))
            failure=@"tab_rule_not_applied";
    }
    if(failure)return game_settings_keyboard_restore(settings) ? failure : @"rollback_failed";
    return @"enabled";
}

static NSDictionary *game_settings_keyboard_status(void *settings,NSString *result) {
    bool ok=false;void *keyArgs[]={string_new("gameSetting_key_hint")};
    void *visible=input_static(settings,"IsSettingTabVisible",1,keyArgs);
    void *tab=game_settings_keyboard_tab(),*items=tab ? invoke(tab,"get_tabItems",0,NULL,&ok) : NULL;
    void *size=items ? invoke(items,"get_Count",0,NULL,&ok) : NULL;
    int count=ok && size ? *(int *)object_unbox(size) : 0,valid=0;
    if(count==48)for(int i=0;i<count;i++){
        void *at[]={&i},*key=invoke(items,"GetKeyByIndex",1,at,&ok),*args[]={key};
        void *value=ok && key ? input_static(settings,"IsSettingItemValid",1,args) : NULL;
        if(value && *(bool *)object_unbox(value))valid++;
    }
    return @{@"enabled":@(game_settings_keyboard_saved!=nil),@"result":result ?: @"status",
             @"tabVisible":@(visible && *(bool *)object_unbox(visible)),@"itemCount":@(count),@"validItemCount":@(valid)};
}
