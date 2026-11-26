// Replace the shipped skin and cloth fragment libraries with archived PC lighting.
// The game keeps its own compiled programs, bindings and materials; only the Metal
// library behind six known fragment stages changes, as it is loaded.
static NSDictionary<NSString *, NSString *> *char172Templates;
static unsigned char172Mode;
static atomic_uint char172Compiled, char172Rejected, char172Loads;
static NSString *char172CompileError;

static void char172InstallLibraries(unsigned mode) {
    char172Mode = mode;
    if (mode != 3 && mode != 4 && mode != 7) return;
    NSString *directory = graphicsResource172(@"character172");
    NSData *raw = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:@"manifest.json"]];
    NSDictionary *catalog = raw ? [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil] : nil;
    if (![catalog[@"version"] isEqual:@"1.7.2"] || ![catalog[@"route"] isEqual:@"library"]) return;
    NSMutableDictionary *templates = [NSMutableDictionary dictionary];
    for (NSString *original in catalog[@"sources"]) {
        NSDictionary *entry = catalog[@"sources"][original];
        NSData *file = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:entry[@"file"]]];
        if (!file || ![wardHash172(file.bytes, file.length) isEqual:entry[@"sha256"]]) return;
        NSString *source = [[NSString alloc] initWithData:file encoding:NSUTF8StringEncoding];
        templates[original] = [source stringByReplacingOccurrencesOfString:@"endfieldCharacterMode = 0u;"
                                          withString:[NSString stringWithFormat:@"endfieldCharacterMode = %uu;", mode]];
    }
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    SEL selector = @selector(newLibraryWithData:error:);
    Method method = device ? class_getInstanceMethod([device class], selector) : NULL;
    if (!templates.count || !method) return;
    char172Templates = [templates copy];
    IMP original = method_getImplementation(method);
    IMP replacement = imp_implementationWithBlock(^id(id object, dispatch_data_t data, NSError **error) {
        const void *bytes = NULL;
        size_t length = 0;
        __attribute__((objc_precise_lifetime)) dispatch_data_t mapped = dispatch_data_create_map(data, &bytes, &length);
        NSString *source = mapped && length ? char172Templates[wardHash172(bytes, length)] : nil;
        atomic_fetch_add(&char172Loads, 1);
        if (source) {
            MTLCompileOptions *options = [MTLCompileOptions new];
            options.languageVersion = MTLLanguageVersion2_3;
            NSError *failure = nil;
            id library = [object newLibraryWithSource:source options:options error:&failure];
            if (library) {
                atomic_fetch_add(&char172Compiled, 1);
                // The caller owns a new library; this block returns at +0.
                CFRetain((__bridge CFTypeRef)library);
                return library;
            }
            // Keep the shipped library when the replacement does not compile.
            char172CompileError = failure.localizedDescription ?: @"Metal 未能编译角色光照源码";
            atomic_fetch_add(&char172Rejected, 1);
        }
        return ((id (*)(id, SEL, dispatch_data_t, NSError **))original)(object, selector, data, error);
    });
    if (!class_addMethod([device class], selector, replacement, method_getTypeEncoding(method)))
        method_setImplementation(method, replacement);
}

// The mode is compiled into the libraries, so a new choice applies from the next launch.
static NSDictionary *updateCharacter172(int requested) {
    unsigned compiled = atomic_load(&char172Compiled), rejected = atomic_load(&char172Rejected);
    NSMutableDictionary *state = [@{@"mode":@(char172Templates ? char172Mode : 0), @"requestedMode":@(requested),
        @"libraries":@(compiled), @"rejected":@(rejected), @"loads":@(atomic_load(&char172Loads))} mutableCopy];
    if (char172CompileError) state[@"error"] = char172CompileError;
    if (char172Mode && !char172Templates) state[@"status"] = @"unavailable";
    else if (rejected) state[@"status"] = @"compileFailed";
    else if (!char172Mode) state[@"status"] = @"off";
    else state[@"status"] = compiled ? @"active" : @"waitingForActor";
    return state;
}
