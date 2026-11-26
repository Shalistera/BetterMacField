// Route current native/managed camera settings to the official occlusion kernels.
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <fcntl.h>
#include <unistd.h>

static const void *sceneMethod172(void *cls, const char *name, int count) {
    static NSMutableDictionary *methods;
    if (!methods) methods = [NSMutableDictionary dictionary];
    NSString *key = [NSString stringWithFormat:@"%p/%s/%d",cls,name,count];
    NSValue *known = methods[key];
    if (!known && cls) {
        const void *method = lookupMethod(cls,name,count);
        if (method) methods[key] = known = [NSValue valueWithPointer:method];
    }
    return known.pointerValue;
}

static void *sceneField172(void *cls, const char *name) {
    static NSMutableDictionary *fields;
    if (!fields) fields = [NSMutableDictionary dictionary];
    NSString *key = [NSString stringWithFormat:@"%p/%s",cls,name];
    NSValue *known = fields[key];
    if (!known && cls) {
        void *field = lightField(cls,name);
        if (field) fields[key] = known = [NSValue valueWithPointer:field];
    }
    return known.pointerValue;
}

static void *sceneGet172(void *object, const char *name) {
    const void *method = object ? sceneMethod172(objectClass(object),name,0) : NULL;
    if (!method) return NULL;
    void *exception = NULL, *value = invoke(method,object,NULL,&exception);
    return exception ? NULL : value;
}

static void *sceneMember172(void *object, const char *name) {
    void *field = object ? sceneField172(objectClass(object),name) : NULL, *value = NULL;
    if (field) lightGet(object,field,&value);
    return value;
}

static BOOL sceneRead172(uintptr_t address, void *bytes, size_t count) {
    mach_vm_size_t read = 0;
    return address && mach_vm_read_overwrite(mach_task_self(),address,count,(mach_vm_address_t)bytes,&read)==KERN_SUCCESS && read==count;
}

static NSString *sceneRetentionReason172 = @"notRequested";
static uintptr_t sceneRetentionRVA172, sceneRetentionSlotOffset172;

// The metadata selects the method. Compare its original .text bytes against the
// same UUID on disk before replacing a writable vtable entry, without old RVAs.
static BOOL sceneOriginalCode172(const void *code) {
    sceneRetentionReason172 = @"originalImageIdentity";
    Dl_info methodImage = {0}, runtimeImage = {0};
    if (!dladdr(code,&methodImage) || !dladdr((void *)domainGet,&runtimeImage) ||
        methodImage.dli_fbase!=runtimeImage.dli_fbase || !methodImage.dli_fname) return NO;
    struct mach_header_64 header;
    sceneRetentionReason172 = @"originalHeader";
    if (!sceneRead172((uintptr_t)methodImage.dli_fbase,&header,sizeof header) || header.magic!=MH_MAGIC_64 ||
        header.cputype!=CPU_TYPE_ARM64 || header.sizeofcmds>1024*1024 || header.ncmds>1024) return NO;
    NSMutableData *commands = [NSMutableData dataWithLength:header.sizeofcmds];
    if (!sceneRead172((uintptr_t)methodImage.dli_fbase+sizeof header,commands.mutableBytes,commands.length)) return NO;
    const uint8_t *bytes = commands.bytes;
    sceneRetentionReason172 = @"originalTextSection";
    uint64_t textVM = 0, codeVM = 0, fileOffset = UINT64_MAX;
    BOOL hasUUID = NO;
    for (NSUInteger pass=0;pass<2;++pass) {
        NSUInteger offset = 0;
        for (uint32_t i=0;i<header.ncmds;++i) {
            if (offset+sizeof(struct load_command)>commands.length) return NO;
            const struct load_command *command = (const void *)(bytes+offset);
            if (command->cmdsize<sizeof *command || command->cmdsize>commands.length-offset) return NO;
            if (command->cmd==LC_UUID && command->cmdsize>=sizeof(struct uuid_command)) {
                hasUUID = YES;
            }
            if (command->cmd==LC_SEGMENT_64 && command->cmdsize>=sizeof(struct segment_command_64)) {
                const struct segment_command_64 *segment = (const void *)command;
                if (!strncmp(segment->segname,"__TEXT",16)) {
                    if (!pass) textVM = segment->vmaddr;
                    else {
                        if (segment->nsects>(command->cmdsize-sizeof *segment)/sizeof(struct section_64)) return NO;
                        const struct section_64 *sections = (const void *)(segment+1);
                        for (uint32_t j=0;j<segment->nsects;++j) {
                            const struct section_64 *section = &sections[j];
                            if (!strncmp(section->sectname,"__text",16) && codeVM>=section->addr &&
                                codeVM-section->addr<=section->size && section->size-(codeVM-section->addr)>=64)
                                fileOffset = section->offset+codeVM-section->addr;
                        }
                    }
                }
            }
            offset += command->cmdsize;
        }
        codeVM = (uintptr_t)code-(uintptr_t)methodImage.dli_fbase+textVM;
    }
    if (!hasUUID || fileOffset==UINT64_MAX) return NO;
    sceneRetentionReason172 = @"originalDiskHeader";
    int fd = open(methodImage.dli_fname,O_RDONLY);
    if (fd<0) return NO;
    struct mach_header_64 diskHeader;
    BOOL valid = pread(fd,&diskHeader,sizeof diskHeader,0)==sizeof diskHeader &&
        !memcmp(&header,&diskHeader,sizeof header);
    NSMutableData *diskCommands = valid ? [NSMutableData dataWithLength:header.sizeofcmds] : nil;
    valid = valid && pread(fd,diskCommands.mutableBytes,diskCommands.length,sizeof header)==(ssize_t)diskCommands.length &&
        !memcmp(commands.bytes,diskCommands.bytes,commands.length);
    uint8_t original[64], current[64];
    if (valid) sceneRetentionReason172 = @"originalDiskBytes";
    valid = valid && pread(fd,original,sizeof original,(off_t)fileOffset)==sizeof original &&
        sceneRead172((uintptr_t)code,current,sizeof current) && !memcmp(original,current,sizeof current);
    close(fd);
    if (valid) sceneRetentionRVA172 = (uintptr_t)code-(uintptr_t)methodImage.dli_fbase;
    return valid;
}

typedef int (*SceneProfile172)(void *,const void *);
static SceneProfile172 sceneProfileOriginal172;
static _Atomic(uintptr_t) *sceneProfileSlot172;
static void *sceneProfileManagerField172;
static const void *sceneSetMemoryless172;
static atomic_uint sceneRetentionCalls172, sceneRetentionErrors172;

static int sceneRetainedProfile172(void *path, const void *method) {
    int profile = sceneProfileOriginal172(path,method), memoryless = 0;
    void *manager = NULL, *exception = NULL;
    lightGet(path,sceneProfileManagerField172,&manager);
    if (manager) {
        void *args[] = {&profile,&memoryless};
        invoke(sceneSetMemoryless172,manager,args,&exception);
    }
    atomic_fetch_add(&sceneRetentionCalls172,1);
    if (!manager || exception) atomic_fetch_add(&sceneRetentionErrors172,1);
    return profile;
}

static BOOL sceneRetainGBuffer172(void *path, BOOL enabled) {
    if (!enabled) {
        if (!sceneProfileSlot172) return YES;
        sceneRetentionReason172 = @"restoreOwnership";
        uintptr_t expected = (uintptr_t)sceneRetainedProfile172;
        if (!atomic_compare_exchange_strong(sceneProfileSlot172,&expected,(uintptr_t)sceneProfileOriginal172)) return NO;
        sceneProfileSlot172 = NULL;
        sceneRetentionReason172 = @"restored";
        return YES;
    }
    if (sceneProfileSlot172) {
        BOOL owned = atomic_load(sceneProfileSlot172)==(uintptr_t)sceneRetainedProfile172;
        sceneRetentionReason172 = owned ? @"installed" : @"installOwnershipLost";
        return owned;
    }
    sceneRetentionReason172 = @"renderPathIdentity";
    void *cls = path ? objectClass(path) : NULL;
    if (!cls || strcmp(className(cls),"HGRenderPathOnePassDeferred")) return NO;
    const void *method = sceneMethod172(cls,"GetGBufferProfileConfig",0);
    sceneRetentionReason172 = @"profileSignature";
    const void *(*returnType)(const void *) = dlsym(RTLD_DEFAULT,"il2cpp_method_get_return_type");
    if (!method || !returnType || lightCount(method) || (methodFlags(method,NULL)&0x10) ||
        !namedType(returnType(method),"HG.Rendering.Runtime.GBufferProfileManager.GBufferProfileConfig")) return NO;
    const void *code = NULL;
    sceneRetentionReason172 = @"profileCodeRead";
    if (!sceneRead172((uintptr_t)method,&code,sizeof code) || !sceneOriginalCode172(code)) return NO;
    void *manager = sceneMember172(path,"m_gBufferProfileMgr");
    sceneRetentionReason172 = @"managerSignature";
    const void *setter = manager ? sceneMethod172(objectClass(manager),"SetGBufferTextureMemoryless",2) : NULL;
    void *field = sceneField172(cls,"m_gBufferProfileMgr");
    if (!setter || !field || lightCount(setter)!=2 ||
        !namedType(lightArg(setter,0),"HG.Rendering.Runtime.GBufferProfileManager.GBufferProfileConfig") ||
        !namedType(lightArg(setter,1),"UnityEngine.RenderTextureMemoryless")) return NO;
    uintptr_t words[512];
    sceneRetentionReason172 = @"vtableRead";
    if (!sceneRead172((uintptr_t)cls,words,sizeof words)) return NO;
    _Atomic(uintptr_t) *slot = NULL;
    sceneRetentionReason172 = @"vtableUniquePair";
    for (NSUInteger i=0;i+1<sizeof words/sizeof *words;++i) {
        if (words[i]!=(uintptr_t)code || words[i+1]!=(uintptr_t)method) continue;
        if (slot) return NO;
        slot = (_Atomic(uintptr_t) *)((uintptr_t)cls+i*sizeof *words);
    }
    if (!slot) return NO;
    sceneRetentionSlotOffset172 = (uintptr_t)slot-(uintptr_t)cls;
    sceneRetentionReason172 = @"writableRegion";
    vm_address_t address = (vm_address_t)slot; vm_size_t size = 0;
    vm_region_basic_info_data_64_t region; mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t result = vm_region_64(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&region,&count,&object);
    if (object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
    if (result!=KERN_SUCCESS || address>(vm_address_t)slot || !(region.protection&VM_PROT_WRITE) ||
        size<sizeof(uintptr_t)*2 || (vm_address_t)slot-address>size-sizeof(uintptr_t)*2) return NO;
    sceneProfileOriginal172 = (SceneProfile172)code;
    sceneProfileManagerField172 = field; sceneSetMemoryless172 = setter;
    uintptr_t expected = (uintptr_t)code;
    sceneRetentionReason172 = @"installOwnership";
    if (!atomic_compare_exchange_strong(slot,&expected,(uintptr_t)sceneRetainedProfile172)) return NO;
    sceneProfileSlot172 = slot;
    sceneRetentionReason172 = @"installed";
    return YES;
}

static NSNumber *sceneParameter172(void *volume, const char *name, NSNumber *type) {
    return renderNumber(sceneGet172(sceneMember172(volume,name),"get_value"),type);
}

static BOOL sceneCopyParameter172(void *volume, const char *name, uint8_t *destination, NSNumber *type) {
    NSNumber *value = sceneParameter172(volume,name,type);
    if (!value || !isfinite(value.doubleValue)) return NO;
    if (CFGetTypeID((__bridge CFTypeRef)type)==CFBooleanGetTypeID()) *destination = value.boolValue;
    else if (!strcmp(type.objCType,@encode(float))) { float f=value.floatValue; memcpy(destination,&f,4); }
    else { int v=value.intValue; memcpy(destination,&v,4); }
    return YES;
}

static BOOL sceneVolumeActive172(void *volume) {
    void *field = volume ? sceneField172(objectClass(volume),"active") : NULL;
    bool active = false;
    if (field && namedType(lightFieldType(field),"System.Boolean")) lightGet(volume,field,&active);
    return active;
}

static BMFUnityRef172 *sceneControlCamera172;
static BOOL sceneControlRequested172;
// The render callback only copies its inspected request. It never dereferences
// the native camera or changes the engine's GI, fog, or volume settings.
static Native172LightingRequest sceneNativeLatest172;
static NSTimeInterval sceneNativeReceived172;
static BOOL sceneNativeAllowed172; // protected by metal_ao_lock

static void scenePublishNative172(const Native172LightingRequest *request) {
    // Caller holds metal_ao_lock, shared with the actual kernel encode path.
    uintptr_t camera = request->gtaoValid ? request->camera : 0;
    if (official_gtao_request_camera!=camera) ++official_gtao_reset;
    official_gtao_request_camera = camera;
    official_contact_request_valid = camera && request->contactValid;
    if (camera) {
        uint8_t settings[64];
        memcpy(settings,request->gtao,sizeof settings);
        // The encode path owns the temporal frame sequence, not the producer.
        memcpy(settings+20,official_gtao_settings+20,4);
        memcpy(official_gtao_settings,settings,sizeof settings);
        memcpy(official_gtao_output_size,request->outputSize,sizeof request->outputSize);
    }
    if (official_contact_request_valid) {
        memcpy(official_contact_config,request->contact,sizeof request->contact);
        memcpy(official_contact_direction,request->direction,sizeof request->direction);
    }
}

static void sceneNativeRequest172(const Native172LightingRequest *request) {
    @synchronized(metal_ao_lock) {
        sceneNativeLatest172 = *request;
        sceneNativeReceived172 = NSProcessInfo.processInfo.systemUptime;
        if (sceneNativeAllowed172) scenePublishNative172(request);
    }
}

static void sceneClearCamera172(void) {
    @synchronized(metal_ao_lock) {
        if (official_gtao_request_camera || sceneControlCamera172) ++official_gtao_reset;
        official_gtao_request_camera = 0; official_contact_request_valid = false;
    }
    sceneControlCamera172 = nil;
}

static NSDictionary *sceneNativeControl172(void) {
    BOOL restored = sceneRetainGBuffer172(NULL,NO);
    sceneControlCamera172 = nil;
    NSDictionary *native = native172SizeState();
    Native172LightingRequest request;
    NSTimeInterval age;
    BOOL ready;
    @synchronized(metal_ao_lock) {
        request = sceneNativeLatest172;
        age = sceneNativeReceived172 ? NSProcessInfo.processInfo.systemUptime-sceneNativeReceived172 : -1;
        ready = restored && [native[@"installed"] boolValue] && [native[@"active"] boolValue] &&
            age>=0 && age<1 && request.camera && request.gtaoValid;
        // Keep accepting the next native frame while waiting for its first
        // request; readiness must not prevent the native renderer from starting.
        sceneNativeAllowed172 = restored;
        if (ready) scenePublishNative172(&request);
        else {
            if (official_gtao_request_camera) ++official_gtao_reset;
            official_gtao_request_camera = 0; official_contact_request_valid = false;
        }
    }
    int quality = 0;
    float intensity = 0;
    memcpy(&quality,request.gtao+12,sizeof quality);
    memcpy(&intensity,request.contact+4,sizeof intensity);
    return @{@"status":!restored ? @"restoreFailed" : ready ? @"ready" : @"waitingForNativeRequest",
        @"path":@"native",@"ready":@(ready),
        @"retentionReady":@(restored && [native[@"installed"] boolValue]),@"native":native,
        @"camera":ready ? [NSString stringWithFormat:@"0x%lx",request.camera] : @"",
        @"sceneSize":@[@(request.sceneSize[0]),@(request.sceneSize[1])],
        @"taauSize":@[@(request.taauSize[0]),@(request.taauSize[1])],
        @"outputSize":@[@(request.outputSize[0]),@(request.outputSize[1])],
        @"requestSerial":@(request.serial),@"requestAge":@(age),
        @"retentionStage":@"nativeMetalCapture",
        @"gtao":@{@"valid":@(ready),@"enabled":@(ready && request.gtao[0]),@"quality":@(quality)},
        @"contact":@{@"valid":@(ready && request.contactValid),@"intensity":@(intensity),
                      @"direction":@[@(request.direction[0]),@(request.direction[1]),@(request.direction[2])]}};
}

static NSDictionary *updateSceneControl172(BOOL requested) {
    native172SetLightingSink(sceneNativeRequest172);
    static NSTimeInterval next;
    static NSDictionary *last;
    NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    if (requested==sceneControlRequested172 && now<next && last) return last;
    sceneControlRequested172 = requested; next = now+0.25;
    if (!requested) {
        @synchronized(metal_ao_lock) { sceneNativeAllowed172 = NO; }
        BOOL restored = sceneRetainGBuffer172(NULL,NO);
        sceneClearCamera172();
        return last = @{@"status":restored ? @"off" : @"restoreFailed",@"ready":@NO};
    }
    if (!lightingAPIs()) {
        @synchronized(metal_ao_lock) { sceneNativeAllowed172 = NO; }
        sceneClearCamera172();
        return last = @{@"status":@"waitingForRuntime",@"ready":@NO};
    }
    bool *cpp = fxCppFlag172();
    if (cpp && *cpp) return last = sceneNativeControl172();
    @synchronized(metal_ao_lock) { sceneNativeAllowed172 = NO; }
    if (!cpp) {
        sceneClearCamera172();
        return last = @{@"status":@"waitingForRenderPath",@"ready":@NO};
    }
    void *hg = lightingCamera172(), *path = sceneGet172(hg,"get_renderPathInstance");
    if (!hg || !path) {
        sceneClearCamera172();
        return last = @{@"status":@"waitingForCamera",@"ready":@NO};
    }
    if (hg!=[sceneControlCamera172 target]) {
        sceneControlCamera172 = [BMFUnityRef172 hold:hg];
        @synchronized(metal_ao_lock) { ++official_gtao_reset; official_gtao_request_camera=0; official_contact_request_valid=false; }
    }
    NSArray *sceneSize = fxSize172(hg,"get_sceneRTSize"), *outputSize = fxSize172(hg,"get_finalRTSize");
    void *volumes = sceneGet172(hg,"get_volumeComponentsData");
    void *ao = sceneMember172(volumes,"m_GTAmbientOcclusion");
    uint8_t settings[64] = {0};
    BOOL aoValid = ao && sceneSize.count==2 && outputSize.count==2;
    const char *flags[] = {"enable","enableFP32Depths","enableBentNormals","generateNormalsInplace","custom"};
    const NSUInteger flagOffsets[] = {0,1,2,3,32};
    for (NSUInteger i=0;i<5;++i) aoValid = sceneCopyParameter172(ao,flags[i],settings+flagOffsets[i],@YES) && aoValid;
    aoValid = sceneCopyParameter172(ao,"qualityLevel",settings+12,@0) && aoValid;
    aoValid = sceneCopyParameter172(ao,"denoisePasses",settings+16,@0) && aoValid;
    const char *floats[] = {"finalValuePower","depthMIPSamplingOffset","radius","radiusMultiplier","falloffRange",
                           "sampleDistributionPower","thinOccluderCompensation","mvFactor","depthFactor"};
    const NSUInteger offsets[] = {24,28,36,40,44,48,52,56,60};
    for (NSUInteger i=0;i<9;++i) aoValid = sceneCopyParameter172(ao,floats[i],settings+offsets[i],@1.f) && aoValid;
    int dimensions[2] = {0}, outputDimensions[2] = {0};
    if (aoValid) {
        for (NSUInteger i=0;i<2;++i) { dimensions[i]=[sceneSize[i] intValue]; outputDimensions[i]=[outputSize[i] intValue]; }
        memcpy(settings+4,dimensions,sizeof dimensions);
    }
    void *stack = sceneGet172(hg,"get_volumeStack"), *contact = NULL;
    static void *contactClass;
    if (!contactClass) contactClass = classNamed("HG.RenderPipelines.Runtime.dll","HG.Rendering.Runtime","ContactShadowVolume");
    const void *component = stack ? sceneMethod172(objectClass(stack),"GetComponent",1) : NULL;
    if (component && contactClass && namedType(lightArg(component,0),"System.Type")) {
        void *args[] = {lightTypeObject(lightType(contactClass))}, *exception = NULL;
        contact = invoke(component,stack,args,&exception);
        if (exception) contact = NULL;
    }
    uint8_t contactSettings[20] = {0};
    BOOL contactValid = sceneVolumeActive172(contact) && sceneCopyParameter172(contact,"ignoreEdgePixels",contactSettings,@YES);
    const char *contactFloats[] = {"shadowIntensity","surfaceThickness","bilinearThreshold"};
    for (NSUInteger i=0;i<3;++i) contactValid = sceneCopyParameter172(contact,contactFloats[i],contactSettings+4+i*4,@1.f) && contactValid;
    contactValid = sceneCopyParameter172(contact,"shadowContrast",contactSettings+16,@0) && contactValid;
    float direction[4] = {0};
    void *selector = sceneMember172(path,"m_lightClusteringPassConstructor");
    const void *directionMethod = selector ? sceneMethod172(objectClass(selector),"TryGetDirectionalLightDir",2) : NULL;
    if (contactValid && directionMethod && namedType(lightArg(directionMethod,0),"UnityEngine.Vector4&") &&
        namedType(lightArg(directionMethod,1),"HG.Rendering.Runtime.HGCamera")) {
        void *args[] = {direction,hg}, *exception = NULL;
        invoke(directionMethod,selector,args,&exception);
        float length = direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
        contactValid = !exception && isfinite(length) && length>0.5f && length<1.5f;
    } else contactValid = NO;
    BOOL retained = aoValid && sceneRetainGBuffer172(path,YES);
    @synchronized(metal_ao_lock) {
        official_contact_request_valid = retained && contactValid;
        if (retained) {
            // The encode path advances this sequence for every actual frame.
            memcpy(settings+20,official_gtao_settings+20,4);
            memcpy(official_gtao_settings,settings,sizeof settings);
            memcpy(official_gtao_output_size,outputDimensions,sizeof outputDimensions);
            official_gtao_request_camera = (uintptr_t)hg;
        } else official_gtao_request_camera = 0;
        if (official_contact_request_valid) {
            memcpy(official_contact_config,contactSettings,sizeof contactSettings);
            memcpy(official_contact_direction,direction,sizeof direction);
        }
    }
    return last = @{@"status":!aoValid ? @"waitingForVolumes" : retained ? @"ready" : @"retentionUnavailable",
        @"path":@"managed",@"ready":@(retained),@"camera":[NSString stringWithFormat:@"%p",hg],@"sceneSize":sceneSize ?: @[],@"outputSize":outputSize ?: @[],
        @"retentionStage":sceneRetentionReason172,@"retentionRVA":[NSString stringWithFormat:@"0x%lx",sceneRetentionRVA172],
        @"retentionSlotOffset":[NSString stringWithFormat:@"0x%lx",sceneRetentionSlotOffset172],
        @"retentionCalls":@(atomic_load(&sceneRetentionCalls172)),@"retentionErrors":@(atomic_load(&sceneRetentionErrors172)),
        @"gtao":@{@"valid":@(aoValid),@"enabled":@(settings[0] && sceneVolumeActive172(ao)),
                  @"quality":sceneParameter172(ao,"qualityLevel",@0) ?: NSNull.null},
        @"contact":@{@"valid":@(contactValid),@"intensity":sceneParameter172(contact,"shadowIntensity",@1.f) ?: NSNull.null,
                      @"direction":@[@(direction[0]),@(direction[1]),@(direction[2])]}};
}
