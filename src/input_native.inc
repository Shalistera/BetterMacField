/* Inspect native view ownership without recording account text or web content. */
static bool input_native_visible(id view) {
    if(!view)return false;
    @try {return ![[view valueForKey:@"hidden"] boolValue] && [[view valueForKey:@"alpha"] doubleValue]>0.01 &&
        NSWidth([[view valueForKey:@"bounds"] rectValue])>0 && NSHeight([[view valueForKey:@"bounds"] rectValue])>0;}
    @catch(NSException *exception) {return false;}
}

static id input_native_unity_view(id view,int depth) {
    if(depth>16 || !input_native_visible(view))return nil;
    Class unity=NSClassFromString(@"UnityView");
    if(unity && [view isKindOfClass:unity])return view;
    for(id child in [view valueForKey:@"subviews"]){id found=input_native_unity_view(child,depth+1);if(found)return found;}
    return nil;
}

static bool input_native_overlay_visible(void) {
    @try {
        id application=[NSClassFromString(@"UIApplication") valueForKey:@"sharedApplication"];
        for(id window in [application valueForKey:@"windows"]){
            if(!input_native_visible(window))continue;
            id branch=input_native_unity_view(window,0);
            if(!branch)continue;
            // Native SDKs and WKWebView are presented above the Unity view branch.
            while(branch && branch!=window){
                id parent=[branch valueForKey:@"superview"];bool above=false;
                for(id sibling in [parent valueForKey:@"subviews"]){
                    if(sibling==branch){above=true;continue;}
                    if(above && input_native_visible(sibling) && [[sibling valueForKey:@"userInteractionEnabled"] boolValue])return true;
                }
                branch=parent;
            }
        }
    } @catch(NSException *exception) {}
    return false;
}
