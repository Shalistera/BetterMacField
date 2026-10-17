static void *(*domain_get)(void);
static const void **(*domain_get_assemblies)(void *, size_t *);
static void *(*assembly_get_image)(const void *);
static const char *(*image_get_name)(void *);
static void *(*class_from_name)(void *, const char *, const char *);
static const void *(*class_get_methods)(void *, void **);
static const char *(*method_get_name)(const void *);
static uint32_t (*method_get_param_count)(const void *);
static uint32_t (*method_get_flags)(const void *,uint32_t *);
static void *(*method_get_class)(const void *);
static void *(*class_get_fields)(void *, void **);
static const char *(*field_get_name)(void *);
static int32_t (*field_get_offset)(void *);
static uint32_t (*field_get_flags)(void *);
static const void *(*field_get_type)(void *);
static char *(*type_get_name)(const void *);
static void (*runtime_free)(void *);
static void *(*runtime_invoke)(const void *, void *, void **, void **);
static void *(*object_unbox)(void *);
static void *(*object_get_class)(void *);
static void (*field_get_value)(void *, void *, void *);
static void (*field_static_get_value)(void *, void *);
static void (*field_set_value)(void *, void *, void *);
static void (*field_set_value_object)(void *, void *, void *);
static const void *(*method_get_param)(const void *, uint32_t);
static const void *(*method_get_return_type)(const void *);
static const char *(*class_get_name)(void *);
static const void *(*class_get_method_from_name)(void *, const char *, int);
static void *(*class_get_field_from_name)(void *, const char *);
static bool (*class_is_valuetype)(void *);
static int32_t (*class_value_size)(void *,uint32_t *);
static void *(*class_get_parent)(void *);
static void *(*field_get_value_object)(void *, void *);
static const void *(*class_get_type)(void *);
static void *(*type_get_object)(const void *);
static uintptr_t (*array_length)(void *);
static uint32_t (*array_object_header_size)(void);
static void *(*array_new)(void *,uintptr_t);
static void *(*object_new)(void *);
static uint32_t (*gchandle_new)(void *,bool);
static void *(*gchandle_get_target)(uint32_t);
static void (*gchandle_free)(uint32_t);
static const uint16_t *(*string_chars)(void *);
static int32_t (*string_length)(void *);
static void *(*string_new)(const char *);
static void *hub_class;
static bool bind_api(void) {
    bool ok = true;
#define API(name) do { name = dlsym(RTLD_DEFAULT, "il2cpp_" #name); \
    if (!name) { fprintf(output, "missing_api=%s\n", #name); ok = false; } } while (0)
    API(domain_get); API(domain_get_assemblies); API(assembly_get_image);
    API(image_get_name); API(class_from_name); API(class_get_methods);
    API(method_get_name); API(method_get_param_count); API(method_get_flags); API(class_get_fields);
    API(field_get_name); API(field_get_offset); API(field_get_flags);
    API(field_get_type); API(type_get_name);
    API(runtime_invoke); API(object_unbox); API(object_get_class);
    API(field_get_value); API(field_static_get_value); API(field_set_value); API(field_set_value_object); API(method_get_param);
    API(method_get_return_type); API(class_get_name); API(class_get_method_from_name);
    API(class_get_field_from_name); API(class_is_valuetype); API(class_value_size);
    API(class_get_parent); API(field_get_value_object); API(class_get_type);
    API(type_get_object); API(array_length); API(array_object_header_size);
    API(array_new); API(object_new); API(gchandle_new); API(gchandle_get_target); API(gchandle_free); API(method_get_class);
    API(string_chars); API(string_length); API(string_new);
#undef API
    runtime_free = dlsym(RTLD_DEFAULT, "il2cpp_free");
    return ok;
}

static NSString *managed_string(void *value) {
    if (!value) return @"";
    if (strcmp(class_get_name(object_get_class(value)),"String")) return @"<not-string>";
    int32_t length=string_length(value);
    if (length<0 || length>2000000) return @"<invalid-length>";
    return [[NSString alloc] initWithCharacters:string_chars(value) length:length];
}

static void *invoke(void *object, const char *name, int argc, void **args, bool *ok) {
    *ok=false;
    if (!object) return NULL;
    void *klass=object_get_class(object);
    const void *method=class_get_method_from_name(klass,name,argc);
    if (!method) { fprintf(output,"missing_method=%s.%s\n",class_get_name(klass),name); return NULL; }
    void *exception=NULL;
    // Inherited reference-type methods (e.g. System.Enum.ToString) need the box.
    void *declaring=method_get_class(method);
    void *receiver=class_is_valuetype(klass) && class_is_valuetype(declaring) ? object_unbox(object) : object;
    void *value=runtime_invoke(method,receiver,args,&exception);
    if (exception) { fprintf(output,"exception=%s method=%s\n",class_get_name(object_get_class(exception)),name); return NULL; }
    *ok=true;
    return value;
}

static const void *typed_method(void *klass,const char *name,int argc,const char *first_type) {
    for(void *k=klass;k;k=class_get_parent(k)) {
        void *iter=NULL;const void *method;
        while((method=class_get_methods(k,&iter))) {
            if(strcmp(method_get_name(method),name) || method_get_param_count(method)!=argc)continue;
            char *type=type_get_name(method_get_param(method,0));
            bool match=type && !strcmp(type,first_type);
            if(type && runtime_free)runtime_free(type);
            if(match)return method;
        }
    }
    return NULL;
}

static void *invoke_typed(void *object,const char *name,int argc,const char *first_type,void **args,bool *ok) {
    *ok=false;const void *method=typed_method(object_get_class(object),name,argc,first_type);
    if(!method){fprintf(output,"missing_overload=%s first=%s\n",name,first_type);return NULL;}
    void *declaring=method_get_class(method);
    void *receiver=class_is_valuetype(object_get_class(object)) && class_is_valuetype(declaring) ? object_unbox(object) : object;
    void *exception=NULL,*value=runtime_invoke(method,receiver,args,&exception);
    if(exception){fprintf(output,"exception=%s method=%s\n",class_get_name(object_get_class(exception)),name);return NULL;}
    *ok=true;return value;
}

static void *field_reference(void *object,const char *name) {
    if (!object) return NULL;
    void *field=class_get_field_from_name(object_get_class(object),name), *value=NULL;
    if (field) {
        if(field_get_flags(field)&0x10)field_static_get_value(field,&value);
        else field_get_value(object,field,&value);
    }
    return value;
}

static void *get_hub(void) {
    if (!hub_class) return NULL;
    const void *method=class_get_method_from_name(hub_class,"get_instance",0);
    if (!method) return NULL;
    void *exception=NULL;
    void *hub=runtime_invoke(method,NULL,NULL,&exception);
    return exception ? NULL : hub;
}

static NSString *scalar_string(void *object) {
    if (!object) return @"<null>";
    const char *name=class_get_name(object_get_class(object));
    if (!strcmp(name,"String")) return managed_string(object);
    if (!strcmp(name,"Boolean")) return *(bool *)object_unbox(object) ? @"true" : @"false";
    if (!strcmp(name,"Int32")) return [NSString stringWithFormat:@"%d",*(int *)object_unbox(object)];
    if (!strcmp(name,"Single")) return [NSString stringWithFormat:@"%g",*(float *)object_unbox(object)];
    return [NSString stringWithFormat:@"<%s>",name];
}

static id render_value(void *object,int depth) {
    if (!object) return NSNull.null;
    const char *name=class_get_name(object_get_class(object));
    if(!name || ![NSString stringWithUTF8String:name])return @"<unavailable-type>";
    if (!strcmp(name,"String")) return managed_string(object);
    if (!strcmp(name,"Boolean")) return @(*(bool *)object_unbox(object));
    if (!strcmp(name,"Int32")) return @(*(int *)object_unbox(object));
    if (!strcmp(name,"UInt32")) return @(*(uint32_t *)object_unbox(object));
    if (!strcmp(name,"Int64")) return @(*(int64_t *)object_unbox(object));
    if (!strcmp(name,"UInt64")) return @(*(uint64_t *)object_unbox(object));
    if (!strcmp(name,"IntPtr") || !strcmp(name,"UIntPtr")) return [NSString stringWithFormat:@"0x%llx",(unsigned long long)*(uintptr_t *)object_unbox(object)];
    if (!strcmp(name,"Double")) {double f=*(double *)object_unbox(object);return isfinite(f) ? @(f) : @"nonfinite";}
    if (!strcmp(name,"Single")) { float f=*(float *)object_unbox(object); return isfinite(f) ? @(f) : @"nonfinite"; }
    if (depth>=3) return scalar_string(object);
    if (!strcmp(name,"List`1")) {
        bool ok=false; void *boxed=invoke(object,"get_Count",0,NULL,&ok);
        if (!ok || !boxed) return @"<list-unavailable>";
        int count=*(int *)object_unbox(boxed);
        if (count<0 || count>400) return @"<list-too-large>";
        NSMutableArray *items=[NSMutableArray array];
        for(int i=0;i<count;i++) { void *args[]={&i};void *item=invoke(object,"get_Item",1,args,&ok);if(!ok)break;[items addObject:render_value(item,depth+1)]; }
        return items;
    }
    NSMutableDictionary *dict=[NSMutableDictionary dictionaryWithObject:@(name) forKey:@"$type"];
    int count=0;
    for(void *klass=object_get_class(object);klass && count<70;klass=class_get_parent(klass)) {
        void *iter=NULL,*field;
        while ((field=class_get_fields(klass,&iter)) && count<70) {
            if (field_get_flags(field)&0x10) continue;
            const char *fname=field_get_name(field);
            if (strstr(fname,"callback") || strstr(fname,"Callback") || strstr(fname,"parent")) continue;
            char *field_type=type_get_name(field_get_type(field));
            bool pointer=field_type && (strchr(field_type,'*') || !strcmp(field_type,"System.Reflection.Pointer"));
            if(field_type && runtime_free)runtime_free(field_type);
            if(pointer){dict[@(fname)]=@"<native-pointer>";continue;}
            void *value=field_get_value_object(field,object);
            dict[@(fname)]=render_value(value,depth+1);count++;
        }
    }
    return dict;
}

static NSArray *dictionary_entries(void *dictionary) {
    NSMutableArray *rows=[NSMutableArray array];
    bool ok=false;
    void *count_value=invoke(dictionary,"get_Count",0,NULL,&ok);
    if (!ok || !count_value) return rows;
    int count=*(int *)object_unbox(count_value);
    if (count<0 || count>6000) { fprintf(output,"dictionary_size_rejected=%d\n",count); return rows; }
    void *iterator=invoke(dictionary,"GetEnumerator",0,NULL,&ok);
    if (!ok || !iterator) return rows;
    for (int i=0;i<count;i++) {
        void *moved=invoke(iterator,"MoveNext",0,NULL,&ok);
        if (!ok || !moved || !*(bool *)object_unbox(moved)) break;
        void *pair=invoke(iterator,"get_Current",0,NULL,&ok);
        if (!ok || !pair) break;
        void *key=invoke(pair,"get_Key",0,NULL,&ok);
        if (!ok) break;
        void *value=invoke(pair,"get_Value",0,NULL,&ok);
        if (!ok) break;
        [rows addObject:@{@"key":scalar_string(key),@"value":render_value(value,0)}];
    }
    return rows;
}

static void *find_class(const char *space,const char *name) {
    size_t count=0;const void **assemblies=domain_get_assemblies(domain_get(),&count);
    for(size_t i=0;i<count && i<1024;i++) { void *k=class_from_name(assembly_get_image(assemblies[i]),space,name);if(k)return k; }
    return NULL;
}

static void *array_item(void *array,uintptr_t index) {
    if(!array || index>=array_length(array))return NULL;
    return ((void **)((char *)array+array_object_header_size()))[index];
}

static int object_int(void *object,const char *getter) {
    bool ok=false;void *value=invoke(object,getter,0,NULL,&ok);
    return ok && value ? *(int *)object_unbox(value) : -1;
}

static NSString *object_name(void *object) {
    bool ok=false;void *value=invoke(object,"get_name",0,NULL,&ok);
    return ok && value ? managed_string(value) : @"";
}

static bool set_field(void *object,const char *name,void *value) {
    void *field=class_get_field_from_name(object_get_class(object),name);
    if(!field){fprintf(output,"missing_field=%s\n",name);return false;}
    field_set_value(object,field,value);return true;
}

static bool override_volume(NSString *name,NSDictionary *settings,bool reset) {
    bool ok=false;void *exception=NULL;
    if(reset) {
        if(override_volume_handle) {
            void *object=find_class("UnityEngine","Object"),*args[]={gchandle_get_target(override_volume_handle)};
            const void *destroy=class_get_method_from_name(object,"Destroy",1);
            runtime_invoke(destroy,NULL,args,&exception);
            gchandle_free(override_volume_handle);gchandle_free(override_profile_handle);
            override_volume_handle=override_profile_handle=0;
        }
        character_shadow_override_enabled=false;
        character_shadow_startup_pending=false;
        fprintf(output,"volume_result=reset\n");return exception==NULL;
    }
    bool characterShadow=[name isEqual:@"HGCharacterVolume"];
    if(!characterShadow)return false;
    void *profile=override_profile_handle ? gchandle_get_target(override_profile_handle) : NULL;
    if(!profile) {
        void *go_class=find_class("UnityEngine","GameObject"),*volume_class=find_class("UnityEngine.Rendering","Volume"),*profile_class=find_class("UnityEngine.Rendering","VolumeProfile"),*scriptable=find_class("UnityEngine","ScriptableObject");
        if(!go_class || !volume_class || !profile_class || !scriptable){fprintf(output,"volume_result=class_missing\n");return false;}
        const void *create=typed_method(scriptable,"CreateInstance",1,"System.Type");
        void *profile_args[]={type_get_object(class_get_type(profile_class))};
        profile=runtime_invoke(create,NULL,profile_args,&exception);
        if(exception || !profile){fprintf(output,"volume_result=profile_failed\n");return false;}
        void *go=object_new(go_class),*name_args[]={string_new("Endfield Mac Graphics Override")};
        const void *ctor=typed_method(go_class,".ctor",1,"System.String");
        runtime_invoke(ctor,go,name_args,&exception);
        if(exception){fprintf(output,"volume_result=gameobject_failed\n");return false;}
        void *type_args[]={type_get_object(class_get_type(volume_class))};
        void *volume=invoke_typed(go,"AddComponent",1,"System.Type",type_args,&ok);
        if(!ok || !volume){fprintf(output,"volume_result=component_failed\n");return false;}
        bool global=true;void *global_args[]={&global};invoke(volume,"set_isGlobal",1,global_args,&ok);
        float priority=100000.0f,weight=1.0f;
        set_field(volume,"priority",&priority);set_field(volume,"weight",&weight);
        void *profile_field=class_get_field_from_name(object_get_class(volume),"sharedProfile");
        field_set_value_object(volume,profile_field,profile);
        fprintf(output,"volume_profile_reference=%s\n",field_reference(volume,"sharedProfile")==profile ? "verified" : "mismatch");
        void *setprofile_args[]={profile};invoke(volume,"SetInstantiatedProfile",1,setprofile_args,&ok);
        invoke(volume,"Play",0,NULL,&ok);
        void *object=find_class("UnityEngine","Object"),*persist_args[]={go};
        const void *persist=class_get_method_from_name(object,"DontDestroyOnLoad",1);
        runtime_invoke(persist,NULL,persist_args,&exception);
        override_volume_handle=gchandle_new(go,false);override_profile_handle=gchandle_new(profile,false);
    }
    void *component_class=find_class("HG.Rendering.Runtime",name.UTF8String);
    bool overrides=false;void *add_args[]={type_get_object(class_get_type(component_class)),&overrides};
    void *component=NULL,*components=field_reference(profile,"components");
    for(int i=0;i<object_int(components,"get_Count");i++){
        void *args[]={&i},*entry=invoke(components,"get_Item",1,args,&ok);
        if(entry && object_get_class(entry)==component_class){component=entry;break;}
    }
    if(!component)component=invoke_typed(profile,"Add",2,"System.Type",add_args,&ok);
    if(!ok || !component){fprintf(output,"volume_result=profile_add_failed\n");return false;}
    if(characterShadow){
        // Override only the native self-shadow quality gate. Disabling this restores
        // the scene's blended parameter without removing other graphics overrides.
        void *parameter=field_reference(component,"charSelfShadowQualityMode");
        if(!parameter){fprintf(output,"volume_result=parameter_missing\n");return false;}
        int tier=0; // HGCharacterVolume.CharacterRenderFeatureQualityTier.AlwaysOn
        bool enabled=settings[@"enabled"] ? [settings[@"enabled"] boolValue] : true;
        void *args[]={&tier};invoke(parameter,"set_value",1,args,&ok);
        bool success=ok && set_field(parameter,"overrideState",&enabled);
        void *actual=invoke(parameter,"get_value",0,NULL,&ok);
        success=success && ok && actual && *(int *)object_unbox(actual)==tier;
        if(success){
            character_shadow_override_enabled=enabled;
            character_shadow_startup_pending=false;
            if([settings[@"remember"] boolValue])
                [NSUserDefaults.standardUserDefaults setBool:enabled forKey:character_shadow_saved_key];
        }
        fprintf(output,"volume_parameter=charSelfShadowQualityMode value=%d override=%d\n",tier,enabled);
        fprintf(output,"volume_result=%s component=%s\n",success ? "override_installed" : "failed",name.UTF8String);
        return success;
    }
    return false;
}

static bool override_parameter(void *hub,NSString *key,NSString *value,bool reset) {
    NSArray *parts=[key componentsSeparatedByString:@"|"];
    if(parts.count!=2)return false;
    void *impl=field_reference(hub,"m_impl"),*dict=field_reference(impl,"m_registeredParameters");
    void *arg[]={string_new([parts[1] UTF8String])};bool ok=false;
    void *list=invoke(dict,"get_Item",1,arg,&ok);
    if(!ok || !list)return false;
    int count=object_int(list,"get_Count");
    for(int i=0;i<count && i<300;i++) {
        void *index[]={&i},*parameter=invoke(list,"get_Item",1,index,&ok);
        if(!ok || !parameter)continue;
        void *name=invoke(parameter,"get_paramName",0,NULL,&ok);
        if(!ok || ![managed_string(name) isEqual:parts[0]])continue;
        if(reset)invoke(parameter,"Reset",0,NULL,&ok);
        else { void *v[]={string_new(value.UTF8String)};invoke(parameter,"OverrideWithString",1,v,&ok); }
        if(!ok)return false;
        invoke(parameter,"MarkFeatureDirty",0,NULL,&ok);
        if(!ok)return false;
        invoke(hub,"RefreshDirtySettings",0,NULL,&ok);
        void *readback=invoke(parameter,"get_paramValue",0,NULL,&ok);
        fprintf(output,"effective_parameter=%s value=%s\n",key.UTF8String,scalar_string(readback).UTF8String);
        return ok;
    }
    fprintf(output,"parameter_not_registered=%s\n",key.UTF8String);
    return false;
}
