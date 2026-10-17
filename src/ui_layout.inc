/* Apply the authored PC reference size to the active gameplay UI canvases. */
static NSMutableArray *input_ui_layout_helpers;
static float input_ui_reference_scale=1.25f;
static NSUInteger input_ui_layout_canvases;

static void input_ui_layout_restore(void) {
    for(NSDictionary *entry in input_ui_layout_helpers){
        void *helper=gchandle_get_target([entry[@"handle"] unsignedIntValue]);
        if(!input_unity_alive(helper)){gchandle_free([entry[@"handle"] unsignedIntValue]);continue;}
        void *klass=object_get_class(helper);bool custom=[entry[@"custom"] boolValue],ok=false;
        float scale=[entry[@"scale"] floatValue];
        field_set_value(helper,class_get_field_from_name(klass,"_useCustomStandardResolution"),&custom);
        field_set_value(helper,class_get_field_from_name(klass,"_customPCReferenceResolutionScale"),&scale);
        invoke(helper,"UpdateCanvas",0,NULL,&ok);invoke(helper,"ForceCanvasUpdate",0,NULL,&ok);
        gchandle_free([entry[@"handle"] unsignedIntValue]);
    }
    input_ui_layout_helpers=nil;input_ui_layout_canvases=0;
}

static void input_ui_layout_update(void) {
    if(!input_ui_layout_enabled)return;
    void *keyboard=input_static(find_class("Beyond","DeviceInfo"),"get_usingKeyboard",0,NULL);
    if(!keyboard || !*(bool *)object_unbox(keyboard)){input_ui_layout_restore();return;}
    void *klass=find_class("Beyond.UI","UICanvasScaleHelper");
    void *width=input_static(find_class("UnityEngine","Screen"),"get_width",0,NULL);
    void *height=input_static(find_class("UnityEngine","Screen"),"get_height",0,NULL);
    if(!klass || !width || !height)return;
    int screenWidth=*(int *)object_unbox(width),screenHeight=*(int *)object_unbox(height);
    if(!input_ui_layout_helpers)input_ui_layout_helpers=[NSMutableArray array];
    NSUInteger changed=0;
    for(NSString *name in @[@"UIRoot",@"UIDummyNaviLayerRoot"]){
        NSMutableDictionary *saved=nil;bool ok=false;
        for(NSMutableDictionary *entry in input_ui_layout_helpers)
            if([entry[@"name"] isEqual:name]){saved=entry;break;}
        void *helper=saved ? gchandle_get_target([saved[@"handle"] unsignedIntValue]) : NULL;
        if(saved && !input_unity_alive(helper)){
            gchandle_free([saved[@"handle"] unsignedIntValue]);
            [input_ui_layout_helpers removeObject:saved];saved=nil;helper=NULL;
        }
        if(!helper){
            void *findArgs[]={string_new(name.UTF8String)};
            void *root=input_static(find_class("UnityEngine","GameObject"),"Find",1,findArgs);
            if(!root)continue;
            void *componentArgs[]={type_get_object(class_get_type(klass))};
            helper=invoke_typed(root,"GetComponent",1,"System.Type",componentArgs,&ok);
            if(!ok || !input_unity_alive(helper))continue;
        }
        void *canvases=field_reference(helper,"m_canvasList");
        void *count=invoke(canvases,"get_Count",0,NULL,&ok);
        if(!ok || !count || *(int *)object_unbox(count)==0)continue;
        void *versionField=class_get_field_from_name(object_get_class(canvases),"_version");
        int version=0;if(versionField)field_get_value(canvases,versionField,&version);
        if(!saved){
            bool custom=false;float scale=1;
            field_get_value(helper,class_get_field_from_name(klass,"_useCustomStandardResolution"),&custom);
            field_get_value(helper,class_get_field_from_name(klass,"_customPCReferenceResolutionScale"),&scale);
            saved=[@{@"name":name,@"handle":@(gchandle_new(helper,false)),@"custom":@(custom),@"scale":@(scale)} mutableCopy];
            [input_ui_layout_helpers addObject:saved];
        }
        NSString *signature=[NSString stringWithFormat:@"%d:%d:%d:%g",version,screenWidth,screenHeight,input_ui_reference_scale];
        if([saved[@"signature"] isEqual:signature])continue;
        // Screen-space canvases use GetProperCanvasResolution. World-space UI
        // uses CalcWorldCanvasParams instead, which retains the mobile reference.
        bool custom=true;
        field_set_value(helper,class_get_field_from_name(klass,"_useCustomStandardResolution"),&custom);
        field_set_value(helper,class_get_field_from_name(klass,"_customPCReferenceResolutionScale"),&input_ui_reference_scale);
        invoke(helper,"UpdateCanvas",0,NULL,&ok);if(!ok)continue;
        void *iterator=invoke(canvases,"GetEnumerator",0,NULL,&ok);
        for(int j=0;iterator && j<250;j++){
            void *more=invoke(iterator,"MoveNext",0,NULL,&ok);if(!ok || !more || !*(bool *)object_unbox(more))break;
            void *canvas=invoke(iterator,"get_Current",0,NULL,&ok);
            void *mode=invoke(canvas,"get_renderMode",0,NULL,&ok);if(!ok || !mode)continue;
            if(*(int *)object_unbox(mode)!=2){changed++;continue;}
            void *transform=invoke(canvas,"get_transform",0,NULL,&ok);
            void *rect=invoke(transform,"get_rect",0,NULL,&ok);if(!ok || !rect)continue;
            float *r=object_unbox(rect),size[2]={r[2]*input_ui_reference_scale,r[3]*input_ui_reference_scale};
            void *scaleValue=invoke(transform,"get_localScale",0,NULL,&ok);if(!ok || !scaleValue)continue;
            float *s=object_unbox(scaleValue),scale[3]={s[0]/input_ui_reference_scale,s[1]/input_ui_reference_scale,s[2]/input_ui_reference_scale};
            void *sizeArgs[]={size},*scaleArgs[]={scale};
            invoke_typed(transform,"set_sizeDelta",1,"UnityEngine.Vector2",sizeArgs,&ok);
            if(ok)invoke_typed(transform,"set_localScale",1,"UnityEngine.Vector3",scaleArgs,&ok);
            if(ok)changed++;
        }
        saved[@"signature"]=signature;
    }
    if(changed){input_ui_layout_canvases=changed;fprintf(output,"input_ui_layout=applied reference_scale=%g canvases=%lu\n",input_ui_reference_scale,(unsigned long)changed);}
}
