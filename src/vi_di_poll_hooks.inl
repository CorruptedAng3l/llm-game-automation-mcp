// Typed code-entry registry for DI8 A/W keyboard and mouse (and legacy aliases).
// Mouse and keyboard implementations are not required to share poll methods.
#define VI_DI_POLL_REGISTRY 1
constexpr unsigned kDiPollSlots=8;
struct DiPollEntry{void* target=nullptr;std::atomic<void*> original{nullptr};bool created=false,enabled=false,busy=false;};
DiPollEntry g_diStateEntries[kDiPollSlots],g_diDataEntries[kDiPollSlots];
std::atomic<unsigned> g_di8ProbeRoles{0};
std::atomic<unsigned long long> g_diPollHookFailures{0};
std::map<HMODULE,bool> g_diPollModules;
bool DiPollEntryKnown(void* target){
    for(unsigned i=0;i<kDiPollSlots;++i)if(g_diStateEntries[i].target==target||g_diDataEntries[i].target==target)return true;
    return false; // caller holds g_claimDiEntryMx
}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiStateEntry(void* self,DWORD cb,LPVOID data){
    const auto original=(PFN_DIGDS)g_diStateEntries[N].original.load(std::memory_order_acquire);
    return original?DIGetDeviceStateImpl(original,self,cb,data):DIERR_NOTINITIALIZED;
}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiDataEntry(void* self,DWORD cb,LPDIDEVICEOBJECTDATA data,LPDWORD count,DWORD flags){
    const auto original=(PFN_DIGDD)g_diDataEntries[N].original.load(std::memory_order_acquire);
    return original?DIGetDeviceDataImpl(original,self,cb,data,count,flags):DIERR_NOTINITIALIZED;
}
#define VI_DI_POLL_ENTRIES(F) (void*)&F<0>,(void*)&F<1>,(void*)&F<2>,(void*)&F<3>,(void*)&F<4>,(void*)&F<5>,(void*)&F<6>,(void*)&F<7>
void* const kDiStateDetours[]={VI_DI_POLL_ENTRIES(DiStateEntry)};
void* const kDiDataDetours[]={VI_DI_POLL_ENTRIES(DiDataEntry)};
#undef VI_DI_POLL_ENTRIES
bool DiPollInstall(void* target,bool buffered){
    if(!target)return false;
    DiPollEntry* slot=nullptr;void* detour=nullptr;bool created=false;
    {
        std::lock_guard<std::mutex> lock(g_claimDiEntryMx);
        auto* entries=buffered?g_diDataEntries:g_diStateEntries;auto* other=buffered?g_diStateEntries:g_diDataEntries;
        for(const auto& e:g_claimDiAcquireEntries)if(e.target==target){++g_diPollHookFailures;return false;}
        for(const auto& e:g_claimDiCoopEntries)if(e.target==target){++g_diPollHookFailures;return false;}
        for(unsigned i=0;i<kDiPollSlots;++i)if(other[i].target==target){++g_diPollHookFailures;return false;}
        for(unsigned i=0;i<kDiPollSlots;++i)if(entries[i].target==target){slot=&entries[i];if(slot->enabled)return true;detour=buffered?kDiDataDetours[i]:kDiStateDetours[i];break;}
        if(!slot)for(unsigned i=0;i<kDiPollSlots;++i)if(!entries[i].target){slot=&entries[i];slot->target=target;detour=buffered?kDiDataDetours[i]:kDiStateDetours[i];break;}
        if(!slot||slot->busy){++g_diPollHookFailures;return false;}
        slot->busy=true;created=slot->created;
    }
    bool ok=false;
    if(!created){
        HMODULE module=nullptr;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)target,&module)){
            {std::lock_guard<std::mutex> lock(g_claimDiEntryMx);if(g_diPollModules.count(module))FreeLibrary(module);else g_diPollModules[module]=true;}
            void* original=nullptr;
            if(VIMH_CreateHook(target,detour,&original)==MH_OK&&original){slot->original.store(original,std::memory_order_release);created=true;}
        }
    }
    if(created){const auto result=VIMH_EnableHook(target);ok=result==MH_OK||result==MH_ERROR_ENABLED;}
    {std::lock_guard<std::mutex> lock(g_claimDiEntryMx);slot->created=created;slot->enabled=ok;slot->busy=false;if(!ok)++g_diPollHookFailures;}
    return ok;
}
bool DiVirtualMouseReady(void* self){
    if(!self)return false;void** table=*(void***)self;bool state=false,buffer=false;
    std::lock_guard<std::mutex> lock(g_claimDiEntryMx);
    for(unsigned i=0;i<kDiPollSlots;++i){
        const auto& s=g_diStateEntries[i];const auto& b=g_diDataEntries[i];
        if(s.enabled&&s.target==table[9]&&CursorEntryReaches(s.target,kDiStateDetours[i]))state=true;
        if(b.enabled&&b.target==table[10]&&CursorEntryReaches(b.target,kDiDataDetours[i]))buffer=true;
    }
    return state&&buffer;
}
