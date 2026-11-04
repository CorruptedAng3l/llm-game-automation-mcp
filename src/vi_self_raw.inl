// Target-origin WM_INPUT provenance for unfiltered DispatchMessageA/W pumps.
// Include after HookApi. Define _CRT_RAND_S before including the CRT headers.
// No invalid HRAWINPUT value is trusted merely because it is invalid or resembles our ring.
// Queue filters excluding the private envelope ID, direct queue consumers and
// SendMessage raw injection do not acquire this provenance.
using ViPostMessage = BOOL (WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using ViDispatchMessage = LRESULT (WINAPI*)(const MSG*);
ViPostMessage o_SelfPostMessageA=nullptr,o_SelfPostMessageW=nullptr;
ViDispatchMessage o_SelfDispatchMessageA=nullptr,o_SelfDispatchMessageW=nullptr;

struct SelfRawPacket {
    HWND window=nullptr; DWORD thread=0; WPARAM w=0; LPARAM l=0;
    ULONG_PTR proof=0,windowStamp=0; ULONGLONG expires=0;
    bool internal=false;RAWINPUT record{};
    std::shared_ptr<WndProcBinding> inputBinding; // immutable selected origin, including latched revocation

};
constexpr size_t kSelfRawCapacity=1024;
constexpr ULONGLONG kSelfRawLifetimeMs=10000;
const wchar_t kSelfRawWindowProperty[]=L"vinput.self_raw.window_generation.v1";
std::mutex g_selfRawMx;
std::map<ULONG_PTR,SelfRawPacket> g_selfRawPending;
std::atomic<UINT> g_selfRawMessage{0};
std::atomic<bool> g_selfRawReady{false};
std::atomic<unsigned long long> g_selfRawPosted{0},g_selfRawDispatched{0},g_selfRawRejected{0},g_selfRawExpired{0},g_selfRawPostFailed{0};
std::atomic<unsigned long long> g_selfRawCapacityRejected{0},g_selfRawInternalPosted{0},g_selfRawInternalDispatched{0},g_selfRawInternalReads{0},g_selfRawInternalReadFailed{0};
std::atomic<DWORD> g_selfRawLastPostError{0};
struct SelfRawContext {HWND window=nullptr;HRAWINPUT raw=nullptr;bool active=false;const SelfRawPacket* packet=nullptr;};
thread_local SelfRawContext g_selfRawContext;
bool SelfRawBindingCurrent(const SelfRawPacket& packet){
    return !packet.inputBinding||(WndBindingLive(packet.inputBinding)&&WndBindingFind(packet.inputBinding->window)==packet.inputBinding);
}
bool SelfRawContextCurrent(){
    const auto* packet=g_selfRawContext.packet;DWORD owner=0;
    return packet&&SelfRawBindingCurrent(*packet)&&packet->window==g_selfRawContext.window&&WndBindingRawAllowed(packet->window)&&
        GetWindowThreadProcessId(packet->window,&owner)==packet->thread&&owner==g_pid&&
        (ULONG_PTR)GetPropW(packet->window,kSelfRawWindowProperty)==packet->windowStamp;
}

bool SelfRawTrusted(HWND window,HRAWINPUT raw) {
    // A cross-thread SendMessage may re-enter the WndProc during a trusted dispatch.
    // Such a sent message must not borrow the current posted-message provenance.
    return g_selfRawContext.active && SelfRawContextCurrent() && g_selfRawContext.window==window &&
        g_selfRawContext.raw==raw && InSendMessageEx(nullptr)==ISMEX_NOSEND;
}
bool SelfRawTrustedRead(HRAWINPUT raw) {
    return g_selfRawContext.active && SelfRawContextCurrent() && g_selfRawContext.raw==raw &&
        InSendMessageEx(nullptr)==ISMEX_NOSEND;
}
// Matching admitted private provenance must not fall through to a
// foreground/opt-out native decoder after its lifetime is revoked.
bool SelfRawReadRevoked(HRAWINPUT raw){return g_selfRawContext.active&&g_selfRawContext.raw==raw&&!SelfRawContextCurrent();}
struct SelfRawScope {
    SelfRawContext previous;
    explicit SelfRawScope(const SelfRawPacket& packet):previous(g_selfRawContext) {
        g_selfRawContext={packet.window,(HRAWINPUT)packet.l,true,&packet};
    }
    ~SelfRawScope(){g_selfRawContext=previous;}
};
bool SelfRawReadInternal(HRAWINPUT raw,UINT command,LPVOID data,PUINT bytes,UINT header,UINT& result) {
    const auto* packet=g_selfRawContext.packet;
    if(!SelfRawTrustedRead(raw)||!packet||!packet->internal)return false;
    ++g_selfRawInternalReads;
    auto failed=[&](DWORD error){++g_selfRawInternalReadFailed;SetLastError(error);result=(UINT)-1;return true;};
    if(!bytes||header!=sizeof(RAWINPUTHEADER)||(command!=RID_INPUT&&command!=RID_HEADER))return failed(ERROR_INVALID_PARAMETER);
    if(!ReaderBufferWritable(bytes,sizeof(*bytes)))return failed(ERROR_NOACCESS);
    const RAWINPUT& record=packet->record;
    if(record.header.dwSize<sizeof(RAWINPUTHEADER)||record.header.dwSize>sizeof(RAWINPUT))return failed(ERROR_INVALID_DATA);
    const UINT needed=command==RID_HEADER?(UINT)sizeof(RAWINPUTHEADER):record.header.dwSize;
    if(!data){if(!ReaderBufferStore(bytes,&needed,sizeof(needed)))return failed(ERROR_NOACCESS);result=0;return true;}
    UINT capacity=0;if(!ReaderBufferLoad(bytes,&capacity,sizeof(capacity)))return failed(ERROR_NOACCESS);
    if(capacity<needed){if(!ReaderBufferStore(bytes,&needed,sizeof(needed)))return failed(ERROR_NOACCESS);return failed(ERROR_INSUFFICIENT_BUFFER);}
    if(!ReaderBufferStore(data,&record,needed))return failed(ERROR_NOACCESS);result=needed;
    if(command==RID_INPUT&&record.header.dwType==RIM_TYPEKEYBOARD&&record.data.keyboard.VKey<256)
        g_keyTrace[record.data.keyboard.VKey].raw_data.record(record.data.keyboard.Flags,!(record.data.keyboard.Flags&RI_KEY_BREAK));
    return true;
}
bool SelfRawRandom(ULONG_PTR& value) {
    unsigned int low=0;
    if(rand_s(&low)!=0)return false;
    value=(ULONG_PTR)low;
#ifdef _WIN64
    unsigned int high=0;
    if(rand_s(&high)!=0)return false;
    value|=(ULONG_PTR)high<<32;
#endif
    return value!=0;
}
void SelfRawPrune(ULONGLONG now) {
    for(auto it=g_selfRawPending.begin();it!=g_selfRawPending.end();) {
        if(now>=it->second.expires){it=g_selfRawPending.erase(it);++g_selfRawExpired;}
        else ++it;
    }
}
BOOL SelfRawPost(ViPostMessage original,HWND window,UINT message,WPARAM w,LPARAM l,const RAWINPUT* record=nullptr) {
    if(!original){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
    if(message!=WM_INPUT)return original(window,message,w,l);
    if(!RawOwnTarget(window)) {
        if(record){++g_selfRawRejected;g_selfRawLastPostError=ERROR_INVALID_WINDOW_HANDLE;SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
        return original(window,message,w,l);
    }
    if(!WndBindingRawAllowed(window)){WndBindingRecordInputRefusal();++g_selfRawRejected;g_selfRawLastPostError=ERROR_NOT_READY;SetLastError(ERROR_NOT_READY);return FALSE;}
    if(!g_selfRawReady) {
        if(record){++g_selfRawPostFailed;g_selfRawLastPostError=ERROR_NOT_SUPPORTED;SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
        return original(window,message,w,l);
    }
    SelfRawPacket packet;packet.window=window;packet.w=w;packet.l=l;const HWND selected=g_hwnd;packet.inputBinding=WndBindingFind(selected);
    if((selected&&!packet.inputBinding)||!SelfRawBindingCurrent(packet)){WndBindingRecordInputRefusal();++g_selfRawRejected;g_selfRawLastPostError=ERROR_NOT_READY;SetLastError(ERROR_NOT_READY);return FALSE;}
    if(record) {
        if(record->header.dwSize<sizeof(RAWINPUTHEADER)||record->header.dwSize>sizeof(RAWINPUT)){++g_selfRawRejected;g_selfRawLastPostError=ERROR_INVALID_DATA;SetLastError(ERROR_INVALID_DATA);return FALSE;}
        packet.internal=true;packet.record=*record; // immutable payload belongs to this one-shot packet
    }
    DWORD owner=0;packet.thread=GetWindowThreadProcessId(window,&owner);
    if(!packet.thread || owner!=g_pid){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
    ULONG_PTR ticket=0;
    {
        std::lock_guard<std::mutex> lock(g_selfRawMx);
        const ULONGLONG now=HousekeepingNow();SelfRawPrune(now);
        if(g_selfRawPending.size()>=kSelfRawCapacity){++g_selfRawRejected;++g_selfRawCapacityRejected;g_selfRawLastPostError=ERROR_NOT_ENOUGH_QUOTA;SetLastError(ERROR_NOT_ENOUGH_QUOTA);return FALSE;}
        for(unsigned attempt=0;attempt<8;++attempt) {
            if(!SelfRawRandom(ticket) || !SelfRawRandom(packet.proof)){++g_selfRawRejected;SetLastError(ERROR_GEN_FAILURE);return FALSE;}
            if(g_selfRawPending.find(ticket)==g_selfRawPending.end())break;
            ticket=0;
        }
        if(!ticket){++g_selfRawRejected;SetLastError(ERROR_RETRY);return FALSE;}
        packet.windowStamp=(ULONG_PTR)GetPropW(window,kSelfRawWindowProperty);
        if(!packet.windowStamp) {
            if(!SelfRawRandom(packet.windowStamp)){++g_selfRawRejected;SetLastError(ERROR_GEN_FAILURE);return FALSE;}
            if(!SetPropW(window,kSelfRawWindowProperty,(HANDLE)packet.windowStamp)){++g_selfRawRejected;return FALSE;}
        }
        // The property disappears on DestroyWindow, rejecting HWND reuse even on the same thread.
        DWORD ownerAgain=0;
        if(GetWindowThreadProcessId(window,&ownerAgain)!=packet.thread || ownerAgain!=g_pid) {
            ++g_selfRawRejected;SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;
        }
        packet.expires=now+kSelfRawLifetimeMs;g_selfRawPending.emplace(ticket,packet);
    }
    BOOL accepted=original(window,g_selfRawMessage.load(),(WPARAM)ticket,(LPARAM)packet.proof);
    const DWORD error=GetLastError();
    if(accepted){++g_selfRawPosted;if(packet.internal)++g_selfRawInternalPosted;g_selfRawLastPostError=0;}
    else {
        ++g_selfRawPostFailed;g_selfRawLastPostError=error;
        std::lock_guard<std::mutex> lock(g_selfRawMx);
        auto it=g_selfRawPending.find(ticket);
        if(it!=g_selfRawPending.end() && it->second.proof==packet.proof)g_selfRawPending.erase(it);
    }
    SetLastError(error);return accepted;
}
BOOL SelfRawPostRecord(HWND window,const RAWINPUT& record,LPARAM marker) {
    return SelfRawPost(o_SelfPostMessageW,window,WM_INPUT,RIM_INPUT,marker,&record);
}
BOOL WINAPI h_SelfPostMessageA(HWND window,UINT message,WPARAM w,LPARAM l){return SelfRawPost(o_SelfPostMessageA,window,message,w,l);}
BOOL WINAPI h_SelfPostMessageW(HWND window,UINT message,WPARAM w,LPARAM l){return SelfRawPost(o_SelfPostMessageW,window,message,w,l);}
LRESULT SelfRawDispatch(ViDispatchMessage original,const MSG* message) {
    if(!original){SetLastError(ERROR_PROC_NOT_FOUND);return 0;}
    const UINT envelope=g_selfRawMessage;
    if(!message)return original(message);
    MSG copied{};if(!ReaderBufferLoad(message,&copied,sizeof(copied))){SetLastError(ERROR_NOACCESS);return 0;}
    message=&copied;PostedDispatchScope postedScope(copied);
    const bool bindingAllowed=(message->message==WM_INPUT||message->message==envelope)?WndBindingRawAllowed(message->hwnd):WndBindingInputAllowed(message->hwnd);
    if(!bindingAllowed&&(PostedInputMessage(message->message)||message->message==WM_INPUT)){
        WndBindingRecordInputRefusal();if(message->message==WM_INPUT){++C.wm_input;++C.wm_input_dropped;return IsWindowUnicode(message->hwnd)?DefWindowProcW(message->hwnd,WM_INPUT,message->wParam,message->lParam):DefWindowProcA(message->hwnd,WM_INPUT,message->wParam,message->lParam);}return 0;
    }
    if(message->message==WM_INPUT && MouseKeyboardIsolating() && !SelfRawTrusted(message->hwnd,(HRAWINPUT)message->lParam)) {
        // Admission precedes the OUTER host WndProc too: a host that consumes
        // WM_INPUT above our subclass must not receive outside raw messages.
        ++C.wm_input;++C.wm_input_dropped;
        return IsWindowUnicode(message->hwnd)?DefWindowProcW(message->hwnd,WM_INPUT,message->wParam,message->lParam):
            DefWindowProcA(message->hwnd,WM_INPUT,message->wParam,message->lParam);
    }
    if(!envelope || message->message!=envelope)return original(message);
    SelfRawPacket packet;bool admitted=false;
    {
        std::lock_guard<std::mutex> lock(g_selfRawMx);SelfRawPrune(HousekeepingNow());
        auto it=g_selfRawPending.find((ULONG_PTR)message->wParam);
        if(it!=g_selfRawPending.end() && it->second.proof==(ULONG_PTR)message->lParam && it->second.window==message->hwnd) {
            packet=it->second;g_selfRawPending.erase(it); // one attempt, even if the HWND validation fails
            DWORD owner=0;
            admitted=packet.thread==GetCurrentThreadId() &&
                GetWindowThreadProcessId(packet.window,&owner)==packet.thread && owner==g_pid &&
                (ULONG_PTR)GetPropW(packet.window,kSelfRawWindowProperty)==packet.windowStamp;
        }
    }
    if(!admitted){++g_selfRawRejected;return 0;}
    if(!bindingAllowed||!SelfRawBindingCurrent(packet)||!WndBindingRawAllowed(packet.window)){WndBindingRecordInputRefusal();++g_selfRawRejected;return 0;}
    MSG restored=*message;restored.message=WM_INPUT;restored.wParam=packet.w;restored.lParam=packet.l;
    SelfRawScope scope(packet);++g_selfRawDispatched;if(packet.internal)++g_selfRawInternalDispatched;
    // The original dispatcher invokes the current OUTER WndProc with the original host marker.
    return original(&restored);
}
LRESULT WINAPI h_SelfDispatchMessageA(const MSG* message){return SelfRawDispatch(o_SelfDispatchMessageA,message);}
LRESULT WINAPI h_SelfDispatchMessageW(const MSG* message){return SelfRawDispatch(o_SelfDispatchMessageW,message);}
bool InstallSelfRawHooks() {
    g_selfRawMessage=RegisterWindowMessageW(L"vinput.self_raw.envelope.v1");
    const bool envelopeReady=g_selfRawMessage!=0;
    const wchar_t* mod=L"user32.dll";HMODULE user=GetModuleHandleW(mod);
    bool safe=HookApi(mod,"DispatchMessageW",&h_SelfDispatchMessageW,&o_SelfDispatchMessageW);
    if(GetProcAddress(user,"DispatchMessageA")==GetProcAddress(user,"DispatchMessageW"))o_SelfDispatchMessageA=o_SelfDispatchMessageW;
    else safe=HookApi(mod,"DispatchMessageA",&h_SelfDispatchMessageA,&o_SelfDispatchMessageA)&&safe;
    g_postedDispatchReady=safe&&o_SelfDispatchMessageA&&o_SelfDispatchMessageW;
    safe=HookApi(mod,"PostMessageW",&h_SelfPostMessageW,&o_SelfPostMessageW)&&safe;
    if(GetProcAddress(user,"PostMessageA")==GetProcAddress(user,"PostMessageW"))o_SelfPostMessageA=o_SelfPostMessageW;
    else safe=HookApi(mod,"PostMessageA",&h_SelfPostMessageA,&o_SelfPostMessageA)&&safe;
    g_selfRawReady=envelopeReady && safe && o_SelfPostMessageA && o_SelfPostMessageW && o_SelfDispatchMessageA && o_SelfDispatchMessageW;
    return g_selfRawReady;
}
std::string SelfRawStatusJson() {
    size_t pending=0,internalPending=0;{std::lock_guard<std::mutex> lock(g_selfRawMx);SelfRawPrune(HousekeepingNow());pending=g_selfRawPending.size();for(const auto& packet:g_selfRawPending)if(packet.second.internal)++internalPending;}
    return "{\"supported\":"+std::string(g_selfRawReady?"true":"false")+
        ",\"scope\":\"same-process PostMessageA/W through unfiltered DispatchMessageA/W pumps; filters excluding the private envelope ID, SendMessage and direct queue consumers excluded\""+
        ",\"posted\":"+std::to_string(g_selfRawPosted.load())+",\"dispatched\":"+std::to_string(g_selfRawDispatched.load())+
        ",\"rejected\":"+std::to_string(g_selfRawRejected.load())+",\"expired\":"+std::to_string(g_selfRawExpired.load())+
        ",\"post_failed\":"+std::to_string(g_selfRawPostFailed.load())+",\"pending\":"+std::to_string(pending)+
        ",\"capacity_rejected\":"+std::to_string(g_selfRawCapacityRejected.load())+",\"last_post_error\":"+std::to_string(g_selfRawLastPostError.load())+
        ",\"internal_posted\":"+std::to_string(g_selfRawInternalPosted.load())+",\"internal_dispatched\":"+std::to_string(g_selfRawInternalDispatched.load())+
        ",\"internal_reads\":"+std::to_string(g_selfRawInternalReads.load())+",\"internal_read_failed\":"+std::to_string(g_selfRawInternalReadFailed.load())+",\"internal_pending\":"+std::to_string(internalPending)+
        ",\"payload_transport\":\"immutable per-dispatch snapshots for vinput records; original markers delegate for target self posts; no prefix-based decoder trust\""+
        ",\"capacity\":"+std::to_string(kSelfRawCapacity)+",\"lifetime_ms\":"+std::to_string(kSelfRawLifetimeMs)+"}";
}
