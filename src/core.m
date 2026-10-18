#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#include <CommonCrypto/CommonDigest.h>
#include <simd/simd.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <unistd.h>

static FILE *output;
static bool probe_cn_client;
static NSString *runtime_path(NSString *name) {
    NSString *root=NSProcessInfo.processInfo.environment[@"BMF_RUNTIME_DIR"] ?: NSTemporaryDirectory();
    return [root stringByAppendingPathComponent:name];
}
static uint32_t override_volume_handle,override_profile_handle;
static bool character_shadow_override_enabled,character_shadow_startup_pending=true;
static NSString *const character_shadow_saved_key=@"BetterMacFieldCharacterSelfShadowEnabled";
#include "settings.inc"
#include "engine.inc"
#include "input_native.inc"
#include "input_bridge.inc"
#include "input_auto.inc"
#include "graphics_runtime.inc"
#include "graphics_mode.inc"
#include "native_temporal_size.inc"
#include "ao_render_source.inc"
#include "ao_compose.inc"
#include "display.inc"
#include "game_settings.inc"
#include "background_perf.inc"
#include "metal_hud.inc"
#include "menu.inc"
#include "uid_display.inc"

static dispatch_source_t core_timer;
static bool core_settings_pending=true;
static void core_start(void) {
    if(!bind_api() || !domain_get())return;
    hub_class=find_class("HG.Rendering.Runtime","HGRenderPipelineSettingHub");
    if(!get_hub())return;
    input_auto_configure(@{@"enabled":bmf_settings[@"keyboard"] ?: @YES});
    install_metal_hud_menu();
    install_core_menu();
    core_timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    dispatch_source_set_timer(core_timer,DISPATCH_TIME_NOW,NSEC_PER_SEC,100*NSEC_PER_MSEC);
    dispatch_source_set_event_handler(core_timer,^{@autoreleasepool {
        @try {
            core_command();
            bmf_uid_update();
            input_auto_window_update();
            background_perf_tick();
            if(core_settings_pending && input_static(find_class("Beyond.Scripts.Quality","QualityManager"),"get_instance",0,NULL)){
                game_settings(@{@"components":@YES,@"reveal":@YES,@"resolutionBridge":@YES});
                core_settings_pending=false;
            }
            game_settings_tick();
            scene_lod_startup_tick();
            display_upscaler_startup_tick();
            display_native_temporal_tick();
            if(input_scene_is_ready()){
                if(character_shadow_startup_pending){
                    character_shadow_startup_pending=false;
                    if([NSUserDefaults.standardUserDefaults boolForKey:character_shadow_saved_key])
                        override_volume(@"HGCharacterVolume",@{@"enabled":@YES},false);
                }
                core_shadow_startup();
                bmf_apply_settings();
            }
        }@catch(NSException *exception){fprintf(output,"runtime_exception=%s\n",exception.reason.UTF8String);}
    }});
    dispatch_resume(core_timer);
    fprintf(output,"core=ready\n");
}

__attribute__((constructor)) static void core_install(void) {
    @autoreleasepool {
        probe_cn_client=[NSBundle.mainBundle.bundleIdentifier isEqual:@"com.hypergryph.endfield"];
        output=fopen(runtime_path(@"bettermacfield-runtime.log").fileSystemRepresentation,"w");if(!output)return;
        setvbuf(output,NULL,_IOLBF,0);
        fprintf(output,"core=starting pid=%d\n",getpid());
        bmf_settings_load();
        [NSUserDefaults.standardUserDefaults registerDefaults:@{character_shadow_saved_key:@YES}];
        // Install Metal identity observers before scene shaders are loaded.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            install_metal_trace(MTLCreateSystemDefaultDevice());
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_SEC),dispatch_get_main_queue(),^{core_start();});
    }
}
