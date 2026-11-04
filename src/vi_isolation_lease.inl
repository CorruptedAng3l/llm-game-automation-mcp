// Keyboard/mouse reader policy. The cursor guard supplies the independent real-FG witness.
// This option does not relax cursor writes or change XInput policy.
#include "vi_reader_memory.inl"
constexpr size_t kReaderScratchLimit=1024*1024;
enum ReaderChannel {ReaderAsyncKey,ReaderKey,ReaderKeyboard,ReaderRawData,ReaderRawBuffer,ReaderDiState,ReaderDiBuffer,ReaderChannels};
std::atomic<unsigned long long> g_readerMasked[ReaderChannels],g_readerPassed[ReaderChannels];
std::atomic<unsigned long long> g_readerMessageKeyDropped{0};
std::atomic<unsigned long long> g_readerDiUnknownDenied{0};
std::mutex g_isolationMx;
ULONGLONG g_isolationDeadline=0;
ULONGLONG g_isolationDisabledAt=0,g_isolationSetAt=0;
unsigned long long g_isolationExpirations=0;
void RefreshIsolationLocked(ULONGLONG now){
    if(!g_cfg.isolate&&g_isolationDeadline&&now>=g_isolationDeadline){
        g_cfg.isolate=true;g_isolationDeadline=0;g_isolationDisabledAt=0;++g_isolationExpirations;
    }
}
bool IsolationEnabled(){
    std::lock_guard<std::mutex> lock(g_isolationMx);
    RefreshIsolationLocked(HousekeepingNow());
    return g_cfg.isolate;
}
void SetIsolation(bool enabled,ULONGLONG duration){
    std::lock_guard<std::mutex> lock(g_isolationMx);
    const auto now=HousekeepingNow();RefreshIsolationLocked(now);g_isolationSetAt=now;
    if(enabled)g_isolationDisabledAt=0;
    else if(g_cfg.isolate||!g_isolationDisabledAt)g_isolationDisabledAt=now;
    g_isolationDeadline=!enabled&&duration?now+duration:0;
    g_cfg.isolate=enabled;
}
bool ReaderMask(unsigned channel){
    const bool mask=IsolationEnabled()&&!PhysicalForegroundOwned();
    if(channel<(unsigned)ReaderChannels){if(mask)++g_readerMasked[channel];else ++g_readerPassed[channel];}
    return mask;
}
// Defined before JOut; construct only fixed-key/counter JSON here.
std::string PhysicalInputStatusJson(bool* isolationSnapshot=nullptr){
    const bool foreground=PhysicalForegroundOwned();
    bool enabled=true;ULONGLONG remaining=0,elapsed=0,setAge=0;bool setKnown=false;unsigned long long expired=0;
    {std::lock_guard<std::mutex> lock(g_isolationMx);const auto now=HousekeepingNow();RefreshIsolationLocked(now);enabled=g_cfg.isolate;remaining=g_isolationDeadline>now?g_isolationDeadline-now:0;expired=g_isolationExpirations;elapsed=!enabled&&g_isolationDisabledAt&&now>=g_isolationDisabledAt?now-g_isolationDisabledAt:0;setKnown=g_isolationSetAt!=0;setAge=setKnown&&now>=g_isolationSetAt?now-g_isolationSetAt:0;}
    if(isolationSnapshot)*isolationSnapshot=enabled;
    std::string out="{\"isolate\":"+std::string(enabled?"true":"false")+",\"foreground_verified_owned\":"+(foreground?"true":"false")+
        ",\"physical_passthrough\":"+(!enabled||foreground?"true":"false")+",\"timed_passthrough\":"+(remaining?"true":"false")+
        ",\"remaining_ms\":"+std::to_string(remaining)+",\"expired_count\":"+std::to_string(expired)+
        ",\"passthrough_elapsed_ms\":"+(enabled?"null":std::to_string(elapsed))+",\"last_explicit_set_age_ms\":"+(setKnown?std::to_string(setAge):"null")+
        ",\"scope\":\"keyboard/mouse/raw/DirectInput; XInput unchanged\",\"warning\":"+
        (enabled?"null":"\"isolate=false explicitly passes physical keyboard/mouse input, including while background\"")+
        ",\"key_messages_dropped\":"+std::to_string(g_readerMessageKeyDropped.load())+",\"di_unknown_denied\":"+std::to_string(g_readerDiUnknownDenied.load())+",\"readers\":{";
    const char* names[]={"async_key","key","keyboard","raw_data","raw_buffer","di_state","di_buffer"};
    for(unsigned i=0;i<(unsigned)ReaderChannels;i++){if(i)out+=',';out+='"';out+=names[i];out+="\":{\"mask_policy_checks\":"+std::to_string(g_readerMasked[i].load())+",\"passthrough_policy_checks\":"+std::to_string(g_readerPassed[i].load())+"}";}
    return out+"}}";
}
