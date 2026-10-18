/* Adapt the shipped depth-only output to the split terrain prepass attachment. */
static char terrain_encoder_target_key,terrain_encoder_depth_color_key;
static id<MTLTexture> terrain_depth_output;
static bool terrain_depth_enabled;

static void terrain_depth_encoder(id encoder,MTLRenderPassDescriptor *pass) {
    if(terrain_depth_enabled) {
        objc_setAssociatedObject(encoder,&terrain_encoder_target_key,pass.colorAttachments[0].texture,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(encoder,&terrain_encoder_depth_color_key,pass.colorAttachments[4].texture,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

static void terrain_depth_draw(id encoder,NSString *identity) {
    if(!terrain_depth_enabled)return;
    id<MTLTexture> target=objc_getAssociatedObject(encoder,&terrain_encoder_target_key);
    if([identity containsString:@"TERRAIN_DEPTH_OUTPUT0"]) {
        terrain_depth_output=target;
        if(metal_tracing)record_metal([NSString stringWithFormat:@"terrain-depth-source:%p %lux%lu %@",(__bridge void *)target,
            (unsigned long)target.width,(unsigned long)target.height,target.label]);
    } else if([identity containsString:@"sha256=d8ce0cb599ec6cb2eb12f80557f892c267a2706bcb3bcefa16fd091195de94aa"] &&
              terrain_depth_output && target.width==terrain_depth_output.width && target.height==terrain_depth_output.height) {
        [encoder setFragmentTexture:terrain_depth_output atIndex:12];
        if(metal_tracing)record_metal([NSString stringWithFormat:@"terrain-depth-input:%p %lux%lu %@",(__bridge void *)terrain_depth_output,
            (unsigned long)terrain_depth_output.width,(unsigned long)terrain_depth_output.height,terrain_depth_output.label]);
        if(metal_tracing) {
            id<MTLTexture> depth=objc_getAssociatedObject(encoder,&terrain_encoder_depth_color_key);
            record_metal([NSString stringWithFormat:@"terrain-depth-color4:%p %lux%lu %@",(__bridge void *)depth,
                (unsigned long)depth.width,(unsigned long)depth.height,depth.label]);
        }
    }
}

static MTLRenderPipelineDescriptor *terrain_depth_pipeline(id<MTLDevice> device,MTLRenderPipelineDescriptor *descriptor) {
    if(!terrain_depth_enabled)return descriptor;
    NSString *identity=objc_getAssociatedObject(descriptor.fragmentFunction,&metal_function_source_key);
    if(![identity containsString:@"sha256=2aab747c74eadcfb2a7d65dd031f2548be2b982dd2ed287dc959a33c96335ae3"] ||
       descriptor.colorAttachments[0].pixelFormat!=MTLPixelFormatR32Float)return descriptor;
    for(NSUInteger i=1;i<8;i++)if(descriptor.colorAttachments[i].pixelFormat!=MTLPixelFormatInvalid)return descriptor;
    static id<MTLFunction> function;
    if(!function) {
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal;\n"
            "fragment float EndfieldTerrainDepth(float4 position [[position]]) { return position.z; }\n";
        NSError *error=nil;
        id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
        function=[library newFunctionWithName:@"EndfieldTerrainDepth"];
        if(!function){fprintf(output,"terrain_depth_pipeline=compile_failed %s\n",error.description.UTF8String);return descriptor;}
        objc_setAssociatedObject(function,&metal_function_source_key,@"TERRAIN_DEPTH_OUTPUT0 original_sha256=2aab747c74eadcfb2a7d65dd031f2548be2b982dd2ed287dc959a33c96335ae3",OBJC_ASSOCIATION_COPY_NONATOMIC);
    }
    MTLRenderPipelineDescriptor *copy=[descriptor copy];copy.fragmentFunction=function;
    fprintf(output,"terrain_depth_pipeline=adapted color0=R32Float\n");
    return copy;
}
