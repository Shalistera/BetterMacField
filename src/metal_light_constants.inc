/* Snapshot actual directional-light constants only during explicit Metal tracing. */
static char metal_light_buffers_key;

static void metal_light_buffer(id encoder,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger index) {
    if(index<16)official_gtao_buffer(encoder,buffer,offset,index);
    native_temporal_buffer(encoder,buffer,offset,index);
    metal_ssr_camera_buffer(encoder,buffer,offset,index);
    if(!metal_tracing || (index>1 && (index<3 || index>6)))return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&metal_light_buffers_key);
    if(!bindings){bindings=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&metal_light_buffers_key,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(buffer)bindings[@(index)]=@[buffer,@(offset)];else [bindings removeObjectForKey:@(index)];
}

static void metal_light_bytes(id encoder,const void *bytes,NSUInteger length,NSUInteger index) {
    if(index<16)official_gtao_bytes(encoder,bytes,length,index);
    native_temporal_bytes(encoder,bytes,length,index);
    metal_ssr_camera_bytes(encoder,bytes,length,index);
    if(!metal_tracing || (index>1 && (index<3 || index>6)))return;
    NSMutableDictionary *bindings=objc_getAssociatedObject(encoder,&metal_light_buffers_key);
    if(!bindings){bindings=[NSMutableDictionary dictionary];objc_setAssociatedObject(encoder,&metal_light_buffers_key,bindings,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
    if(bytes)bindings[@(index)]=[NSData dataWithBytes:bytes length:MIN(length,index==5 ? 8880 : index==1 ? 1984 : index==0 ? 320 : 32)];else [bindings removeObjectForKey:@(index)];
}

static void metal_light_constants_draw(id encoder,NSString *description) {
    if(metal_tracing && [description containsString:@"d6af24c94a2dd3ab6c038fae09d80f379a09de5eecabfdfe25922484bbbe9c87"]) {
        // Current native TAAU Performance: global jitter at288/304, twelve
        // authored vectors in buffer1. Inspect before adapting this pass.
        for(NSNumber *index in @[@0,@1]) {
            id entry=objc_getAssociatedObject(encoder,&metal_light_buffers_key)[index];
            const void *bytes=NULL;NSUInteger length=0;
            if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
            else if([entry isKindOfClass:NSArray.class]) {
                id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
                if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){bytes=(const char *)buffer.contents+offset;length=buffer.length-offset;}
            }
            NSUInteger start=index.intValue==0 ? 288 : 0,end=index.intValue==0 ? 320 : 192;
            NSMutableArray *values=[NSMutableArray array];
            for(NSUInteger offset=start;bytes && offset+16<=MIN(length,end);offset+=16) {
                float v[4];memcpy(v,(const char *)bytes+offset,sizeof v);
                [values addObject:[NSString stringWithFormat:@"%lu=[%g,%g,%g,%g]",(unsigned long)offset,v[0],v[1],v[2],v[3]]];
            }
            record_metal([NSString stringWithFormat:@"native-taau:buffer%@ %@",index,[values componentsJoinedByString:@" "]]);
        }
    }
    if(metal_tracing && ([description containsString:@"962c458ca7ef4fc855cff62ca57b12125272a1e662e5e20c3b5c2e456658e205"] ||
        [description containsString:@"de3ab0f30103f0346311f6bea1ee2ab68afe8ae8df98506daf98ec3ebfc68e27"])) {
        id entry=objc_getAssociatedObject(encoder,&metal_light_buffers_key)[@1];
        const void *bytes=NULL;NSUInteger length=0;
        if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
        else if([entry isKindOfClass:NSArray.class]) {
            id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){bytes=(const char *)buffer.contents+offset;length=buffer.length-offset;}
        }
        /* These offsets are verified against both current 1.6.3 Global layouts. */
        if(length>=1968) {
            NSMutableArray *values=[NSMutableArray array];
            const unsigned offsets[]={432,1136,1152,1168,1216,1232,1248,1264,1280,1296,1312,1328,1344,1360,1376,1392,1952};
            for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
                float v[4];memcpy(v,(const char *)bytes+offsets[i],sizeof v);
                [values addObject:[NSString stringWithFormat:@"%u=[%g,%g,%g,%g]",offsets[i],v[0],v[1],v[2],v[3]]];
            }
            record_metal([NSString stringWithFormat:@"current-cloth-global:%@",[values componentsJoinedByString:@" "]]);
        } else record_metal(@"current-cloth-global:buffer unavailable");
    }
    bool currentSkin=metal_tracing &&
        [character_skin_original isEqual:@"ab7ad018484d94554fd335dc1f2a3938e5f8f8be444a6fa81460a3caebee815e"] &&
        [description containsString:@"SKIN_PARITY"];
    if(metal_tracing && (currentSkin || [description containsString:character_skin_original] || [description containsString:@"SKIN_NATIVE"])) {
        id entry=objc_getAssociatedObject(encoder,&metal_light_buffers_key)[@1];
        const void *bytes=NULL;NSUInteger length=0;
        if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
        else if([entry isKindOfClass:NSArray.class]) {
            id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){bytes=(const char *)buffer.contents+offset;length=buffer.length-offset;}
        }
        /* Current1.6.3 globals insert48bytes before CharacterParams; verified
           against the matched native AIR and current PC buffer declarations. */
        NSUInteger shift=currentSkin ? 48 : 0;
        if(length>=1840+shift) {
            float v[8];memcpy(v,(const char *)bytes+1808+shift,sizeof v);
            record_metal([NSString stringWithFormat:@"character-skin-ambient:direction=[%g,%g,%g,%g] params=[%g,%g,%g,%g]",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]]);
            float colorMode;memcpy(&colorMode,(const char *)bytes+1732+shift,4);
            record_metal([NSString stringWithFormat:@"character-skin-ambient:colorOverride=%g normalizationActive=%d",colorMode,colorMode<0.5f]);
        }
        if(length>=1936+shift) {
            float strength;memcpy(&strength,(const char *)bytes+1932+shift,4);
            record_metal([NSString stringWithFormat:@"character-skin-specular:globalStrength=%g layout=%@ %@",strength,currentSkin ? @"1.6.3" : @"legacy",description]);
        }
        if(currentSkin && length>=1968) {
            float intensity,blend;
            memcpy(&intensity,(const char *)bytes+1852,4);
            memcpy(&blend,(const char *)bytes+1964,4);
            record_metal([NSString stringWithFormat:@"character-skin-light:intensityOverride=%g overrideBlend=%g",intensity,blend]);
        }
        entry=objc_getAssociatedObject(encoder,&metal_light_buffers_key)[@6];bytes=NULL;length=0;
        if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
        else if([entry isKindOfClass:NSArray.class]) {
            id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){bytes=(const char *)buffer.contents+offset;length=buffer.length-offset;}
        }
        if(length>=12) {
            float v[3];memcpy(v,bytes,sizeof v);
            record_metal([NSString stringWithFormat:@"character-skin-material:smoothness=%g specular=%g metallic=%g",v[0],v[1],v[2]]);
        } else {
            character_skin_sample_private(encoder,entry,description);
            NSString *details=[entry isKindOfClass:NSArray.class] ?
                [NSString stringWithFormat:@"buffer storage=%lu length=%lu offset=%@",(unsigned long)[entry[0] storageMode],(unsigned long)[entry[0] length],entry[1]] :
                NSStringFromClass([entry class]) ?: @"missing";
            record_metal([NSString stringWithFormat:@"character-skin-material:unavailable %@",details]);
        }
    }
    if(metal_tracing && [description containsString:@"sha256=0a75560c2089e563fe99393e3b38b48529a9de09bca425a92f479e0919aa573f"]) {
        id entry=objc_getAssociatedObject(encoder,&metal_light_buffers_key)[@5];
        const void *bytes=NULL;NSUInteger length=0;
        if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
        else if([entry isKindOfClass:NSArray.class]) {
            id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){bytes=(const char *)buffer.contents+offset;length=buffer.length-offset;}
        }
        /* LOD500 programs488/492/496 share this exact fragment and ShadowData layout. */
        if(length>=8880) {
            float v[8];memcpy(v,(const char *)bytes+8848,sizeof v);
            record_metal([NSString stringWithFormat:@"character-shadow-constants:texel=[%g,%g,%g,%g] params=[%g,%g,%g,%g]",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]]);
        }
    }
    if(!metal_tracing || (![description containsString:@"source=DeferredLighting "] &&
       ![description containsString:@"da0098a33a2d153edcaacb0edf5d9b291729b4a77b73d1e72328b90aa07f5617"]))return;
    NSDictionary *bindings=objc_getAssociatedObject(encoder,&metal_light_buffers_key);
    for(NSNumber *index in bindings) {
        if(index.unsignedIntegerValue==1 || index.unsignedIntegerValue==6)continue;
        id entry=bindings[index];const void *bytes=NULL;NSUInteger length=0;
        if([entry isKindOfClass:NSData.class]){bytes=[entry bytes];length=[entry length];}
        else {
            id<MTLBuffer> buffer=entry[0];NSUInteger offset=[entry[1] unsignedIntegerValue];
            if(buffer.storageMode!=MTLStorageModePrivate && offset<buffer.length && buffer.contents){
                bytes=(const char *)buffer.contents+offset;length=MIN(32,buffer.length-offset);
            }
        }
        if(length<32){record_metal([NSString stringWithFormat:@"lighting-constants:buffer%@ unavailable %@",index,description]);continue;}
        float values[8];memcpy(values,bytes,32);
        record_metal([NSString stringWithFormat:@"lighting-constants:buffer%@ first8=[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g] %@",
            index,values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],description]);
    }
}

static void install_metal_light_constants(Class cls) {
    SEL sel=@selector(setFragmentBuffer:offset:atIndex:);Method method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger index){
        metal_light_buffer(object,buffer,offset,index);
        ((void(*)(id,SEL,id,NSUInteger,NSUInteger))original)(object,@selector(setFragmentBuffer:offset:atIndex:),buffer,offset,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setFragmentBuffers:offsets:withRange:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,const id<MTLBuffer> *buffers,const NSUInteger *offsets,NSRange range){
        if(metal_tracing || atomic_load(&metal_ssr_history_enabled) || atomic_load(&native_temporal_enabled) || official_gtao_active())for(NSUInteger i=0;i<range.length;i++)metal_light_buffer(object,buffers[i],offsets[i],range.location+i);
        ((void(*)(id,SEL,const id<MTLBuffer> *,const NSUInteger *,NSRange))original)(object,@selector(setFragmentBuffers:offsets:withRange:),buffers,offsets,range);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setFragmentBufferOffset:atIndex:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,NSUInteger offset,NSUInteger index){
        metal_ssr_camera_offset(object,offset,index);
        if(index<16)official_gtao_offset(object,offset,index);
        native_temporal_offset(object,offset,index);
        if(metal_tracing && (index<=1 || (index>=3 && index<=6))){
            NSMutableDictionary *bindings=objc_getAssociatedObject(object,&metal_light_buffers_key);id entry=bindings[@(index)];
            if([entry isKindOfClass:NSArray.class])bindings[@(index)]=@[entry[0],@(offset)];
        }
        ((void(*)(id,SEL,NSUInteger,NSUInteger))original)(object,@selector(setFragmentBufferOffset:atIndex:),offset,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setFragmentBytes:length:atIndex:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,const void *bytes,NSUInteger length,NSUInteger index){
        metal_light_bytes(object,bytes,length,index);
        ((void(*)(id,SEL,const void *,NSUInteger,NSUInteger))original)(object,@selector(setFragmentBytes:length:atIndex:),bytes,length,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
}

static void install_official_gtao_vertex_constants(Class cls) {
    SEL sel=@selector(setVertexBuffer:offset:atIndex:);Method method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,id<MTLBuffer> buffer,NSUInteger offset,NSUInteger index){
        if(index<16)official_gtao_buffer(object,buffer,offset,index+16);
        ((void(*)(id,SEL,id,NSUInteger,NSUInteger))original)(object,@selector(setVertexBuffer:offset:atIndex:),buffer,offset,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setVertexBuffers:offsets:withRange:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,const id<MTLBuffer> *buffers,const NSUInteger *offsets,NSRange range){
        if(official_gtao_active())for(NSUInteger i=0;i<range.length;i++)if(range.location+i<16)official_gtao_buffer(object,buffers[i],offsets[i],range.location+i+16);
        ((void(*)(id,SEL,const id<MTLBuffer> *,const NSUInteger *,NSRange))original)(object,@selector(setVertexBuffers:offsets:withRange:),buffers,offsets,range);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setVertexBufferOffset:atIndex:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,NSUInteger offset,NSUInteger index){
        if(index<16)official_gtao_offset(object,offset,index+16);
        ((void(*)(id,SEL,NSUInteger,NSUInteger))original)(object,@selector(setVertexBufferOffset:atIndex:),offset,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
    sel=@selector(setVertexBytes:length:atIndex:);method=class_getInstanceMethod(cls,sel);
    if(method){IMP original=method_getImplementation(method);IMP wrapper=imp_implementationWithBlock(^(id object,const void *bytes,NSUInteger length,NSUInteger index){
        if(index<16)official_gtao_bytes(object,bytes,length,index+16);
        ((void(*)(id,SEL,const void *,NSUInteger,NSUInteger))original)(object,@selector(setVertexBytes:length:atIndex:),bytes,length,index);
    });if(!class_addMethod(cls,sel,wrapper,method_getTypeEncoding(method)))method_setImplementation(method,wrapper);}
}
