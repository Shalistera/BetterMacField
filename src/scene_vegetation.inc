/* Mount prepared native-assets scene chunks through the game's VFS. */
#include "vegetation_files.inc"
static int vegetation_record_bytes;
static NSMutableArray *vegetation_route_chunks;
static NSArray *vegetation_route_specs;
static bool vegetation_startup_pending=true,vegetation_requested;
static NSString *vegetation_result=@"关闭";
static NSArray *vegetation_route_selection(NSDictionary *command) {return vegetation_route_specs;}

static bool vegetation_record_layout(void *klass) {
    if(!klass)return false;
    uint32_t alignment=0;
    int size=class_value_size(klass,&alignment);
    void *tag=class_get_field_from_name(klass,"fileTagHash");
    int extra=tag ? 8 : 0;
    if(size!=32+extra || (tag && field_get_offset(tag)!=24))return false;
    const char *names[]={"fileNameHash","fileChunkMD5NameIndex","ivSeed","offset","len","m_packedFlags"};
    const int offsets[]={16,24,28,32,36,40};
    for(int i=0;i<6;i++){
        void *field=class_get_field_from_name(klass,names[i]);
        if(!field || field_get_offset(field)!=offsets[i]+(i ? extra : 0))return false;
    }
    vegetation_record_bytes=size;
    return true;
}

static bool vegetation_direct_record(void *record) {
    void *positionClass=find_class("Beyond.VFS","EFileLoaderPosType");
    void *persistent=positionClass ? class_get_field_from_name(positionClass,"PersistAsset") : NULL;
    if(!persistent)return false;
    uint8_t position=0;field_static_get_value(persistent,&position);
    bool direct=true,ok=false;void *directArgs[]={&direct},*positionArgs[]={&position};
    invoke(record,"set_bIsDirectFileName",1,directArgs,&ok);
    if(!ok)return false;
    invoke(record,"set_loaderPosType",1,positionArgs,&ok);
    return ok;
}

static bool vegetation_set_chunk(void *chunks,int index,void *chunk) {
    for(void *klass=object_get_class(chunks);klass;klass=class_get_parent(klass)){
        void *iter=NULL;const void *method;
        while((method=class_get_methods(klass,&iter))){
            if(strcmp(method_get_name(method),"SetValue") || method_get_param_count(method)!=2)continue;
            char *type=type_get_name(method_get_param(method,1));bool match=type && !strcmp(type,"System.Int32");
            if(type)runtime_free(type);if(!match)continue;
            void *args[]={chunk,&index},*exception=NULL;runtime_invoke(method,chunks,args,&exception);return exception==NULL;
        }
    }
    return false;
}

static bool vegetation_original_matches(void *record,NSDictionary *spec) {
    bool ok=false;
    void *direct=invoke(record,"get_bIsDirectFileName",0,NULL,&ok);
    if(!ok || !direct || *(bool *)object_unbox(direct))return false;
    void *encrypted=invoke(record,"get_bUseEncrypt",0,NULL,&ok);
    if(!ok || !encrypted || *(bool *)object_unbox(encrypted))return false;
    void *value=invoke(record,"GetChunkFileRedirectedLoaderPath",0,NULL,&ok);
    NSString *path=ok && value ? managed_string(value) : nil;
    if(!path)return false;
    uint32_t offset=0,length=0;void *klass=object_get_class(record);
    field_get_value(record,class_get_field_from_name(klass,"offset"),&offset);
    field_get_value(record,class_get_field_from_name(klass,"len"),&length);
    if(length!=[spec[@"originalBytes"] unsignedIntValue])return false;
    NSFileHandle *file=[NSFileHandle fileHandleForReadingAtPath:path];
    NSData *bytes=nil;
    @try {
        [file seekToFileOffset:offset];bytes=[file readDataOfLength:length];
    } @catch(NSException *exception) {
        fprintf(output,"vegetation_route_original_read_failed=%s\n",exception.name.UTF8String);
    } @finally { [file closeFile]; }
    if(bytes.length!=length)return false;
    unsigned char digest[CC_MD5_DIGEST_LENGTH];CC_MD5(bytes.bytes,(CC_LONG)bytes.length,digest);
    NSMutableString *hash=[NSMutableString string];
    for(unsigned i=0;i<sizeof(digest);i++)[hash appendFormat:@"%02x",digest[i]];
    return [hash isEqual:spec[@"originalMD5"]];
}

static void *vegetation_route_record(void *vfs,NSDictionary *spec) {
    void *record=object_new(find_class("Beyond.VFS","FVFBlockFileInfo"));bool ok=false;
    void *args[]={string_new([spec[@"fileName"] UTF8String]),object_unbox(record)};
    void *found=invoke_typed(vfs,"GetFileInfo",2,"System.String",args,&ok);
    if(!ok || !found || !*(bool *)object_unbox(found))return NULL;
    void *name=invoke(record,"get_fileName",0,NULL,&ok);
    return ok && name && [managed_string(name) isEqual:spec[@"fileName"]] ? record : NULL;
}

static NSDictionary *vegetation_route_location(void *vfs,void *record) {
    uint32_t alignment=0;void *locationClass=find_class("Beyond.VFS","FVFSFileInfoLocation");
    if(!locationClass || class_value_size(locationClass,&alignment)!=16)return nil;
    int64_t hash=0;field_get_value(record,class_get_field_from_name(object_get_class(record),"fileNameHash"),&hash);
    // Serialized locations are eight bytes; runtime TryGetValue writes sixteen.
    unsigned char location[16]={0};int locationSize=8;bool located=false,ok=false;
    void *map=field_get_value_object(class_get_field_from_name(object_get_class(vfs),"m_lowMemoryFileName2FileInfo"),vfs);
    const void *method=NULL,*item;void *iter=NULL;
    while(map && (item=class_get_methods(object_get_class(map),&iter))){
        if(strcmp(method_get_name(item),"TryGetValue") || method_get_param_count(item)!=3)continue;
        char *type=type_get_name(method_get_param(item,1));
        bool match=type && !strcmp(type,"Beyond.VFS.FVFSFileInfoLocation&");
        if(type)runtime_free(type);if(match){method=item;break;}
    }
    if(method){
        void *lookup[]={&hash,location,&locationSize},*exception=NULL;
        void *value=runtime_invoke(method,object_unbox(map),lookup,&exception);
        located=!exception && value && *(bool *)object_unbox(value);
    }
    if(!located){
        map=field_get_value_object(class_get_field_from_name(object_get_class(vfs),"m_fileName2FileInfo"),vfs);
        void *created=invoke(map,"get_IsCreated",0,NULL,&ok);
        if(ok && created && *(bool *)object_unbox(created)){
            void *lookup[]={&hash,location};void *value=invoke(map,"TryGetValue",2,lookup,&ok);
            located=ok && value && *(bool *)object_unbox(value);
        }
    }
    if(!located)return nil;
    uint16_t chunk=0;int32_t file=0;memcpy(&chunk,location+2,2);memcpy(&file,location+4,4);
    return file>=0 ? @{@"block":@(location[0]),@"chunk":@(chunk),@"file":@(file)} : nil;
}

static bool vegetation_route_chunk(void *vfs,NSDictionary *group,void **chunks,void **chunk,void **files) {
    uint8_t blockType=[group[@"block"] unsignedCharValue];int index=[group[@"chunk"] intValue];bool ok=false;
    void *args[]={&blockType};void *block=invoke(vfs,"GetVFBlockByType",1,args,&ok);
    *chunks=ok && block ? field_reference(block,"allChunks") : NULL;
    if(!*chunks || index<0 || index>=array_length(*chunks))return false;
    void *chunkArgs[]={&index};*chunk=invoke_typed(*chunks,"GetValue",1,"System.Int32",chunkArgs,&ok);
    *files=ok && *chunk ? field_get_value_object(class_get_field_from_name(object_get_class(*chunk),"files"),*chunk) : NULL;
    return *files!=NULL;
}

static bool vegetation_route_verify(void *vfs,NSArray *groups,BOOL installed) {
    for(NSDictionary *group in groups)for(NSDictionary *entry in group[@"entries"]){
        void *record=vegetation_route_record(vfs,entry[@"spec"]);
        NSData *expected=entry[installed ? @"replacement" : @"original"];
        if(!record || expected.length!=vegetation_record_bytes ||
           memcmp(object_unbox(record),expected.bytes,vegetation_record_bytes))return false;
    }
    return true;
}

static bool vegetation_route_swap(void *vfs,NSMutableDictionary *group,BOOL install,BOOL checkOnly) {
    if([group[@"installed"] boolValue]==install)return true;
    void *chunks=NULL,*chunk=NULL,*files=NULL;
    if(!vegetation_route_chunk(vfs,group,&chunks,&chunk,&files))return false;
    void *expected=gchandle_get_target([group[install ? @"originalFiles" : @"privateFiles"] unsignedIntValue]);
    void *next=gchandle_get_target([group[install ? @"privateFiles" : @"originalFiles"] unsignedIntValue]);
    // Only swap the array we prepared. A native remount must not be overwritten.
    if(!expected || !next || *(void **)object_unbox(files)!=*(void **)object_unbox(expected) ||
       object_int(files,"get_Length")!=object_int(expected,"get_Length"))return false;
    if(checkOnly)return true;
    field_set_value(chunk,class_get_field_from_name(object_get_class(chunk),"files"),object_unbox(next));
    if(!vegetation_set_chunk(chunks,[group[@"chunk"] intValue],chunk))return false;
    group[@"installed"]=@(install);return true;
}

static void vegetation_route_free(NSArray *groups) {
    for(NSDictionary *group in groups){
        if([group[@"installed"] boolValue])continue;
        bool ok=false;uint32_t private=[group[@"privateFiles"] unsignedIntValue],original=[group[@"originalFiles"] unsignedIntValue];
        if(private){invoke(gchandle_get_target(private),"Dispose",0,NULL,&ok);gchandle_free(private);}
        if(original)gchandle_free(original);
    }
}

static bool vegetation_bundle_route(BOOL enabled,NSDictionary *command) {
    void *vfs=input_static(find_class("Beyond.VFS","VirtualFileSystem"),"get_instance",0,NULL);
    void *klass=find_class("Beyond.VFS","FVFBlockFileInfo");
    if(!vfs || !vegetation_record_layout(klass)){fprintf(output,"scene_vegetation=route_layout_unavailable\n");return false;}
    if(enabled && vegetation_route_chunks.count){fprintf(output,"scene_vegetation=route_already_enabled\n");return vegetation_route_verify(vfs,vegetation_route_chunks,YES);}
    if(!enabled){
        if(!vegetation_route_chunks.count){fprintf(output,"scene_vegetation=route_already_disabled\n");return true;}
        for(NSMutableDictionary *group in vegetation_route_chunks){
            if(!vegetation_route_verify(vfs,@[group],[group[@"installed"] boolValue])){
                fprintf(output,"scene_vegetation=route_identity_changed\n");return false;
            }
            if(!vegetation_route_swap(vfs,group,NO,YES)){
                fprintf(output,"scene_vegetation=route_array_changed\n");return false;
            }
        }
        bool restored=true;
        for(NSMutableDictionary *group in [vegetation_route_chunks reverseObjectEnumerator])
            if(!vegetation_route_swap(vfs,group,NO,NO))restored=false;
        if(!restored || !vegetation_route_verify(vfs,vegetation_route_chunks,NO)){
            fprintf(output,"scene_vegetation=route_restore_failed\n");return false;
        }
        NSUInteger count=vegetation_route_chunks.count;
        vegetation_route_free(vegetation_route_chunks);vegetation_route_chunks=nil;
        fprintf(output,"scene_vegetation=route_restored bundles=%lu chunks=%lu\n",(unsigned long)vegetation_route_specs.count,(unsigned long)count);return true;
    }
    NSArray *specs=vegetation_route_selection(command);
    if(!specs.count){fprintf(output,"scene_vegetation=route_selection_required\n");return false;}
    NSMutableArray *groups=[NSMutableArray array];NSMutableDictionary *byChunk=[NSMutableDictionary dictionary];
    NSString *failure=nil;bool ok=false;
    int allocator=0;void *allocatorField=class_get_field_from_name(find_class("Unity.Collections","Allocator"),"Persistent");
    if(!allocatorField){fprintf(output,"scene_vegetation=route_allocator_missing\n");return false;}
    field_static_get_value(allocatorField,&allocator);
    // Prepare every private array before publishing any replacement to the VFS.
    for(NSDictionary *spec in specs){
        void *record=vegetation_route_record(vfs,spec);
        if(!record || !vegetation_original_matches(record,spec)){failure=@"original_identity_changed";break;}
        NSDictionary *location=vegetation_route_location(vfs,record);
        if(!location){failure=@"index_missing";break;}
        void *chunks=NULL,*chunk=NULL,*files=NULL;
        if(!vegetation_route_chunk(vfs,location,&chunks,&chunk,&files)){failure=@"chunk_missing";break;}
        int fileIndex=[location[@"file"] intValue];void *getArgs[]={&fileIndex};
        void *actual=fileIndex<object_int(files,"get_Length") ? invoke(files,"get_Item",1,getArgs,&ok) : NULL;
        if(!ok || !actual || memcmp(object_unbox(actual),object_unbox(record),vegetation_record_bytes)){failure=@"record_changed";break;}
        NSData *before=[NSData dataWithBytes:object_unbox(record) length:vegetation_record_bytes];
        ok=vegetation_direct_record(record);
        void *pathValue=ok ? invoke(record,"GetChunkFileRedirectedLoaderPath",0,NULL,&ok) : NULL;
        NSString *path=ok && pathValue ? managed_string(pathValue) : nil;
        NSString *payload=[runtime_path(@"Vegetation") stringByAppendingPathComponent:spec[@"payload"]];
        NSData *bundle=[NSData dataWithContentsOfFile:payload];
        if(!path || !bundle || bundle.length!=[spec[@"bytes"] unsignedLongLongValue] ||
           ![vegetation_file_hash(bundle) isEqual:spec[@"sha256"]]){failure=@"scene_package_missing_or_changed";break;}
        NSString *fileError=vegetation_file_stage(runtime_path(@""),path,bundle);
        if(fileError){fprintf(output,"vegetation_file_error=%s\n",fileError.UTF8String);failure=@"scene_write_failed";break;}
        uint32_t offset=0,length=(uint32_t)bundle.length;
        field_set_value(record,class_get_field_from_name(klass,"offset"),&offset);
        field_set_value(record,class_get_field_from_name(klass,"len"),&length);
        NSString *key=[NSString stringWithFormat:@"%@:%@",location[@"block"],location[@"chunk"]];
        NSMutableDictionary *group=byChunk[key];
        if(!group){
            void *copy=object_new(object_get_class(files));char *type=type_get_name(class_get_type(object_get_class(files)));
            void *copyArgs[]={object_unbox(files),&allocator};invoke_typed(copy,".ctor",2,type,copyArgs,&ok);runtime_free(type);
            if(!ok){failure=@"copy_failed";break;}
            group=[@{@"block":location[@"block"],@"chunk":location[@"chunk"],@"installed":@NO,
                @"originalFiles":@(gchandle_new(files,false)),@"privateFiles":@(gchandle_new(copy,false)),
                @"entries":[NSMutableArray array]} mutableCopy];
            byChunk[key]=group;[groups addObject:group];
        }
        void *copy=gchandle_get_target([group[@"privateFiles"] unsignedIntValue]);
        void *setArgs[]={&fileIndex,object_unbox(record)};invoke(copy,"set_Item",2,setArgs,&ok);
        if(!ok){failure=@"write_failed";break;}
        [group[@"entries"] addObject:@{@"spec":spec,@"file":@(fileIndex),@"original":before,
            @"replacement":[NSData dataWithBytes:object_unbox(record) length:vegetation_record_bytes]}];
    }
    if(failure || !vegetation_route_verify(vfs,groups,NO)){
        vegetation_route_free(groups);fprintf(output,"scene_vegetation=route_prepare_failed reason=%s\n",(failure ?: @"record_changed").UTF8String);return false;
    }
    for(NSMutableDictionary *group in groups)if(!vegetation_route_swap(vfs,group,YES,NO)){failure=@"chunk_update_failed";break;}
    if(failure || !vegetation_route_verify(vfs,groups,YES)){
        bool restored=true;
        for(NSMutableDictionary *group in [groups reverseObjectEnumerator])if(!vegetation_route_swap(vfs,group,NO,NO))restored=false;
        if(restored)vegetation_route_free(groups);
        else {vegetation_route_chunks=groups;vegetation_route_specs=specs;}
        fprintf(output,"scene_vegetation=route_verify_failed restored=%d\n",restored);return false;
    }
    vegetation_route_chunks=groups;vegetation_route_specs=specs;
    fprintf(output,"scene_vegetation=route_enabled bundles=%lu chunks=%lu\n",(unsigned long)specs.count,(unsigned long)groups.count);
    return true;
}

static void vegetation_prepare(void) {
    NSString *error=vegetation_files_restore(runtime_path(@""));
    vegetation_requested=[bmf_settings[@"sceneVegetation"] boolValue];
    if(error){vegetation_result=error;vegetation_startup_pending=false;return;}
    if(!vegetation_requested){vegetation_startup_pending=false;return;}
    NSData *data=[NSData dataWithContentsOfFile:runtime_path(@"Vegetation/manifest.json")];
    NSDictionary *manifest=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if(![manifest[@"bundleIDs"] containsObject:NSBundle.mainBundle.bundleIdentifier] ||
       ![manifest[@"version"] isEqual:[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]]){
        vegetation_result=@"当前客户端的植被包尚未适配";vegetation_startup_pending=false;return;
    }
    vegetation_route_specs=manifest[@"bundles"];
    if(!vegetation_route_specs.count){vegetation_result=@"缺少植被场景包，请使用完整安装包";vegetation_startup_pending=false;return;}
    vegetation_result=@"等待场景资源初始化";
    [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationWillTerminateNotification object:nil queue:nil usingBlock:^(NSNotification *note){
        NSString *failure=vegetation_files_restore(runtime_path(@""));
        if(failure)fprintf(output,"vegetation_restore_error=%s\n",failure.UTF8String);
    }];
}
static void vegetation_startup_tick(void) {
    if(!vegetation_startup_pending)return;
    void *vfs=input_static(find_class("Beyond.VFS","VirtualFileSystem"),"get_instance",0,NULL);
    if(!vfs || !vegetation_route_record(vfs,vegetation_route_specs[0]))return;
    vegetation_startup_pending=false;
    bool installed=vegetation_bundle_route(YES,@{});
    vegetation_result=installed ? @"已启用，下次启动可关闭恢复" : @"植被恢复未生效，请复制日志反馈";
    if(!installed && !vegetation_route_chunks.count){
        NSString *error=vegetation_files_restore(runtime_path(@""));
        if(error)vegetation_result=error;
    }
    fprintf(output,"vegetation_startup=%s\n",vegetation_result.UTF8String);
}
static NSDictionary *vegetation_state(void) {
    return @{@"active":@(vegetation_route_chunks.count>0),@"chunks":@(vegetation_route_specs.count),
        @"restartRequired":@([bmf_settings[@"sceneVegetation"] boolValue]!=vegetation_requested),@"result":vegetation_result};
}
