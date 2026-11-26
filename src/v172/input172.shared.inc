// Game code outside the InputSystem bindings still asks UnityEngine.Input for
// mouse buttons, the wheel, the pointer, Escape and the cursor state. Answer
// from the same AppKit state as the virtual devices. Each query keeps its code;
// only the icall pointer its wrapper has cached is replaced.
static int (*input172Frame)(void);
static int (*input172LockState)(void);
static bool input172ButtonHeld[5],input172EscapeHeld;
static int input172ButtonDown[5],input172ButtonUp[5],input172EscapeDown,input172EscapeUp;
static float input172Wheel[2];
static int input172WheelFrame;
static BOOL input172Captured;

typedef struct {
    const char *method;int count;const char *icall;uintptr_t replacement,native;_Atomic(uintptr_t) *slot;
} Input172Query;
static Input172Query input172Queries[10];
static void *input172QueryClass(int index) {return index==9 ? input172Cursor : input172LegacyInput;}

static bool input172Sharing(void) {return input172VirtualMouse!=0;}
// An AppKit event arrives between frames; its edge belongs to the next one.
static void input172SetButton(NSUInteger button,BOOL down) {
    input172Buttons[@(button)]=@(down);
    if(button>=5 || !input172Frame || down==input172ButtonHeld[button])return;
    input172ButtonHeld[button]=down;
    (down ? input172ButtonDown : input172ButtonUp)[button]=input172Frame()+1;
}
static void input172SetEscape(BOOL down) {
    if(!input172Frame || down==input172EscapeHeld)return;
    input172EscapeHeld=down;
    *(down ? &input172EscapeDown : &input172EscapeUp)=input172Frame()+1;
}
static void input172ShareWheel(double x,double y) {
    if(!input172Frame)return;
    int frame=input172Frame()+1;
    if(frame!=input172WheelFrame){input172WheelFrame=frame;input172Wheel[0]=input172Wheel[1]=0;}
    input172Wheel[0]+=(float)x;input172Wheel[1]+=(float)y;
}

// kind: 0 held, 1 pressed this frame, 2 released this frame.
static bool input172ButtonState(int button,int kind) {
    int frame=input172Frame();
    return kind==1 ? input172ButtonDown[button]==frame : kind==2 ? input172ButtonUp[button]==frame :
        input172ButtonHeld[button] || input172ButtonDown[button]==frame;
}
static bool input172ButtonQuery(int index,int button) {
    if(input172Sharing() && button>=0 && button<5 && input172ButtonState(button,index))return true;
    return ((bool (*)(int))input172Queries[index].native)(button);
}
static bool input172MouseHeld(int button) {return input172ButtonQuery(0,button);}
static bool input172MousePressed(int button) {return input172ButtonQuery(1,button);}
static bool input172MouseReleased(int button) {return input172ButtonQuery(2,button);}
static bool input172KeyQuery(int index,int key) {
    if(key>=323 && key<328 && input172Sharing() && input172ButtonState(key-323,index-3))return true;
    if(key==27 && input172VirtualKeyboard){
        int frame=input172Frame();
        return index==4 ? input172EscapeDown==frame : index==5 ? input172EscapeUp==frame :
            input172EscapeHeld || input172EscapeDown==frame;
    }
    return ((bool (*)(int))input172Queries[index].native)(key);
}
static bool input172KeyHeld(int key) {return input172KeyQuery(3,key);}
static bool input172KeyPressed(int key) {return input172KeyQuery(4,key);}
static bool input172KeyReleased(int key) {return input172KeyQuery(5,key);}
static void input172WheelQuery(float *delta) {
    if(input172Sharing() && input172WheelFrame==input172Frame()){delta[0]=input172Wheel[0];delta[1]=input172Wheel[1];}
    else ((void (*)(float *))input172Queries[6].native)(delta);
}
static void input172PointerQuery(float *position) {
    if(input172Sharing() && input172Size[0]>0){
        // A locked pointer sits at the screen centre, as it does on PC.
        position[0]=input172Captured ? input172Size[0]/2 : input172Pointer[0];
        position[1]=input172Captured ? input172Size[1]/2 : input172Pointer[1];
        position[2]=0;
    }else ((void (*)(float *))input172Queries[7].native)(position);
}
static bool input172AnyQuery(void) {
    if(input172Sharing())for(int i=0;i<5;i++)if(input172ButtonHeld[i])return true;
    return ((bool (*)(void))input172Queries[8].native)();
}
// iOS Unity always reports a visible pointer, which keeps the game's PC keyboard
// logic (click to attack, camera look) switched off. Report the lock state the
// game maintains instead.
static bool input172VisibleQuery(void) {return input172LockState()!=1;}

static _Atomic(uintptr_t) *input172QuerySlot(void *cls,Input172Query *query) {
    static void *(*resolve)(const char *);
    if(!resolve)resolve=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    const void *method=cls ? lookupMethod(cls,query->method,query->count) : NULL;
    const uint32_t *code=method ? *(const uint32_t *const *)method : NULL;
    if(!resolve || !code || !(query->native=(uintptr_t)resolve(query->icall)))return NULL;
    uint64_t scratch[4]={0};void *args[]={scratch},*exception=NULL;
    invoke(method,NULL,args,&exception); // the first call fills the wrapper's cache
    if(exception)return NULL;
    for(int i=0;i<12;i++){
        // adrp xB,page ; ldr xT,[xB,#slot] ; cbnz xT,call
        uint32_t page=code[i],load=code[i+1],test=code[i+2];
        if((page&0x9f000000)!=0x90000000 || (load&0xffc003e0)!=(0xf9400000|(page&31)<<5) ||
           (test&0xff00001f)!=(0xb5000000|(load&31)))continue;
        int64_t pages=((page>>29)&3)|(((page>>5)&0x7ffff)<<2);
        if(pages&(1<<20))pages-=1<<21;
        _Atomic(uintptr_t) *slot=(void *)((((uintptr_t)(code+i))&~(uintptr_t)4095)+pages*4096+((load>>10)&4095)*8);
        return atomic_load(slot)==query->native ? slot : NULL;
    }
    return NULL;
}

static NSString *input172ShareState=@"off";
static void input172ShareQueries(BOOL enable) {
    if(enable==![input172ShareState isEqual:@"off"])return;
    if(!enable){
        for(int i=0;i<10;i++)if(input172Queries[i].slot){
            uintptr_t expected=input172Queries[i].replacement;
            atomic_compare_exchange_strong(input172Queries[i].slot,&expected,input172Queries[i].native);
            input172Queries[i].slot=NULL;
        }
        input172ShareState=@"off";return;
    }
    void *(*resolve)(const char *)=dlsym(RTLD_DEFAULT,"il2cpp_resolve_icall");
    input172Frame=resolve ? resolve("UnityEngine.Time::get_frameCount()") : NULL;
    input172LockState=resolve ? resolve("UnityEngine.Cursor::get_lockState()") : NULL;
    input172ShareState=@"unsupported";
    if(!input172Frame || !input172LockState || !input172LegacyInput)return;
#define QUERY(index,name,args,signature,function) \
    input172Queries[index]=(Input172Query){name,args,"UnityEngine." signature,(uintptr_t)function,0,NULL}
    QUERY(0,"GetMouseButton",1,"Input::GetMouseButton(System.Int32)",input172MouseHeld);
    QUERY(1,"GetMouseButtonDown",1,"Input::GetMouseButtonDown(System.Int32)",input172MousePressed);
    QUERY(2,"GetMouseButtonUp",1,"Input::GetMouseButtonUp(System.Int32)",input172MouseReleased);
    QUERY(3,"GetKeyInt",1,"Input::GetKeyInt(UnityEngine.KeyCode)",input172KeyHeld);
    QUERY(4,"GetKeyDownInt",1,"Input::GetKeyDownInt(UnityEngine.KeyCode)",input172KeyPressed);
    QUERY(5,"GetKeyUpInt",1,"Input::GetKeyUpInt(UnityEngine.KeyCode)",input172KeyReleased);
    QUERY(6,"get_mouseScrollDelta_Injected",1,"Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)",input172WheelQuery);
    QUERY(7,"get_mousePosition_Injected",1,"Input::get_mousePosition_Injected(UnityEngine.Vector3&)",input172PointerQuery);
    QUERY(8,"get_anyKey",0,"Input::get_anyKey()",input172AnyQuery);
    QUERY(9,"get_visible",0,"Cursor::get_visible()",input172VisibleQuery);
#undef QUERY
    int shared=0;
    for(int i=0;i<10;i++){
        _Atomic(uintptr_t) *slot=input172QuerySlot(input172QueryClass(i),&input172Queries[i]);
        uintptr_t expected=input172Queries[i].native;
        if(!slot || !atomic_compare_exchange_strong(slot,&expected,input172Queries[i].replacement))continue;
        input172Queries[i].slot=slot;shared++;
    }
    input172ShareState=[NSString stringWithFormat:@"%d/10",shared];
}
