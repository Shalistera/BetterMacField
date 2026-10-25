/* One recovery journal for loose scene files. Official .chk files stay intact. */
static NSString *vegetation_recovery_root(NSString *runtime) {
    NSString *root=runtime.stringByStandardizingPath;
    if([root.lastPathComponent isEqual:@"BetterMacField"] && [root.stringByDeletingLastPathComponent.lastPathComponent isEqual:@"tmp"])
        return [root.stringByDeletingLastPathComponent.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Library/Application Support/BetterMacField"];
    return root;
}
static NSString *vegetation_file_hash(NSData *data) {
    if(!data)return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,digest);
    NSMutableString *value=[NSMutableString string];
    for(unsigned i=0;i<sizeof(digest);i++)[value appendFormat:@"%02x",digest[i]];
    return value;
}
static NSString *vegetation_files_restore(NSString *runtime) {
    runtime=vegetation_recovery_root(runtime);
    NSString *journal=[runtime stringByAppendingPathComponent:@"vegetation-files.plist"];
    NSArray *entries=[NSArray arrayWithContentsOfFile:journal];
    NSFileManager *fm=NSFileManager.defaultManager;
    if(!entries)return [fm fileExistsAtPath:journal] ? @"植被恢复记录无法读取，请保留运行目录并反馈。" : nil;
    for(NSDictionary *entry in entries.reverseObjectEnumerator){
        NSString *path=entry[@"path"],*backup=entry[@"backup"];
        NSData *current=[NSData dataWithContentsOfFile:path];
        NSString *hash=vegetation_file_hash(current);
        BOOL saved=[fm fileExistsAtPath:backup];
        if(!saved && [hash isEqual:entry[@"previous"]])continue;
        if(current && ![hash isEqual:entry[@"installed"]])return [@"场景文件已有其他修改，请保留恢复记录并反馈：" stringByAppendingString:path];
        if(entry[@"previous"] && !saved)return @"植被恢复备份缺失，请保留运行目录并反馈。";
        NSError *error=nil;
        if(current && ![fm removeItemAtPath:path error:&error])return error.localizedDescription;
        if(saved && ![fm moveItemAtPath:backup toPath:path error:&error])return error.localizedDescription;
    }
    NSError *error=nil;
    if(![fm removeItemAtPath:journal error:&error])return error.localizedDescription;
    [fm removeItemAtPath:[runtime stringByAppendingPathComponent:@"VegetationBackup"] error:nil];
    return nil;
}
static NSString *vegetation_file_stage(NSString *runtime,NSString *path,NSData *data) {
    runtime=vegetation_recovery_root(runtime);
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *journal=[runtime stringByAppendingPathComponent:@"vegetation-files.plist"];
    NSMutableArray *entries=[[NSArray arrayWithContentsOfFile:journal] mutableCopy] ?: [NSMutableArray array];
    NSString *backup=[[runtime stringByAppendingPathComponent:@"VegetationBackup"] stringByAppendingPathComponent:[NSString stringWithFormat:@"%lu",(unsigned long)entries.count]];
    NSError *error=nil;
    if(![fm createDirectoryAtPath:backup.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error] ||
       ![fm createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error])return error.localizedDescription;
    NSMutableDictionary *entry=[@{@"path":path,@"backup":backup,@"installed":vegetation_file_hash(data)} mutableCopy];
    if([fm fileExistsAtPath:path]){
        NSData *old=[NSData dataWithContentsOfFile:path options:0 error:&error];
        if(!old)return error.localizedDescription;
        entry[@"previous"]=vegetation_file_hash(old);
    }
    [entries addObject:entry];
    if(![entries writeToFile:journal atomically:YES])return @"无法保存植被恢复记录。";
    if(entry[@"previous"] && ![fm moveItemAtPath:path toPath:backup error:&error])return error.localizedDescription;
    if(![data writeToFile:path options:NSDataWritingAtomic error:&error])return error.localizedDescription;
    return nil;
}
