/* Local HUD text only; the original string is retained for toggling back. */
static uint32_t bmf_uid_text;
static NSString *bmf_uid_original;
static bool bmf_uid_replaced;

static void bmf_uid_update(void) {
    bool enabled=[bmf_settings[@"privateUID"] boolValue];
    void *text=bmf_uid_text ? gchandle_get_target(bmf_uid_text) : NULL;
    if(text && !input_unity_alive(text)){
        gchandle_free(bmf_uid_text);bmf_uid_text=0;text=NULL;
        bmf_uid_original=nil;bmf_uid_replaced=false;
    }
    if(!text && enabled){
        // Authored UID label; the adjacent latency text is independent.
        void *args[]={string_new("UIDPanelPanel/Main/TextNode/Text")};
        void *root=input_static(find_class("UnityEngine","GameObject"),"Find",1,args);
        void *klass=find_class("Beyond.UI","UIText");
        if(!root || !klass)return;
        bool ok=false;void *component[]={type_get_object(class_get_type(klass))};
        text=invoke_typed(root,"GetComponent",1,"System.Type",component,&ok);
        if(!ok || !input_unity_alive(text))return;
        bmf_uid_text=gchandle_new(text,false);
    }
    if(!text)return;
    bool ok=false;void *value=invoke(text,"get_text",0,NULL,&ok);
    if(!ok || !value)return;
    NSString *current=managed_string(value),*replacement=@"不给你看";
    if(enabled){
        if([current isEqual:replacement])return;
        bmf_uid_original=current;
        void *args[]={string_new(replacement.UTF8String)};
        invoke(text,"set_text",1,args,&ok);bmf_uid_replaced=ok;
    }else{
        if(bmf_uid_replaced && [current isEqual:replacement] && bmf_uid_original){
            void *args[]={string_new(bmf_uid_original.UTF8String)};
            invoke(text,"set_text",1,args,&ok);
            if(!ok)return;
        }
        gchandle_free(bmf_uid_text);bmf_uid_text=0;
        bmf_uid_original=nil;bmf_uid_replaced=false;
    }
}
