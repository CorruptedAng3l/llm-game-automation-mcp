// Bounded legacy-message publication records. This preserves ordinary keyboard
// messages and queue filters; tuple matching is not cryptographic queue origin.
constexpr int kPostedCapacity=512;
struct PostedMsg {
    UINT m=0;WPARAM w=0;LPARAM l=0;HWND window=nullptr;DWORD thread=0;
    unsigned long long serial=0,generation=0;ULONG_PTR windowStamp=0;
    bool translated=false,dispatching=false,suppressTranslation=false;
    BYTE keys[256]{};HKL layout=nullptr;
};
SRWLOCK g_ringLock=SRWLOCK_INIT;
PostedMsg g_ring[kPostedCapacity];int g_ringPos=0;
unsigned long long g_postedSerial=0;ULONG_PTR g_postedWindowSerial=0;
const wchar_t kPostedWindowProperty[]=L"vinput.posted_input.window_generation.v1";
std::atomic<unsigned long long> g_postedRingRefused{0},g_postedRingStale{0},g_postedDispatchRetired{0};
std::atomic<unsigned long long> g_translationClaimed{0},g_translationDuplicate{0},g_translationChars{0},g_translationPostFailed{0},g_translationNoChar{0},g_translationUnsupported{0},g_translationDead{0};
std::atomic<bool> g_translationInstalled{false},g_translationNoState{false},g_postedDispatchReady{false};
std::atomic<DWORD> g_translationLastError{0};
using TranslateMessageFn=BOOL(WINAPI*)(const MSG*);
TranslateMessageFn o_TranslateMessage=nullptr;
int JobResource(int);
extern unsigned long long g_resourceGeneration[257];
bool PostOurs(HWND,UINT,WPARAM,LPARAM,bool suppressTranslation=false);
bool PostTranslated(const PostedMsg&,UINT,WPARAM,LPARAM);
bool PostedInputMessage(UINT message){return (message>=WM_KEYFIRST&&message<=WM_KEYLAST)||(message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST);}
bool PostedKeyMessage(UINT message){return message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP;}
bool PostedEntryValid(const PostedMsg& entry){
    if(!entry.m||!entry.serial||entry.generation!=g_inputReadyGeneration.load()||entry.window!=g_hwnd)return false;
    DWORD pid=0;
    return GetWindowThreadProcessId(entry.window,&pid)==entry.thread&&pid==g_pid&&
        (ULONG_PTR)GetPropW(entry.window,kPostedWindowProperty)==entry.windowStamp;
}
bool PostedCaptureTarget(PostedMsg& target){
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);target={};target.window=g_hwnd;target.generation=g_inputReadyGeneration;
    DWORD owner=0;target.thread=target.window?GetWindowThreadProcessId(target.window,&owner):0;
    if(!target.thread||owner!=g_pid){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return false;}
    AcquireSRWLockExclusive(&g_ringLock);target.windowStamp=(ULONG_PTR)GetPropW(target.window,kPostedWindowProperty);
    if(!target.windowStamp){target.windowStamp=++g_postedWindowSerial;if(!target.windowStamp)target.windowStamp=++g_postedWindowSerial;
        if(!SetPropW(target.window,kPostedWindowProperty,(HANDLE)target.windowStamp)){ReleaseSRWLockExclusive(&g_ringLock);return false;}}
    ReleaseSRWLockExclusive(&g_ringLock);return true;
}
bool PostedTargetCurrent(const PostedMsg& target){
    DWORD owner=0;return target.window==g_hwnd&&target.generation==g_inputReadyGeneration.load()&&
        GetWindowThreadProcessId(target.window,&owner)==target.thread&&owner==g_pid&&
        (ULONG_PTR)GetPropW(target.window,kPostedWindowProperty)==target.windowStamp;
}
// Caller holds g_ringLock. Select publication order, never array-index order
// after wrap, so equal key tuples retain their original modifier snapshots.
int PostedFind(HWND window,UINT message,WPARAM w,LPARAM l,bool unclaimed){
    int found=-1;unsigned long long oldest=~0ULL;
    for(int i=0;i<kPostedCapacity;i++){
        auto& entry=g_ring[i];if(entry.window!=window||entry.m!=message||entry.w!=w||entry.l!=l)continue;
        if(!PostedEntryValid(entry)){entry={};++g_postedRingStale;continue;}
        if((!unclaimed||!entry.dispatching)&&entry.serial<oldest){oldest=entry.serial;found=i;}
    }
    return found;
}
int RingAdd(HWND window,UINT message,WPARAM w,LPARAM l,bool suppress,unsigned long long& serial,const PostedMsg* expected){
    serial=0;DWORD owner=0;const DWORD thread=GetWindowThreadProcessId(window,&owner);
    if(!thread||owner!=g_pid||window!=g_hwnd){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return -1;}
    AcquireSRWLockExclusive(&g_ringLock);
    ULONG_PTR stamp=(ULONG_PTR)GetPropW(window,kPostedWindowProperty);
    if(!stamp){stamp=++g_postedWindowSerial;if(!stamp)stamp=++g_postedWindowSerial;
        if(!SetPropW(window,kPostedWindowProperty,(HANDLE)stamp)){ReleaseSRWLockExclusive(&g_ringLock);return -1;}}
    const auto generation=g_inputReadyGeneration.load();
    if(expected&&(expected->window!=window||expected->thread!=thread||expected->generation!=generation||expected->windowStamp!=stamp)){
        ReleaseSRWLockExclusive(&g_ringLock);SetLastError(ERROR_RETRY);return -1;
    }
    int slot=-1;
    for(int offset=0;offset<kPostedCapacity;offset++){
        const int i=(g_ringPos+offset)%kPostedCapacity;auto& entry=g_ring[i];
        // Retargeted/destroyed-window records are no longer live publications.
        if(entry.m&&(entry.generation!=generation||(entry.window==window&&entry.windowStamp!=stamp))){entry={};++g_postedRingStale;}
        if(!entry.m){slot=i;break;}
    }
    if(slot<0){++g_postedRingRefused;ReleaseSRWLockExclusive(&g_ringLock);SetLastError(ERROR_NOT_ENOUGH_QUOTA);return -1;}
    PostedMsg entry;entry.m=message;entry.w=w;entry.l=l;entry.window=window;entry.thread=thread;
    entry.generation=expected?expected->generation:generation;entry.windowStamp=stamp;entry.serial=++g_postedSerial;if(!entry.serial)entry.serial=++g_postedSerial;
    entry.suppressTranslation=suppress;
    if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){
        for(int key=0;key<256;key++)entry.keys[key]=g_vk[key]?0x80:0;
        // Never merge physical toggle bits. Generic modifiers are already
        // mirrored by SetVkState; copy the target UI thread's layout read-only.
        entry.layout=GetKeyboardLayout(thread);
    }
    serial=entry.serial;g_ring[slot]=entry;g_ringPos=(slot+1)%kPostedCapacity;
    ReleaseSRWLockExclusive(&g_ringLock);return slot;
}
void RingRetire(int slot,unsigned long long serial,bool dispatched=false){
    if(slot<0||slot>=kPostedCapacity||!serial)return;
    AcquireSRWLockExclusive(&g_ringLock);
    if(g_ring[slot].serial==serial){g_ring[slot]={};if(dispatched)++g_postedDispatchRetired;}
    ReleaseSRWLockExclusive(&g_ringLock);
}
struct PostedDispatchContext {bool active=false;HWND window=nullptr;UINT message=0;WPARAM w=0;LPARAM l=0;int slot=-1;unsigned long long serial=0;};
thread_local PostedDispatchContext g_postedDispatch;
struct PostedDispatchScope {
    PostedDispatchContext previous;int slot=-1;unsigned long long serial=0;
    explicit PostedDispatchScope(const MSG& message):previous(g_postedDispatch){
        g_postedDispatch={true,message.hwnd,message.message,message.wParam,message.lParam,-1,0};
        if(!PostedInputMessage(message.message))return;
        AcquireSRWLockExclusive(&g_ringLock);
        slot=PostedFind(message.hwnd,message.message,message.wParam,message.lParam,true);
        if(slot>=0){auto& entry=g_ring[slot];entry.dispatching=true;serial=entry.serial;g_postedDispatch.slot=slot;g_postedDispatch.serial=serial;}
        ReleaseSRWLockExclusive(&g_ringLock);
    }
    ~PostedDispatchScope(){RingRetire(slot,serial,true);g_postedDispatch=previous;}
};
bool RingConsume(HWND window,UINT message,WPARAM w,LPARAM l){
    bool hit=false;AcquireSRWLockExclusive(&g_ringLock);
    int slot=-1;
    if(g_postedDispatch.active){
        const auto& context=g_postedDispatch;
        if(context.window==window&&context.message==message&&context.w==w&&context.l==l&&context.slot>=0&&
            g_ring[context.slot].serial==context.serial)slot=context.slot;
    }else slot=PostedFind(window,message,w,l,false); // existing direct-WndProc tuple coverage
    if(slot>=0&&PostedEntryValid(g_ring[slot])){g_ring[slot]={};hit=true;}
    ReleaseSRWLockExclusive(&g_ringLock);return hit;
}
// 0 unowned,1 first owned down,2 owned already attempted/up,3 native forwarding.
int RingTranslation(const MSG& message,bool isolating,PostedMsg& snapshot){
    AcquireSRWLockExclusive(&g_ringLock);const int slot=PostedFind(message.hwnd,message.message,message.wParam,message.lParam,false);
    if(slot<0){ReleaseSRWLockExclusive(&g_ringLock);return 0;}
    auto& entry=g_ring[slot];
    if(entry.thread!=GetCurrentThreadId()){ReleaseSRWLockExclusive(&g_ringLock);return 0;}
    if(entry.translated){++g_translationDuplicate;ReleaseSRWLockExclusive(&g_ringLock);return 2;}
    entry.translated=true;snapshot=entry;
    if(!isolating&&!entry.suppressTranslation){ReleaseSRWLockExclusive(&g_ringLock);return 3;}
    ReleaseSRWLockExclusive(&g_ringLock);
    if(message.message==WM_KEYUP||message.message==WM_SYSKEYUP||snapshot.suppressTranslation)return 2;
    ++g_translationClaimed;return 1;
}
// Protected by g_inputMx; the native translation/callback itself holds no lock.
unsigned g_postedTranslationsInFlight=0;
struct PostedTranslationInFlight {
    bool active=false;
    void BeginLocked(){++g_postedTranslationsInFlight;active=true;}
    ~PostedTranslationInFlight(){if(active){std::lock_guard<std::recursive_mutex> lock(g_inputMx);--g_postedTranslationsInFlight;}}
};
bool TranslationNoStateRuntime(){
    using RtlVersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE module=GetModuleHandleW(L"ntdll.dll");auto version=module?(RtlVersionFn)GetProcAddress(module,"RtlGetVersion"):nullptr;
    OSVERSIONINFOW info{};info.dwOSVersionInfoSize=sizeof(info);
    return version&&version(&info)==0&&(info.dwMajorVersion>10||(info.dwMajorVersion==10&&info.dwBuildNumber>=14393));
}
BOOL WINAPI h_TranslateMessage(const MSG* input){
    MSG message{};
    if(!input||!ReaderBufferLoad(input,&message,sizeof(message))){SetLastError(ERROR_NOACCESS);return FALSE;}
    if(!PostedKeyMessage(message.message))return o_TranslateMessage(input);
    const bool isolating=MouseKeyboardIsolating();PostedMsg snapshot;
    int source=0;PostedTranslationInFlight inFlight;
    {std::lock_guard<std::recursive_mutex> claim(g_inputMx);source=RingTranslation(message,isolating,snapshot);if(source==1||source==3)inFlight.BeginLocked();}
    if(source==3||(!source&&!isolating))return o_TranslateMessage(input);
    if(!source)return FALSE; // outside key messages are not translated while isolated
    if(source==2)return TRUE;
    if(!g_translationNoState||!snapshot.layout){++g_translationUnsupported;g_translationLastError=ERROR_NOT_SUPPORTED;return TRUE;}
    WCHAR text[16]{};const UINT scan=(UINT)(((ULONG_PTR)message.lParam>>16)&0xff);
    const int count=ToUnicodeEx((UINT)message.wParam,scan,snapshot.keys,text,(int)(sizeof(text)/sizeof(text[0])),4,snapshot.layout);
    if(!count){++g_translationNoChar;return TRUE;}
    const bool dead=count<0;if(dead)++g_translationDead;
    const unsigned units=dead?(text[0]?1u:0u):(unsigned)count;
    if(units>sizeof(text)/sizeof(text[0])){++g_translationUnsupported;g_translationLastError=ERROR_INVALID_DATA;return TRUE;}
    const UINT character=message.message==WM_SYSKEYDOWN?(dead?WM_SYSDEADCHAR:WM_SYSCHAR):(dead?WM_DEADCHAR:WM_CHAR);
    // No ring lock or input lock is held while translating or posting. A full
    // ring fails immediately; one-shot state prevents retries/native fallback.
    for(unsigned i=0;i<units;i++){
        if(!PostTranslated(snapshot,character,(WPARAM)text[i],message.lParam)){++g_translationPostFailed;g_translationLastError=GetLastError();break;}
        ++g_translationChars;
    }
    return TRUE; // Native BOOL is nonzero for key messages even with no character.
}
int MessageKey(UINT m,WPARAM w,bool& down){
    down=m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_MBUTTONDOWN||m==WM_XBUTTONDOWN;
    if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN||m==WM_KEYUP||m==WM_SYSKEYUP)return w>0&&w<256?(int)w:0;
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP)return VK_LBUTTON;if(m==WM_RBUTTONDOWN||m==WM_RBUTTONUP)return VK_RBUTTON;
    if(m==WM_MBUTTONDOWN||m==WM_MBUTTONUP)return VK_MBUTTON;if(m==WM_XBUTTONDOWN||m==WM_XBUTTONUP)return HIWORD(w)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2;
    return 0;
}
bool PostPublication(HWND window,UINT message,WPARAM w,LPARAM l,bool suppressTranslation,const PostedMsg* expected){
    // Includes direct type/translated chars, which can publish outside KeyEvent.
    std::lock_guard<std::recursive_mutex> inputPublication(g_inputMx);
    DWORD owner=0;if(!window||!GetWindowThreadProcessId(window,&owner)||owner!=g_pid){DWORD error=owner?ERROR_ACCESS_DENIED:ERROR_INVALID_WINDOW_HANDLE;++C.post_target_rejected;C.last_post_target_error=error;SetLastError(error);return false;}
    bool down=false;const int vk=MessageKey(message,w,down);
    int slot=-1;unsigned long long serial=0;
    if(PostedInputMessage(message)){
        slot=RingAdd(window,message,w,l,suppressTranslation,serial,expected);
        if(slot<0){const DWORD error=GetLastError();++C.post_target_rejected;C.last_post_target_error=error;SetLastError(error);return false;}
    }
    if(vk)g_keyTrace[vk].posting(down); // only an actual native PostMessage attempt
    const BOOL accepted=PostMessageW(window,message,w,l);const DWORD error=accepted?0:GetLastError();++C.posted;
    if(vk)g_keyTrace[vk].post_result(down,accepted!=FALSE,error);
    if(!accepted)RingRetire(slot,serial);SetLastError(error);return accepted!=FALSE;
}
bool PostOurs(HWND window,UINT message,WPARAM w,LPARAM l,bool suppressTranslation){return PostPublication(window,message,w,l,suppressTranslation,nullptr);}
bool PostTranslated(const PostedMsg& source,UINT message,WPARAM w,LPARAM l){return PostPublication(source.window,message,w,l,false,&source);}
std::string PostedInputStatusJson(){
    unsigned pending=0;AcquireSRWLockShared(&g_ringLock);for(const auto& entry:g_ring)if(entry.m)++pending;ReleaseSRWLockShared(&g_ringLock);
    return "{\"version\":1,\"capacity\":512,\"pending\":"+std::to_string(pending)+",\"ring_refused\":"+std::to_string(g_postedRingRefused.load())+
        ",\"stale_retired\":"+std::to_string(g_postedRingStale.load())+",\"dispatch_retired\":"+std::to_string(g_postedDispatchRetired.load())+
        ",\"translator_hook_ready\":"+(g_translationInstalled?"true":"false")+",\"dispatch_hooks_ready\":"+(g_postedDispatchReady?"true":"false")+
        ",\"no_state_change_supported\":"+(g_translationNoState?"true":"false")+",\"translated_keydowns\":"+std::to_string(g_translationClaimed.load())+
        ",\"duplicate_translate_suppressed\":"+std::to_string(g_translationDuplicate.load())+",\"characters_posted\":"+std::to_string(g_translationChars.load())+
        ",\"character_post_failures\":"+std::to_string(g_translationPostFailed.load())+",\"no_character\":"+std::to_string(g_translationNoChar.load())+
        ",\"unsupported\":"+std::to_string(g_translationUnsupported.load())+",\"dead_key_events\":"+std::to_string(g_translationDead.load())+
        ",\"last_error\":"+std::to_string(g_translationLastError.load())+
        ",\"scope\":\"legacy HWND/tuple publication records through ordinary TranslateMessage then DispatchMessageA/W; not cryptographic queue provenance; no dead-key composition, IME or Alt+numpad guarantee; Unicode type preferred\"}";
}
