// Native 1.7.2 request bridge. Include after lighting172.inc, before MetalFX.
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <fcntl.h>

typedef struct {
    uintptr_t camera;
    int sceneSize[2], taauSize[2], outputSize[2];
    uint8_t gtao[64], contact[20];
    float direction[4];
    BOOL gtaoValid, contactValid;
    uint64_t serial;
} Native172LightingRequest;
typedef void (*Native172LightingSink)(const Native172LightingRequest *);
typedef void (*Native172Render)(void *,void *,void *,void *,void *,void *);

static _Atomic(Native172LightingSink) native172LightingSink;
static atomic_bool native172Enabled, native172Temporal, native172Installed;
static atomic_uintptr_t native172Owner;
static atomic_ulong native172Requests, native172Corrections;
static Native172Render native172OriginalRender;
static uint32_t native172CameraHandle, native172SelectorHandle;
static NSString *native172Reason = @"notRequested";
static NSDictionary *native172Inspection;
static Native172LightingRequest native172LastRequest;
static NSTimeInterval native172LastRequestTime;

static NSObject *native172Lock(void) {
    static NSObject *lock; static dispatch_once_t once;
    dispatch_once(&once,^{ lock=[NSObject new]; }); return lock;
}
static BOOL native172Read(uintptr_t address, void *out, size_t length) {
    mach_vm_size_t count=0;
    return address && mach_vm_read_overwrite(mach_task_self(),address,length,(mach_vm_address_t)out,&count)==KERN_SUCCESS && count==length;
}
static void native172SetLightingSink(Native172LightingSink sink) {
    atomic_store_explicit(&native172LightingSink,sink,memory_order_release);
}

static size_t (*native172FieldOffset)(void *);
static int32_t (*native172ValueSize)(void *,uint32_t *);
static uint32_t (*native172InstanceSize)(void *);
static bool (*native172IsValue)(void *);
static void *(*native172TypeClass)(const void *);
static const void *(*native172ReturnType)(const void *);
static size_t native172Header;

typedef struct { size_t source; uint8_t destination, bytes; } Native172FieldCopy;
static struct {
    size_t requestBytes, aoBytes, contactBytes;
    size_t enableTAAU, scene, taau, final, environment, ao, visible, visibleCount, contact;
    Native172FieldCopy aoFields[20], contactFields[6];
    size_t aoFieldCount, contactFieldCount;
} native172Layout;
static const void *native172SelectLight, *native172LightDirection;
static void *native172VisibleField, *native172LightIndexField;

static BOOL native172Offset(void *klass,const char *name,const char *type,size_t *offset) {
    void *field=klass ? lightField(klass,name) : NULL;
    if (!field || !namedType(lightFieldType(field),type)) {
        native172Reason=[NSString stringWithFormat:@"fieldType:%s.%s",klass ? className(klass) : "?",name]; return NO;
    }
    size_t value=native172FieldOffset(field);
    if (value<native172Header || value-native172Header>=8192) {
        native172Reason=[NSString stringWithFormat:@"fieldOffset:%s",name]; return NO;
    }
    *offset=value-native172Header; return YES;
}
static BOOL native172CopyMap(void *klass,const char *name,const char *type,size_t bytes,size_t destination,
                             Native172FieldCopy *map,size_t size) {
    size_t offset=0;
    if (!native172Offset(klass,name,type,&offset) || offset+bytes>size) return NO;
    *map=(Native172FieldCopy){offset,(uint8_t)destination,(uint8_t)bytes}; return YES;
}
static BOOL native172BindLayout(void) {
    static BOOL ready;
    if (ready) return YES;
    if (!lightingAPIs()) { native172Reason=@"runtimeAPIs"; return NO; }
#define NATIVE172_API(variable,name) if (!(variable=dlsym(RTLD_DEFAULT,"il2cpp_" #name))) { native172Reason=@"metadataAPI:" #name; return NO; }
    NATIVE172_API(native172FieldOffset,field_get_offset)
    NATIVE172_API(native172ValueSize,class_value_size)
    NATIVE172_API(native172InstanceSize,class_instance_size)
    NATIVE172_API(native172IsValue,class_is_valuetype)
    NATIVE172_API(native172TypeClass,class_from_il2cpp_type)
    NATIVE172_API(native172ReturnType,method_get_return_type)
#undef NATIVE172_API
    void *integer=lightingClass("System","Int32");
    int32_t intBytes=integer ? native172ValueSize(integer,NULL) : 0;
    uint32_t boxed=integer ? native172InstanceSize(integer) : 0;
    if (intBytes!=4 || boxed<intBytes || boxed-intBytes!=sizeof(uintptr_t)*2) { native172Reason=@"boxedHeader"; return NO; }
    native172Header=boxed-intBytes;
    void *request=lightingClass("UnityEngine.HyperGryphEngineCode","HGRenderPathParamsCPP");
    void *phase=lightingClass("UnityEngine.HyperGryphEngineCode","HGEnvironmentPhaseCPP");
    void *ao=lightingClass("UnityEngine.HyperGryphEngineCode","HGGTAmbientOcclusionSettingParameters");
    void *contact=lightingClass("UnityEngine.HyperGryphEngineCode","HGContactShadowConfigCPP");
    if (!request || !phase || !ao || !contact || !native172IsValue(request) || !native172IsValue(phase) ||
        !native172IsValue(ao) || !native172IsValue(contact)) { native172Reason=@"nativeValueTypes"; return NO; }
    int32_t requestBytes=native172ValueSize(request,NULL), aoBytes=native172ValueSize(ao,NULL), contactBytes=native172ValueSize(contact,NULL);
    if (requestBytes<64 || requestBytes>8192 || aoBytes<64 || aoBytes>256 || contactBytes<20 || contactBytes>256) {
        native172Reason=@"nativeValueSizes"; return NO;
    }
    native172Layout.requestBytes=requestBytes; native172Layout.aoBytes=aoBytes; native172Layout.contactBytes=contactBytes;
#define NATIVE172_FIELD(cls,name,type,dest) if (!native172Offset(cls,name,type,&native172Layout.dest)) return NO
    NATIVE172_FIELD(request,"enableTAAU","System.Boolean",enableTAAU);
    NATIVE172_FIELD(request,"sceneRTSize","UnityEngine.Vector2Int",scene);
    NATIVE172_FIELD(request,"taauRTSize","UnityEngine.Vector2Int",taau);
    NATIVE172_FIELD(request,"finalRTSize","UnityEngine.Vector2Int",final);
    NATIVE172_FIELD(request,"envPhase","UnityEngine.HyperGryphEngineCode.HGEnvironmentPhaseCPP*",environment);
    NATIVE172_FIELD(request,"gtaoSettingParameters","UnityEngine.HyperGryphEngineCode.HGGTAmbientOcclusionSettingParameters*",ao);
    NATIVE172_FIELD(request,"visibleLights","System.IntPtr",visible);
    NATIVE172_FIELD(request,"visibleLightCount","System.Int32",visibleCount);
    NATIVE172_FIELD(phase,"contactShadowConfig","UnityEngine.HyperGryphEngineCode.HGContactShadowConfigCPP*",contact);
#undef NATIVE172_FIELD
    const char *aoBools[]={"enableAO","enableFP32Depths","enableBentNormals","generateNormalsInplace","useCustomSettings"};
    const uint8_t aoBoolOffsets[]={0,1,2,3,32};
    const char *aoInts[]={"screenWidth","screenHeight","qualityLevel","denoisePasses","cameraFrameCount"};
    const char *aoFloats[]={"finalValuePower","depthMIPSamplingOffset","radius","radiusMultiplier","falloffRange",
                           "sampleDistributionPower","thinOccluderCompensation","mvFactor","depthFactor"};
    const uint8_t aoFloatOffsets[]={24,28,36,40,44,48,52,56,60};
    size_t count=0;
    for (size_t i=0;i<5;++i) if (!native172CopyMap(ao,aoBools[i],"System.Boolean",1,aoBoolOffsets[i],&native172Layout.aoFields[count++],aoBytes)) return NO;
    for (size_t i=0;i<5;++i) if (!native172CopyMap(ao,aoInts[i],"System.Int32",4,4+i*4,&native172Layout.aoFields[count++],aoBytes)) return NO;
    for (size_t i=0;i<9;++i) if (!native172CopyMap(ao,aoFloats[i],"System.Single",4,aoFloatOffsets[i],&native172Layout.aoFields[count++],aoBytes)) return NO;
    native172Layout.aoFieldCount=count;
    const char *contactNames[]={"ignoreEdgePixels","disableTerrainContactShadow","intensity","surfaceThickness","bilinearThreshold","contract"};
    const char *contactTypes[]={"System.Boolean","System.Boolean","System.Single","System.Single","System.Single","System.Int32"};
    const uint8_t contactOffsets[]={0,1,4,8,12,16};
    for (size_t i=0;i<6;++i) if (!native172CopyMap(contact,contactNames[i],contactTypes[i],i<2 ? 1 : 4,
                                                contactOffsets[i],&native172Layout.contactFields[i],contactBytes)) return NO;
    native172Layout.contactFieldCount=6;
    const size_t requestOffsets[]={native172Layout.enableTAAU,native172Layout.scene,native172Layout.taau,native172Layout.final,
                                  native172Layout.environment,native172Layout.ao,native172Layout.visible,native172Layout.visibleCount};
    for (size_t i=0;i<8;++i) if (requestOffsets[i]+(i==0 ? 1 : i==7 ? 4 : 8)>native172Layout.requestBytes) {
        native172Reason=@"requestFieldBounds"; return NO;
    }
    ready=YES; native172Reason=@"layoutVerified"; return YES;
}

// The running code and disk image must have the same UUID, load commands and
// executable bytes. The callback ABI is additionally pinned below.
static BOOL native172OriginalCode(const void *code,uintptr_t *rva) {
    Dl_info image={0},runtime={0}; struct mach_header_64 header;
    if (!dladdr(code,&image) || !dladdr((void *)domainGet,&runtime) || image.dli_fbase!=runtime.dli_fbase || !image.dli_fname ||
        !native172Read((uintptr_t)image.dli_fbase,&header,sizeof header) || header.magic!=MH_MAGIC_64 ||
        header.cputype!=CPU_TYPE_ARM64 || header.sizeofcmds>1024*1024 || header.ncmds>1024) return NO;
    NSMutableData *commands=[NSMutableData dataWithLength:header.sizeofcmds];
    if (!native172Read((uintptr_t)image.dli_fbase+sizeof header,commands.mutableBytes,commands.length)) return NO;
    uint64_t textVM=0,fileOffset=UINT64_MAX; BOOL uuid=NO;
    for (NSUInteger pass=0;pass<2;++pass) {
        NSUInteger offset=0;
        for (uint32_t i=0;i<header.ncmds;++i) {
            if (offset+sizeof(struct load_command)>commands.length) return NO;
            const struct load_command *command=(const void *)((const uint8_t *)commands.bytes+offset);
            if (command->cmdsize<sizeof *command || command->cmdsize>commands.length-offset) return NO;
            if (command->cmd==LC_UUID && command->cmdsize==sizeof(struct uuid_command)) {
                uuid=gameUUIDBytesMatch172(((const struct uuid_command *)command)->uuid);
            }
            if (command->cmd==LC_SEGMENT_64 && command->cmdsize>=sizeof(struct segment_command_64)) {
                const struct segment_command_64 *segment=(const void *)command;
                if (!strncmp(segment->segname,"__TEXT",16)) {
                    if (!pass) textVM=segment->vmaddr;
                    else {
                        if (segment->nsects>(command->cmdsize-sizeof *segment)/sizeof(struct section_64)) return NO;
                        const struct section_64 *sections=(const void *)(segment+1);
                        uint64_t address=(uintptr_t)code-(uintptr_t)image.dli_fbase+textVM;
                        for (uint32_t j=0;j<segment->nsects;++j)
                            if (!strncmp(sections[j].sectname,"__text",16) && address>=sections[j].addr &&
                                address-sections[j].addr<=sections[j].size && sections[j].size-(address-sections[j].addr)>=64)
                                fileOffset=sections[j].offset+address-sections[j].addr;
                    }
                }
            }
            offset+=command->cmdsize;
        }
    }
    if (!uuid || fileOffset==UINT64_MAX) return NO;
    int fd=open(image.dli_fname,O_RDONLY); if (fd<0) return NO;
    struct mach_header_64 diskHeader; uint8_t memory[64],disk[64];
    NSMutableData *diskCommands=[NSMutableData dataWithLength:commands.length];
    BOOL ok=pread(fd,&diskHeader,sizeof diskHeader,0)==sizeof diskHeader && !memcmp(&header,&diskHeader,sizeof header) &&
        pread(fd,diskCommands.mutableBytes,diskCommands.length,sizeof header)==diskCommands.length &&
        !memcmp(commands.bytes,diskCommands.bytes,commands.length) &&
        pread(fd,disk,sizeof disk,(off_t)fileOffset)==sizeof disk && native172Read((uintptr_t)code,memory,sizeof memory) &&
        !memcmp(memory,disk,sizeof disk);
    close(fd); if (ok && rva) *rva=(uintptr_t)code-(uintptr_t)image.dli_fbase; return ok;
}

static BOOL native172BindDirection(void) {
    if (native172SelectorHandle) return YES;
    void *klass=lightingClass("HG.Rendering.Runtime","LightClusteringPassConstructor");
    native172SelectLight=klass ? lookupMethod(klass,"GetDirectionalLightIndex",1) : NULL;
    native172LightDirection=klass ? lookupMethod(klass,"TryGetDirectionalLightDir",2) : NULL;
    native172VisibleField=klass ? lightField(klass,"m_visibleLights") : NULL;
    native172LightIndexField=klass ? lightField(klass,"m_directionalLightIndex") : NULL;
    if (!native172SelectLight || !native172LightDirection || !native172VisibleField || !native172LightIndexField ||
        !(methodFlags(native172SelectLight,NULL)&0x10) || (methodFlags(native172LightDirection,NULL)&0x10) ||
        !namedType(native172ReturnType(native172SelectLight),"System.Int32") ||
        !namedType(lightArg(native172LightDirection,0),"UnityEngine.Vector4&") ||
        !namedType(lightArg(native172LightDirection,1),"HG.Rendering.Runtime.HGCamera") ||
        !namedType(lightFieldType(native172LightIndexField),"System.Int32") ||
        !native172OriginalCode(*(const void **)native172SelectLight,NULL) ||
        !native172OriginalCode(*(const void **)native172LightDirection,NULL)) return NO;
    void *arrayClass=native172TypeClass(lightArg(native172SelectLight,0));
    size_t buffer=0,length=0,allocator=0;
    if (!arrayClass || !native172IsValue(arrayClass) || native172ValueSize(arrayClass,NULL)!=16 ||
        !native172Offset(arrayClass,"m_Buffer","System.Void*",&buffer) || buffer!=0 ||
        !native172Offset(arrayClass,"m_Length","System.Int32",&length) || length!=8 ||
        !native172Offset(arrayClass,"m_AllocatorLabel","Unity.Collections.Allocator",&allocator) || allocator!=12) return NO;
    void *selector=lightNew(klass);
    native172SelectorHandle=selector ? lightPin(selector,false) : 0;
    return native172SelectorHandle!=0;
}
static BOOL native172Direction(const uint8_t *request,float direction[4]) {
    if (!NSThread.isMainThread || !threadCurrent || !threadCurrent() || !native172SelectorHandle) return NO;
    void *selector=lightTarget(native172SelectorHandle), *hg=native172CameraHandle ? lightTarget(native172CameraHandle) : NULL;
    if (!selector || !hg) return NO;
    struct { uintptr_t buffer; int length,allocator; } visible={0};
    memcpy(&visible.buffer,request+native172Layout.visible,8); memcpy(&visible.length,request+native172Layout.visibleCount,4);
    if (!visible.buffer || visible.length<1 || visible.length>65536) return NO;
    void *exception=NULL,*args[]={&visible};
    void *value=invoke(native172SelectLight,NULL,args,&exception);
    if (exception || !value) return NO;
    int index=*(int *)unbox(value); if (index<0 || index>=visible.length) return NO;
    lightSet(selector,native172VisibleField,&visible); lightSet(selector,native172LightIndexField,&index);
    void *directionArgs[]={direction,hg}; invoke(native172LightDirection,selector,directionArgs,&exception);
    memset(&visible,0,sizeof visible); lightSet(selector,native172VisibleField,&visible);
    if (exception) return NO;
    float length=0; for (int i=0;i<3;++i) { if (!isfinite(direction[i])) return NO; length+=direction[i]*direction[i]; }
    return length>.5f && length<1.5f;
}
static BOOL native172Dimensions(const int size[2]) { return size[0]>0 && size[1]>0 && size[0]<=7680 && size[1]<=4320; }

static void native172RenderBridge(void *owner,void *parameters,void *culling,void *camera,void *context,void *command) {
    Native172Render original=native172OriginalRender;
    if (!atomic_load(&native172Enabled) || (uintptr_t)owner!=atomic_load(&native172Owner)) {
        original(owner,parameters,culling,camera,context,command); return;
    }
    uint8_t request[8192];
    if (!native172Read((uintptr_t)parameters,request,native172Layout.requestBytes)) {
        original(owner,parameters,culling,camera,context,command); return;
    }
    Native172LightingRequest out={0}; out.camera=(uintptr_t)camera;
    memcpy(out.sceneSize,request+native172Layout.scene,8); memcpy(out.taauSize,request+native172Layout.taau,8);
    memcpy(out.outputSize,request+native172Layout.final,8);
    BOOL dimensions=native172Dimensions(out.sceneSize) && native172Dimensions(out.taauSize) && native172Dimensions(out.outputSize);
    BOOL resize=dimensions && atomic_load(&native172Temporal) && request[native172Layout.enableTAAU] &&
        out.outputSize[0]>=out.sceneSize[0] && out.outputSize[1]>=out.sceneSize[1];
    if (resize) {
        if (memcmp(out.taauSize,out.outputSize,8)) atomic_fetch_add(&native172Corrections,1);
        memcpy((uint8_t *)parameters+native172Layout.taau,out.outputSize,8); memcpy(out.taauSize,out.outputSize,8);
    }
    uintptr_t pointer=0; uint8_t source[256];
    memcpy(&pointer,request+native172Layout.ao,8);
    if (dimensions && native172Read(pointer,source,native172Layout.aoBytes)) {
        for (size_t i=0;i<native172Layout.aoFieldCount;++i) { Native172FieldCopy f=native172Layout.aoFields[i]; memcpy(out.gtao+f.destination,source+f.source,f.bytes); }
        out.gtaoValid=YES;
    }
    uintptr_t phase=0; memcpy(&phase,request+native172Layout.environment,8);
    if (dimensions && phase && native172Read(phase+native172Layout.contact,&pointer,8) && native172Read(pointer,source,native172Layout.contactBytes)) {
        for (size_t i=0;i<native172Layout.contactFieldCount;++i) { Native172FieldCopy f=native172Layout.contactFields[i]; memcpy(out.contact+f.destination,source+f.source,f.bytes); }
        out.contactValid=native172Direction(request,out.direction);
    }
    out.serial=atomic_fetch_add(&native172Requests,1)+1;
    @synchronized(native172Lock()) { native172LastRequest=out; native172LastRequestTime=NSProcessInfo.processInfo.systemUptime; }
    Native172LightingSink sink=atomic_load_explicit(&native172LightingSink,memory_order_acquire);
    @try { if (sink) sink(&out); original(owner,parameters,culling,camera,context,command); }
    @finally { if (resize) memcpy((uint8_t *)parameters+native172Layout.taau,request+native172Layout.taau,8); }
}

static NSDictionary *native172InspectSize(void *hg) {
    if (!lightingAPIs() || !hg) return @{@"reason":@"cameraUnavailable"};
    void *field=lightField(objectClass(hg),"renderPathInstanceCPP"); uintptr_t path=0,wrapper[4]={0};
    if (!field || !namedType(lightFieldType(field),"System.Int64")) return @{@"reason":@"cameraNativeField"};
    lightGet(hg,field,&path);
    if (!native172Read(path,wrapper,sizeof wrapper)) return @{@"reason":@"wrapperUnavailable"};
    NSMutableArray *entries=[NSMutableArray array];
    for (int i=1;i<4;++i) {
        uintptr_t rva=0; uint8_t bytes[64];
        const void *code=(const void *)wrapper[i]; if (i==2 && code==(const void *)native172RenderBridge) code=(const void *)native172OriginalRender;
        BOOL valid=native172OriginalCode(code,&rva);
        NSMutableString *hex=[NSMutableString string];
        if (native172Read((uintptr_t)code,bytes,sizeof bytes)) for (size_t j=0;j<sizeof bytes;++j) [hex appendFormat:@"%02x",bytes[j]];
        [entries addObject:@{@"rva":[NSString stringWithFormat:@"0x%lx",rva],@"diskMatches":@(valid),@"bytes":hex}];
    }
    return native172Inspection=@{@"path":[NSString stringWithFormat:@"0x%lx",path],@"owner":[NSString stringWithFormat:@"0x%lx",wrapper[0]],@"callbacks":entries};
}

static BOOL native172InstallSize(void *hg) {
    atomic_store(&native172Installed,false);
    if (!NSThread.isMainThread || !threadCurrent || !threadCurrent()) { native172Reason=@"mainRuntimeThreadRequired"; return NO; }
    if (!native172BindLayout()) return NO;
    native172InspectSize(hg);
    void *field=hg ? lightField(objectClass(hg),"renderPathInstanceCPP") : NULL;
    uintptr_t path=0,wrapper[4]={0};
    if (!field || !namedType(lightFieldType(field),"System.Int64")) { native172Reason=@"cameraNativeField"; return NO; }
    lightGet(hg,field,&path);
    if (!native172Read(path,wrapper,sizeof wrapper) || !wrapper[0]) { native172Reason=@"wrapperUnavailable"; return NO; }
    if (wrapper[2]!=(uintptr_t)native172RenderBridge) {
        // Current 1.7.2 runtime wrapper + disk disassembly: x0=owner, x1=request,
        // x2=culling, x3=camera, x4=context, x5=command; request+0xa8 is envPhase.
        NSArray<NSNumber *> *expectedRVAs=gameProfile172[@"renderCallbacks"];
        const uint8_t expectedBytes[3][16]={
            {0xff,0x03,0x03,0xd1,0xf8,0x5f,0x08,0xa9,0xf6,0x57,0x09,0xa9,0xf4,0x4f,0x0a,0xa9},
            {0xfc,0x6f,0xba,0xa9,0xfa,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,0xf6,0x57,0x03,0xa9},
            {0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9,0xfd,0x43,0x00,0x91,0xf3,0x03,0x00,0xaa}};
        for (size_t i=0;i<3;++i) {
            uintptr_t rva=0; uint8_t bytes[16];
            if (!native172OriginalCode((const void *)wrapper[i+1],&rva) || rva!=expectedRVAs[i].unsignedLongLongValue ||
                !native172Read(wrapper[i+1],bytes,sizeof bytes) || memcmp(bytes,expectedBytes[i],sizeof bytes)) {
                native172Reason=[NSString stringWithFormat:@"callbackIdentity:%zu",i]; return NO;
            }
        }
        vm_address_t address=path+16; vm_size_t size=0; vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64; mach_port_t object=MACH_PORT_NULL;
        if (vm_region_64(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object)!=KERN_SUCCESS ||
            address>path+16 || size<path+24-address || !(info.protection&VM_PROT_WRITE)) { native172Reason=@"wrapperNotWritable"; return NO; }
        if (native172OriginalRender && native172OriginalRender!=(Native172Render)wrapper[2]) { native172Reason=@"renderCallbackChanged"; return NO; }
        native172OriginalRender=(Native172Render)wrapper[2];
        uintptr_t expected=wrapper[2];
        if (!__atomic_compare_exchange_n((uintptr_t *)(path+16),&expected,(uintptr_t)native172RenderBridge,false,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)) {
            native172Reason=@"wrapperChanged"; return NO;
        }
    }
    if (!native172CameraHandle || lightTarget(native172CameraHandle)!=hg) {
        if (native172CameraHandle) lightUnpin(native172CameraHandle);
        native172CameraHandle=lightPin(hg,false);
    }
    native172BindDirection();
    if (atomic_exchange(&native172Owner,wrapper[0])!=wrapper[0]) {
        @synchronized(native172Lock()) { native172LastRequestTime=0; }
    }
    atomic_store(&native172Installed,true);
    native172Reason=@"installed"; return YES;
}
static void native172ConfigureSize(BOOL enabled,BOOL temporal) {
    atomic_store(&native172Temporal,temporal); atomic_store(&native172Enabled,enabled);
}
static NSDictionary *native172SizeState(void) {
    Native172LightingRequest request; NSTimeInterval age;
    @synchronized(native172Lock()) { request=native172LastRequest; age=native172LastRequestTime ? NSProcessInfo.processInfo.systemUptime-native172LastRequestTime : -1; }
    BOOL installed=atomic_load(&native172Installed), active=installed && atomic_load(&native172Enabled) && age>=0 && age<1;
    return @{@"installed":@(installed),@"active":@(active),@"reason":native172Reason ?: @"",
             @"inspection":native172Inspection ?: @{},@"requests":@(atomic_load(&native172Requests)),@"corrections":@(atomic_load(&native172Corrections)),
             @"lastRequestAge":@(age),@"sceneSize":@[@(request.sceneSize[0]),@(request.sceneSize[1])],
             @"taauSize":@[@(request.taauSize[0]),@(request.taauSize[1])],@"finalSize":@[@(request.outputSize[0]),@(request.outputSize[1])],
             @"outputSizeVerified":@(active && native172Dimensions(request.outputSize) && !memcmp(request.taauSize,request.outputSize,8)),
             @"gtaoValid":@(request.gtaoValid),@"contactValid":@(request.contactValid)};
}
