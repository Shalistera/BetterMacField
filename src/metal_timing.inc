/* Explicit GPU timing sample of command buffers containing scene lighting. */
#import <QuartzCore/QuartzCore.h>
static atomic_bool metal_timing_active;
static NSMutableArray *metal_timing_samples;
static NSMutableArray *metal_timing_main_tasks;
static char metal_timing_buffer_key,metal_timing_scene_key,metal_timing_encode_key;

static void metal_timing_encoder(id encoder,id buffer) {
    if(metal_timing_active) {
        objc_setAssociatedObject(encoder,&metal_timing_buffer_key,[NSValue valueWithNonretainedObject:buffer],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if(!objc_getAssociatedObject(buffer,&metal_timing_encode_key))
            objc_setAssociatedObject(buffer,&metal_timing_encode_key,@(CACurrentMediaTime()),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}

static void metal_timing_pipeline(id encoder,id pipeline) {
    if(!metal_timing_active)return;
    NSString *description=objc_getAssociatedObject(pipeline,&metal_pipeline_description_key);
    if(![description containsString:@"source=DeferredLighting "])return;
    id buffer=[objc_getAssociatedObject(encoder,&metal_timing_buffer_key) nonretainedObjectValue];
    if(buffer)objc_setAssociatedObject(buffer,&metal_timing_scene_key,@YES,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void install_metal_timing(Class bufferClass) {
    SEL selector=@selector(commit);
    Method method=class_getInstanceMethod(bufferClass,selector);
    if(!method)return;
    IMP original=method_getImplementation(method);
    IMP replacement=imp_implementationWithBlock(^(id<MTLCommandBuffer> buffer){
        double entered=metal_timing_active ? CACurrentMediaTime() : 0;
        metal_ssr_history_commit(buffer);
        character_skin_commit(buffer);
        if(entered>0) {
            double submitted=CACurrentMediaTime();
            double encoding=[objc_getAssociatedObject(buffer,&metal_timing_encode_key) doubleValue];
            NSMutableArray *samples=metal_timing_samples;
            bool scene=[objc_getAssociatedObject(buffer,&metal_timing_scene_key) boolValue];
            [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){
                double start=completed.GPUStartTime,end=completed.GPUEndTime;
                NSDictionary *row=@{@"start":@(start),@"end":@(end),@"ms":@((end-start)*1000),
                    @"encodeStart":@(encoding),@"commitTime":@(submitted),@"historyCommitMS":@((submitted-entered)*1000),
                    @"kernelStart":@(completed.kernelStartTime),@"kernelEnd":@(completed.kernelEndTime),
                    @"sceneLighting":@(scene),@"status":@(completed.status)};
                @synchronized(samples){if(samples.count<1200)[samples addObject:row];}
            }];
        }
        ((void(*)(id,SEL))original)(buffer,@selector(commit));
    });
    if(!class_addMethod(bufferClass,selector,replacement,method_getTypeEncoding(method)))method_setImplementation(method,replacement);
}

static void capture_metal_timing(NSDictionary *command) {
    if(metal_timing_active){fprintf(output,"gpu_timing=busy\n");return;}
    double seconds=command[@"seconds"] ? fmax(1,fmin(5,[command[@"seconds"] doubleValue])) : 3;
    NSMutableArray *samples=[NSMutableArray array];metal_timing_samples=samples;
    NSMutableArray *tasks=[NSMutableArray array];metal_timing_main_tasks=tasks;
    double started=NSDate.date.timeIntervalSince1970;
    metal_timing_active=true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(seconds*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        metal_timing_active=false;
        // Include ordinary in-flight work; no GPU or main-thread wait is introduced.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/2),dispatch_get_main_queue(),^{
            NSArray *rows;@synchronized(samples){rows=[samples copy];}
            NSDictionary *result=@{@"startedAt":@(started),@"endedAt":@(NSDate.date.timeIntervalSince1970),
                @"seconds":@(seconds),@"samples":rows,@"mainTasks":[tasks copy]};
            [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
                writeToFile:runtime_path(@"endfield-ios-gpu-timing.json") atomically:YES];
            fprintf(output,"gpu_timing=complete samples=%lu\n",(unsigned long)rows.count);
        });
    });
}
