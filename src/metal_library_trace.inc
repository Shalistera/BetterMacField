/* Attribute live Metal functions to the locally extracted iOS lighting programs. */
#include <CommonCrypto/CommonDigest.h>
static char metal_library_source_key,metal_function_source_key;
static NSDictionary *metal_known_lighting_sources,*metal_lighting_gbuffer_bindings;
static NSMutableSet *metal_library_classes;
static atomic_uint metal_source_libraries,metal_binary_libraries,metal_known_libraries;
static atomic_bool metal_source_sample_saved;
static NSUInteger metal_trace_source_bytes;
static NSDictionary *metal_ao_patches;
static id<MTLTexture> metal_ao_white,metal_ao_texture;
static id<MTLTexture> metal_contact_texture;
static atomic_uint metal_contact_draws;
static NSObject *metal_ao_lock;
static char metal_ao_pipeline_key;
static atomic_uint metal_ao_compiled,metal_ao_draws,metal_ao_bound_draws;
static atomic_bool metal_ao_automatic;
static _Atomic(float) metal_screen_ao_strength;
static _Atomic(float) metal_screen_ao_radius=1.0f;
static atomic_uint metal_screen_ao_draws;
static atomic_uint metal_screen_ao_debug;
static atomic_bool metal_screen_ao_legacy_pattern; // Live same-view AO sampling comparison only.
static _Atomic(float) metal_native_contact_strength;
static _Atomic(float) metal_native_contact_length=0.5f;
static atomic_uint metal_native_contact_draws;
static atomic_uint metal_native_contact_debug;
static char metal_ao_dimensions_key;

static NSNumber *metal_ao_size_key(id<MTLTexture> texture) {
    return @(((uint64_t)texture.width<<32) | texture.height);
}

static NSString *metal_sha256(NSData *data) {
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,hash);
    NSMutableString *digest=[NSMutableString string];
    for(int i=0;i<CC_SHA256_DIGEST_LENGTH;i++)[digest appendFormat:@"%02x",hash[i]];
    return digest;
}

#include "character_skin.inc"

static void metal_watch_library(id library,NSString *identity) {
    if(!library || !identity)return;
    objc_setAssociatedObject(library,&metal_library_source_key,identity,OBJC_ASSOCIATION_COPY_NONATOMIC);
    Class cls=object_getClass(library);
    @synchronized(metal_library_classes) {
        NSString *name=NSStringFromClass(cls);
        if([metal_library_classes containsObject:name])return;
        [metal_library_classes addObject:name];
        SEL selector=@selector(newFunctionWithName:);
        Method method=class_getInstanceMethod(cls,selector);
        if(!method)return;
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,NSString *functionName){
            id function=((id(*)(id,SEL,id))original)(object,@selector(newFunctionWithName:),functionName);
            NSString *source=objc_getAssociatedObject(object,&metal_library_source_key);
            if(function && source)objc_setAssociatedObject(function,&metal_function_source_key,source,OBJC_ASSOCIATION_COPY_NONATOMIC);
            return function;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
        selector=@selector(newFunctionWithName:constantValues:error:);
        method=class_getInstanceMethod(cls,selector);
        if(method) {
            IMP specialized=method_getImplementation(method);
            IMP wrapper=imp_implementationWithBlock(^id(id object,NSString *functionName,MTLFunctionConstantValues *values,NSError **error){
                id function=((id(*)(id,SEL,id,id,NSError **))specialized)(object,@selector(newFunctionWithName:constantValues:error:),functionName,values,error);
                NSString *source=objc_getAssociatedObject(object,&metal_library_source_key);
                if(function && source)objc_setAssociatedObject(function,&metal_function_source_key,source,OBJC_ASSOCIATION_COPY_NONATOMIC);
                return function;
            });
            if(!class_addMethod(cls,selector,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);
        }
    }
}

static void install_metal_library_trace(id<MTLDevice> device) {
    character_skin_load();
    Class cls=object_getClass(device);
    bool terrainDepth=getenv("ENDFIELD_TERRAIN_DEPTH_FIX")!=NULL;
    NSData *map=[NSData dataWithContentsOfFile:runtime_path(@"endfield-ios-lighting-source-map.json")];
    metal_known_lighting_sources=map ? [NSJSONSerialization JSONObjectWithData:map options:0 error:nil] : @{};
    NSData *bindingMap=[NSData dataWithContentsOfFile:runtime_path(@"endfield-ios-lighting-gbuffer-map.json")];
    metal_lighting_gbuffer_bindings=bindingMap ? [NSJSONSerialization JSONObjectWithData:bindingMap options:0 error:nil] : @{};
    metal_library_classes=[NSMutableSet set];
    metal_ao_lock=[NSObject new];
    const char *patchPath=getenv("ENDFIELD_AO_PATCHES_PATH");
    NSData *patchData=patchPath ? [NSData dataWithContentsOfFile:@(patchPath)] : nil;
    metal_ao_patches=patchData ? [NSJSONSerialization JSONObjectWithData:patchData options:0 error:nil] : @{};
    if(metal_ao_patches.count || character_skin_source) {
        MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO];
        descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageShaderRead;
        metal_ao_white=[device newTextureWithDescriptor:descriptor];metal_ao_white.label=@"Endfield AO disabled (white)";
        uint8_t value=255;[metal_ao_white replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&value bytesPerRow:1];
    }
    SEL selector=@selector(newLibraryWithSource:options:error:);
    Method method=class_getInstanceMethod(cls,selector);
    if(method) {
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,NSString *source,MTLCompileOptions *options,NSError **error){
            atomic_fetch_add(&metal_source_libraries,1);
            if(source.length>30000 && source.length<524288 && [source containsString:@"fragment"] &&
               [source containsString:@"texture_14"] && !atomic_exchange(&metal_source_sample_saved,true))
                [source writeToFile:runtime_path(@"endfield-ios-runtime-fragment-source.metal") atomically:YES encoding:NSUTF8StringEncoding error:nil];
            NSRange start=[source rangeOfString:@"#include <metal_stdlib>"];
            NSString *body=start.location==NSNotFound ? source : [source substringFromIndex:start.location];
            NSRange end=[body rangeOfString:[NSString stringWithFormat:@"%C",(unichar)0]];
            if(end.location!=NSNotFound)body=[body substringToIndex:end.location];
            NSData *data=[[body stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] dataUsingEncoding:NSUTF8StringEncoding];
            NSString *digest=metal_sha256(data);
            NSString *known=metal_known_lighting_sources[digest];
            if(metal_tracing && data.length<524288) {
                @synchronized(metal_library_classes) {
                    NSString *directory=runtime_path(@"endfield-ios-metal-sources");
                    NSString *path=[directory stringByAppendingPathComponent:[digest stringByAppendingString:@".metal"]];
                    if(metal_trace_source_bytes+data.length<=16*1024*1024 && ![NSFileManager.defaultManager fileExistsAtPath:path]) {
                        [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
                        if([data writeToFile:path atomically:YES])metal_trace_source_bytes+=data.length;
                    }
                }
            }
            NSDictionary *patch=known && metal_ao_white ? metal_ao_patches[digest] : nil;
            NSData *replacement=patch ? [NSData dataWithContentsOfFile:patch[@"source"]] : nil;
            bool patched=replacement && [metal_sha256(replacement) isEqual:patch[@"sha256"]];
            NSString *compiledSource=patched ? [[NSString alloc] initWithData:replacement encoding:NSUTF8StringEncoding] : source;
            bool skinPatched=character_skin_source && [digest isEqual:character_skin_original];
            bool skinNative=[source containsString:@"// Endfield native skin lighting"];
            if(skinPatched)compiledSource=character_skin_source;
            bool terrainPatched=false;
            if(terrainDepth && [digest isEqual:@"d8ce0cb599ec6cb2eb12f80557f892c267a2706bcb3bcefa16fd091195de94aa"]) {
                // The split prepass supplies slot12; lighting still consumes color4.
                // Preserve the terrain depth at the same pixels that write its GBuffer.
                compiledSource=[compiledSource stringByReplacingOccurrencesOfString:@"    float4 out_var_SV_Target3 [[color(3)]];"
                    withString:@"    float4 out_var_SV_Target3 [[color(3)]];\n    float terrain_depth [[color(4)]];"];
                compiledSource=[compiledSource stringByReplacingOccurrencesOfString:@"    return out;"
                    withString:@"    out.terrain_depth = _483;\n    return out;"];
                terrainPatched=true;
            }
            MTLCompileOptions *compileOptions=options;
            if(patched) {
                compileOptions=[options copy] ?: [MTLCompileOptions new];
                if(compileOptions.languageVersion<MTLLanguageVersion2_3)
                    compileOptions.languageVersion=MTLLanguageVersion2_3;
            }
            id library=((id(*)(id,SEL,id,id,NSError **))original)(object,@selector(newLibraryWithSource:options:error:),compiledSource,compileOptions,error);
            if(metal_tracing){
                NSString *failure=error && *error ? (*error).localizedDescription : @"";
                record_metal([NSString stringWithFormat:@"source sha256=%@ loaded=%d error=%@",digest,library!=nil,failure]);
            }
            if(!library && (patched || terrainPatched || skinPatched)) {
                skinPatched=false;
                fprintf(output,"render_shader_compile_failed=%s\n",known.UTF8String);patched=false;terrainPatched=false;
                if(error)*error=nil;
                library=((id(*)(id,SEL,id,id,NSError **))original)(object,@selector(newLibraryWithSource:options:error:),source,options,error);
            }
            if(library && patched)atomic_fetch_add(&metal_ao_compiled,1);
            if(library && skinPatched)atomic_fetch_add(&character_skin_compiled,1);
            if(library && skinNative)atomic_fetch_add(&character_skin_native_compiled,1);
            if(known)atomic_fetch_add(&metal_known_libraries,1);
            {
                metal_watch_library(library,[NSString stringWithFormat:@"%@ sha256=%@%@%@%@",known ?: @"runtime",digest,
                    patched ? (patch[@"contactShadow"] ? @" AO_PATCH CONTACT_PATCH" : @" AO_PATCH") : @"",terrainPatched ? @" TERRAIN_LIGHTING_DEPTH" : @"",skinPatched ? @" SKIN_PARITY" : skinNative ? @" SKIN_NATIVE" : @""]);
            }
            return library;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
    selector=@selector(newLibraryWithData:error:);method=class_getInstanceMethod(cls,selector);
    if(method) {
        IMP original=method_getImplementation(method);
        IMP replacement=imp_implementationWithBlock(^id(id object,dispatch_data_t data,NSError **error){
            atomic_fetch_add(&metal_binary_libraries,1);
            id library=nil;NSString *digest=nil,*known=nil;bool patched=false,skinPatched=false;
            NSDictionary *patch=nil;
            if(metal_tracing || metal_known_lighting_sources.count || metal_ao_patches.count || character_skin_source){
                const void *bytes=NULL;size_t length=0;
                dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&length);
                if(mapped && bytes && length<=16*1024*1024){
                    digest=metal_sha256([NSData dataWithBytesNoCopy:(void *)bytes length:length freeWhenDone:NO]);
                    known=metal_known_lighting_sources[digest];
                    patch=known && metal_ao_white ? metal_ao_patches[digest] : nil;
                    NSData *source=patch ? [NSData dataWithContentsOfFile:patch[@"source"]] : nil;
                    if(source && [metal_sha256(source) isEqual:patch[@"sha256"]]) {
                        MTLCompileOptions *options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_3;
                        NSError *failure=nil;
                        library=[object newLibraryWithSource:[[NSString alloc] initWithData:source encoding:NSUTF8StringEncoding]
                            options:options error:&failure];
                        patched=library!=nil;
                        fprintf(output,"render_binary_patch=%s candidate=%s loaded=%d error=%s\n",digest.UTF8String,
                            [patch[@"sha256"] UTF8String],patched,
                            failure.localizedDescription.UTF8String ?: "");
                    }
                    NSString *skinSource=character_skin_sources[digest];
                    if(skinSource) {
                        MTLCompileOptions *options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_3;
                        NSError *failure=nil;
                        library=[object newLibraryWithSource:skinSource options:options error:&failure];
                        skinPatched=library!=nil;
                        fprintf(output,"character_skin_binary_patch=%s loaded=%d error=%s\n",digest.UTF8String,skinPatched,
                            failure.localizedDescription.UTF8String ?: "");
                    }
                }
            }
            if(!library)library=((id(*)(id,SEL,dispatch_data_t,NSError **))original)(object,@selector(newLibraryWithData:error:),data,error);
            if(patched)atomic_fetch_add(&metal_ao_compiled,1);
            if(skinPatched)atomic_fetch_add(&character_skin_compiled,1);
            if(known)atomic_fetch_add(&metal_known_libraries,1);
            // Keep the already-computed identity for libraries created before a
            // trace starts, including character variants outside the patch map.
            if(digest) {
                NSString *identity=[NSString stringWithFormat:@"%@ binary sha256=%@ loaded=%d%@",known ?: @"runtime",digest,
                    library!=nil,skinPatched ? @" SKIN_PARITY" : patched ? @" AO_PATCH" : @""];
                if(metal_tracing)record_metal(identity);
                metal_watch_library(library,identity);
            }
            // This block returns at +0, while newLibraryWithData callers expect +1.
            // The raw original IMP leaves its owned result outside ARC; the nested
            // Objective-C newLibrary call above is ARC-managed and needs ownership
            // transferred explicitly to the original caller.
            if((patched || skinPatched) && library)CFRetain((__bridge CFTypeRef)library);
            return library;
        });
        if(!class_addMethod(cls,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
    }
}
