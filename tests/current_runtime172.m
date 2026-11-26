#import "../src/v172/graphics172.m"
#include <assert.h>
static int testFrame(void) { return 101; }
static bool originalKey(int key) { return key == 65; }
int main(void) { @autoreleasepool {
    assert([selectedUpscaler172(@{@"upscaler":@"metalfx-native"}) isEqual:@"metalfx-temporal"]);
    assert([selectedUpscaler172(@{@"upscaler":@"taau"}) isEqual:@"taau"]);
    assert(validValue(@"keyboard", @YES) && !validValue(@"keyboard", @1));
    assert(validValue(@"height", @2160) && !validValue(@"height", @YES));
    assert(!validValue(@"wardrobe", @YES) && !validValue(@"sceneVegetation", @YES));
    selected=[@{@"upscaler":@"taau", @"keyboard":@YES} mutableCopy];
    changedInGame=[NSMutableSet set];
    handleLiveCommand(@{@"id":@"test", @"op":@"configure", @"settings":@{@"upscaler":@"metalfx-native", @"keyboard":@NO}});
    assert(!commandProblem && [selected[@"upscaler"] isEqual:@"metalfx-native"] && ![selected[@"keyboard"] boolValue]);
    handleLiveCommand(@{@"id":@"test2", @"op":@"probe", @"steps":@[]});
    assert(commandProblem && !wantsBaseline);
    handleLiveCommand(@{@"id":@"test3", @"op":@"compare", @"enabled":@YES});
    assert(!commandProblem && wantsBaseline);
    BMFOutput172 size={0}; __block int writes=0;
    BOOL (^setSize)(int,int)=^BOOL(int w,int h) { assert(w==3840 && h==2160);writes++;return YES; };
    runOutput172(&size,2160,YES,YES,1920,1080,1,setSize);
    assert([runOutput172(&size,2160,YES,YES,1920,1080,2,setSize)[@"status"] isEqual:@"waitingForReadback"]);
    assert(writes==1 && [runOutput172(&size,2160,YES,YES,3840,2160,3,setSize)[@"status"] isEqual:@"applied"]);
    assert([runOutput172(&size,2160,YES,YES,2560,1440,4,setSize)[@"status"] isEqual:@"userSize"] && writes==1);
    input172Frame=testFrame; input172VirtualMouse=1;
    for (int i=3;i<=5;i++) input172Queries[i].native=(uintptr_t)originalKey;
    input172ButtonHeld[1]=true;input172ButtonDown[1]=101;
    assert(input172KeyHeld(324) && input172KeyPressed(324) && !input172KeyReleased(324));
    input172ButtonHeld[1]=false;input172ButtonDown[1]=100;input172ButtonUp[1]=101;
    assert(!input172KeyHeld(324) && input172KeyReleased(324) && input172KeyHeld(65));
    puts("PASS: public settings and commands, output resize ownership, zipline GetKey mouse queries");
} return 0; }
