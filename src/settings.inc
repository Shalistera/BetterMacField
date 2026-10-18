/* Launcher preferences are explicit per-client input; live menu changes return
   through the same file, and the comparison hotkey never overwrites them. */
static NSMutableDictionary *bmf_settings;
static bool bmf_settings_pending;
static NSString *bmf_settings_result=@"等待进入场景";
static bool bmf_background_limit=true;
static NSDictionary *bmf_field_catalog;
static void bmf_save_setting(NSString *key,id value) {
    if(!bmf_settings)bmf_settings=[NSMutableDictionary dictionary];
    if(value)bmf_settings[key]=value;
    [[NSJSONSerialization dataWithJSONObject:bmf_settings options:NSJSONWritingPrettyPrinted error:nil]
        writeToFile:runtime_path(@"bettermacfield-settings.json") atomically:YES];
}
static NSDictionary *bmf_valid_settings(NSDictionary *values) {
    NSMutableDictionary *valid=[NSMutableDictionary dictionary];
    for(NSDictionary *field in bmf_field_catalog[@"fields"]){
        NSString *key=field[@"key"];id value=values[key];
        if(!value)continue;
        if(field[@"choices"]){
            for(NSDictionary *choice in field[@"choices"])if([choice[@"value"] isEqual:value])valid[key]=value;
        }else if([value isKindOfClass:NSNumber.class])valid[key]=@([value boolValue]);
    }
    return valid;
}
static void bmf_settings_load(void) {
    NSData *catalog=[NSData dataWithContentsOfFile:runtime_path(@"features.json")];
    bmf_field_catalog=catalog ? [NSJSONSerialization JSONObjectWithData:catalog options:0 error:nil] : @{};
    NSData *data=[NSData dataWithContentsOfFile:runtime_path(@"bettermacfield-settings.json")];
    NSDictionary *values=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    bmf_settings=[NSMutableDictionary dictionary];
    for(NSDictionary *field in bmf_field_catalog[@"fields"])bmf_settings[field[@"key"]]=field[@"default"];
    [bmf_settings addEntriesFromDictionary:bmf_valid_settings(values ?: @{})];
    bmf_settings_pending=bmf_settings.count>0;
    if(bmf_settings[@"backgroundLimit"])bmf_background_limit=[bmf_settings[@"backgroundLimit"] boolValue];
}
static void bmf_request_settings(NSDictionary *values) {
    [bmf_settings addEntriesFromDictionary:bmf_valid_settings(values)];
    bmf_save_setting(nil,nil);bmf_settings_pending=true;bmf_settings_result=@"等待应用";
}
