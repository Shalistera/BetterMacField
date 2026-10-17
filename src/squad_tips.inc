/* The PC squad prefab's warning list is skipped by Lua on mobile builds. */
static uint32_t input_squad_tip_template;
static bool input_squad_tip_original_active;
static NSMutableArray<NSNumber *> *input_squad_tip_cells;
static NSArray<NSString *> *input_squad_tip_ids;

static void input_squad_tips_reset(void) {
    for(NSNumber *handle in input_squad_tip_cells){
        void *args[]={gchandle_get_target(handle.unsignedIntValue)};
        input_static(find_class("UnityEngine","Object"),"Destroy",1,args);
        gchandle_free(handle.unsignedIntValue);
    }
    input_squad_tip_cells=nil;input_squad_tip_ids=nil;
    if(input_squad_tip_template){
        bool ok=false;void *args[]={&input_squad_tip_original_active};
        invoke(gchandle_get_target(input_squad_tip_template),"SetActive",1,args,&ok);
        gchandle_free(input_squad_tip_template);input_squad_tip_template=0;
    }
}

static void input_squad_tips_update(void) {
    if(!input_watch_fit_enabled)return;
    void *device=find_class("Beyond","DeviceInfo");
    void *keyboard=input_static(device,"get_usingKeyboard",0,NULL);
    void *mobile=input_static(device,"get_isMobile",0,NULL);
    if(!keyboard || !*(bool *)object_unbox(keyboard) || !mobile || !*(bool *)object_unbox(mobile)){
        input_squad_tips_reset();return;
    }
    void *findArgs[]={string_new("SquadIconPanel")};
    void *panel=input_static(find_class("UnityEngine","GameObject"),"Find",1,findArgs);
    if(!panel)return;
    bool ok=false;void *transform=invoke(panel,"get_transform",0,NULL,&ok);
    void *path[]={string_new("Main/DisableLayout/DisableTips")};
    void *tipTransform=invoke(transform,"Find",1,path,&ok);
    void *tip=invoke(tipTransform,"get_gameObject",0,NULL,&ok);
    if(!ok || !tip)return;
    if(input_squad_tip_template && gchandle_get_target(input_squad_tip_template)!=tip)input_squad_tips_reset();
    if(!input_squad_tip_template){
        void *active=invoke(tip,"get_activeSelf",0,NULL,&ok);if(!ok || !active)return;
        input_squad_tip_original_active=*(bool *)object_unbox(active);
        input_squad_tip_template=gchandle_new(tip,false);input_squad_tip_cells=[NSMutableArray array];
    }
    void *tips=invoke(input_battle_manager(),"get_squadDisableTips",0,NULL,&ok);
    void *countValue=invoke(tips,"get_Count",0,NULL,&ok);if(!ok || !countValue)return;
    int count=*(int *)object_unbox(countValue);
    NSMutableArray *ids=[NSMutableArray array];
    for(int i=0;i<count;i++){
        void *args[]={&i},*id=invoke(tips,"get_Item",1,args,&ok);if(!ok || !id)return;
        [ids addObject:managed_string(id)];
    }
    if([input_squad_tip_ids isEqual:ids])return;
    const void *getText=typed_method(find_class("Beyond.I18n","I18nUtils"),"GetText",1,"System.String");
    const void *clone=typed_method(find_class("UnityEngine","Object"),"Instantiate",2,"UnityEngine.Object");
    if(!getText || !clone)return;
    bool hidden=false;void *hideArgs[]={&hidden};invoke(tip,"SetActive",1,hideArgs,&ok);if(!ok)return;
    void *parent=invoke(tipTransform,"get_parent",0,NULL,&ok);if(!ok || !parent)return;
    while(input_squad_tip_cells.count<(NSUInteger)count){
        void *args[]={tip,parent},*exception=NULL,*cell=runtime_invoke(clone,NULL,args,&exception);
        if(exception || !cell)return;
        input_squad_tip_cells[input_squad_tip_cells.count]=@(gchandle_new(cell,false));
    }
    for(NSUInteger i=0;i<input_squad_tip_cells.count;i++){
        void *cell=gchandle_get_target(input_squad_tip_cells[i].unsignedIntValue);
        bool show=i<(NSUInteger)count;void *showArgs[]={&show};invoke(cell,"SetActive",1,showArgs,&ok);
        if(!show)continue;
        void *cellTransform=invoke(cell,"get_transform",0,NULL,&ok);
        void *textPath[]={string_new("Ani/DisableTxt")};
        void *textTransform=invoke(cellTransform,"Find",1,textPath,&ok);if(!ok || !textTransform)return;
        void *componentArgs[]={type_get_object(class_get_type(find_class("Beyond.UI","UIText")))};
        void *text=invoke_typed(textTransform,"GetComponent",1,"System.Type",componentArgs,&ok);if(!ok || !text)return;
        void *idArgs[]={string_new([ids[i] UTF8String])},*exception=NULL;
        void *localized=runtime_invoke(getText,NULL,idArgs,&exception);if(exception || !localized)return;
        void *valueArgs[]={localized};invoke(text,"set_text",1,valueArgs,&ok);if(!ok)return;
        invoke(cellTransform,"SetAsFirstSibling",0,NULL,&ok);
    }
    input_squad_tip_ids=[ids copy];
    fprintf(output,"input_squad_tips=count=%d template_hidden=1\n",count);
}
