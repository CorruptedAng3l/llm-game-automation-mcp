// Actual DI8 factory/device discovery. No device rights or polling are requested
// here; originals execute outside the code-entry registry mutex.
#define VI_DI_DISCOVERY 1
enum DiDiscoveryKind { DiQuery=0,DiCreate=1,DiSetFormat=2,DiRelease=3,DiActionMap=4,DiDiscoveryKinds=5 };
constexpr unsigned kDiDiscoverySlots=16;
struct DiDiscoveryEntry {void* target=nullptr;std::atomic<void*> original{nullptr};bool created=false,enabled=false,busy=false;};
std::mutex g_diDiscoveryMx;DiDiscoveryEntry g_diDiscovery[DiDiscoveryKinds][kDiDiscoverySlots];std::map<HMODULE,bool> g_diDiscoveryModules;
std::atomic<unsigned long long> g_diFactoryReturns{0},g_diDeviceReturns{0},g_diQueriedDevices{0},g_diDiscoveryFailures{0},g_diProbeFactoryReturns{0};
thread_local unsigned g_diDiscoveryProbeDepth=0;
struct DiDiscoveryProbeScope{DiDiscoveryProbeScope(){++g_diDiscoveryProbeDepth;}~DiDiscoveryProbeScope(){--g_diDiscoveryProbeDepth;}};
using DiQueryFn=HRESULT(STDMETHODCALLTYPE*)(void*,REFIID,void**);
using DiCreateFn=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,void**,LPUNKNOWN);
using DiFactoryFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);
std::atomic<void*> g_diFactoryTarget{nullptr},g_diFactoryOriginal{nullptr};std::atomic<HMODULE> g_diFactoryModule{nullptr};std::atomic<bool> g_diFactoryEnabled{false};bool g_diFactoryBusy=false;
bool DiObserveOwner(void*);bool DiObserveDevice(void*,bool=false);bool DiDiscoveryInstall(void*,DiDiscoveryKind);
bool DiDeviceIid(REFIID iid){return IsEqualIID(iid,IID_IDirectInputDeviceA)||IsEqualIID(iid,IID_IDirectInputDeviceW)||IsEqualIID(iid,IID_IDirectInputDevice2A)||IsEqualIID(iid,IID_IDirectInputDevice2W)||IsEqualIID(iid,IID_IDirectInputDevice7A)||IsEqualIID(iid,IID_IDirectInputDevice7W)||IsEqualIID(iid,IID_IDirectInputDevice8A)||IsEqualIID(iid,IID_IDirectInputDevice8W);}
bool DiOwnerIid(REFIID iid){return IsEqualIID(iid,IID_IDirectInput8A)||IsEqualIID(iid,IID_IDirectInput8W);}
void DiDiscoveryKeep(HMODULE module){bool duplicate=false;{std::lock_guard<std::mutex> lock(g_diDiscoveryMx);duplicate=g_diDiscoveryModules.count(module)!=0;if(!duplicate)g_diDiscoveryModules[module]=true;}if(duplicate)FreeLibrary(module);}
bool DiDiscoveryPin(void* target){HMODULE module=nullptr;if(!target||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)target,&module))return false;DiDiscoveryKeep(module);return true;}
HRESULT DiDiscoveryPublish(void** out,bool guarded,HRESULT native){
    if(guarded)return native;
    // The native call granted exactly this reference. Publishing a device whose
    // Acquire/coop entries are unguarded would create a new native-rights gap.
    ++g_diDiscoveryFailures;ClaimDiReleaseBorrowed(*out);*out=nullptr;return DIERR_NOTINITIALIZED;
}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiQueryEntry(void* self,REFIID iid,void** out){
    const auto original=(DiQueryFn)g_diDiscovery[DiQuery][N].original.load(std::memory_order_acquire);if(!original)return DIERR_NOTINITIALIZED;
    const HRESULT hr=original(self,iid,out);if(FAILED(hr)||!out||!*out)return hr;
    if(DiOwnerIid(iid))return DiDiscoveryPublish(out,DiObserveOwner(*out),hr);
    if(DiDeviceIid(iid)){if(!g_diDiscoveryProbeDepth)++g_diQueriedDevices;const bool guarded=DiObserveDevice(*out,IsEqualIID(iid,IID_IDirectInputDevice8A)||IsEqualIID(iid,IID_IDirectInputDevice8W));const bool probeOnly=g_diDiscoveryProbeDepth&&!DiFormatFind(self)&&!DiFormatFind(*out);return DiDiscoveryPublish(out,guarded&&(probeOnly||DiFormatLink(self,*out)),hr);}
    if(DiFormatFind(self))return DiDiscoveryPublish(out,DiFormatLink(self,*out),hr); // Universal IUnknown slots only; no unknown device layout is assumed.
    if(IsEqualIID(iid,IID_IUnknown))return DiDiscoveryPublish(out,DiDiscoveryInstall((*(void***)*out)[0],DiQuery),hr);
    return hr;
}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiCreateEntry(void* self,REFGUID guid,void** out,LPUNKNOWN outer){
    const auto original=(DiCreateFn)g_diDiscovery[DiCreate][N].original.load(std::memory_order_acquire);if(!original)return DIERR_NOTINITIALIZED;
    const HRESULT hr=original(self,guid,out,outer);if(FAILED(hr)||!out||!*out)return hr;
    if(!g_diDiscoveryProbeDepth)++g_diDeviceReturns;
    return DiDiscoveryPublish(out,DiObserveDevice(*out,true),hr);
}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiSetFormatEntry(void* self,LPCDIDATAFORMAT format){const auto original=(DiSetFormatFn)g_diDiscovery[DiSetFormat][N].original.load(std::memory_order_acquire);return original?DiSetFormatCall(original,self,format):DIERR_NOTINITIALIZED;}
template<unsigned N>ULONG STDMETHODCALLTYPE DiReleaseEntry(void* self){const auto original=(DiReleaseFn)g_diDiscovery[DiRelease][N].original.load(std::memory_order_acquire);if(!original)return 0;auto lifetime=DiFormatFind(self);const ULONG left=original(self);if(!left&&lifetime)DiFormatRetire(lifetime);return left;}
template<unsigned N>HRESULT STDMETHODCALLTYPE DiActionMapEntry(void* self,void* format,const void* name,DWORD flags){const auto original=(DiActionMapFn)g_diDiscovery[DiActionMap][N].original.load(std::memory_order_acquire);return original?DiSetActionMapCall(original,self,format,name,flags):DIERR_NOTINITIALIZED;}
#define VI_DI_DISCOVERY_ENTRIES(F) (void*)&F<0>,(void*)&F<1>,(void*)&F<2>,(void*)&F<3>,(void*)&F<4>,(void*)&F<5>,(void*)&F<6>,(void*)&F<7>,(void*)&F<8>,(void*)&F<9>,(void*)&F<10>,(void*)&F<11>,(void*)&F<12>,(void*)&F<13>,(void*)&F<14>,(void*)&F<15>
void* const kDiQueryEntries[]={VI_DI_DISCOVERY_ENTRIES(DiQueryEntry)};void* const kDiCreateEntries[]={VI_DI_DISCOVERY_ENTRIES(DiCreateEntry)};
void* const kDiFormatEntries[]={VI_DI_DISCOVERY_ENTRIES(DiSetFormatEntry)};void* const kDiReleaseEntries[]={VI_DI_DISCOVERY_ENTRIES(DiReleaseEntry)};void* const kDiActionEntries[]={VI_DI_DISCOVERY_ENTRIES(DiActionMapEntry)};
void* const* const kDiDiscoveryDetours[]={kDiQueryEntries,kDiCreateEntries,kDiFormatEntries,kDiReleaseEntries,kDiActionEntries};
#undef VI_DI_DISCOVERY_ENTRIES
bool DiDiscoveryInstall(void* target,DiDiscoveryKind kind){
    if(!target)return false;
    {std::lock_guard<std::mutex> lock(g_claimDiEntryMx);if(DiPollEntryKnown(target))return false;for(const auto& e:g_claimDiAcquireEntries)if(e.target==target)return false;for(const auto& e:g_claimDiCoopEntries)if(e.target==target)return false;}
    DiDiscoveryEntry* slot=nullptr;void* detour=nullptr;bool created=false;
    {std::lock_guard<std::mutex> lock(g_diDiscoveryMx);
        for(unsigned k=0;k<DiDiscoveryKinds;++k)if(k!=(unsigned)kind)for(const auto& e:g_diDiscovery[k])if(e.target==target)return false;
        for(unsigned i=0;i<kDiDiscoverySlots;++i)if(g_diDiscovery[kind][i].target==target){slot=&g_diDiscovery[kind][i];detour=kDiDiscoveryDetours[kind][i];if(slot->enabled)return CursorEntryReaches(target,detour);break;}
        if(!slot)for(unsigned i=0;i<kDiDiscoverySlots;++i)if(!g_diDiscovery[kind][i].target){slot=&g_diDiscovery[kind][i];slot->target=target;detour=kDiDiscoveryDetours[kind][i];break;}
        if(!slot||slot->busy)return false;slot->busy=true;created=slot->created;
    }
    bool ok=false;if(!created&&DiDiscoveryPin(target)){void* original=nullptr;if(VIMH_CreateHook(target,detour,&original)==MH_OK&&original){slot->original.store(original,std::memory_order_release);created=true;}}
    if(created){const auto hr=VIMH_EnableHook(target);ok=(hr==MH_OK||hr==MH_ERROR_ENABLED)&&CursorEntryReaches(target,detour);}
    {std::lock_guard<std::mutex> lock(g_diDiscoveryMx);slot->created=created;slot->enabled=ok;slot->busy=false;}
    if(!ok)++g_diDiscoveryFailures;return ok;
}
bool DiObserveOwner(void* owner){void** table=*(void***)owner;const bool query=DiDiscoveryInstall(table[0],DiQuery),create=DiDiscoveryInstall(table[3],DiCreate);return query&&create;}
bool DiObserveDevice(void* device,bool device8){
    void** table=*(void***)device;
    const bool acquire=ClaimDiInstallEntry(table[7],false),coop=ClaimDiInstallEntry(table[13],true);
    const bool state=DiPollInstall(table[9],false),buffer=DiPollInstall(table[10],true),query=DiDiscoveryInstall(table[0],DiQuery);
    const bool format=DiDiscoveryInstall(table[11],DiSetFormat),release=DiDiscoveryInstall(table[2],DiRelease),action=!device8||DiDiscoveryInstall(table[30],DiActionMap);
    if(!state||!buffer||!query||!format||!release||!action)++g_diDiscoveryFailures;
    // A native factory may return a cached pre-existing COM identity. Probes
    // establish code coverage only; an unseen layout must retain legacy fallback.
    const bool probeOnly=g_diDiscoveryProbeDepth&&!DiFormatFind(device);
    return acquire&&coop&&query&&format&&release&&action&&(probeOnly||DiFormatEnsure(device));
}
HRESULT WINAPI DiFactoryEntry(HINSTANCE instance,DWORD version,REFIID iid,void** out,LPUNKNOWN outer){
    const auto original=(DiFactoryFn)g_diFactoryOriginal.load(std::memory_order_acquire);const HMODULE expected=g_diFactoryModule.load();HMODULE held=nullptr;
    const auto target=g_diFactoryTarget.load();
    if(!original||!expected||GetModuleHandleW(L"dinput8.dll")!=expected||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)expected,&held)){++g_diDiscoveryFailures;return DIERR_NOTINITIALIZED;}
    if(held!=expected||(void*)GetProcAddress(held,"DirectInput8Create")!=target||!CursorEntryReaches(target,(void*)&DiFactoryEntry)){FreeLibrary(held);++g_diDiscoveryFailures;return DIERR_NOTINITIALIZED;}
    DiDiscoveryKeep(held);if(!DiDiscoveryPin(target)){++g_diDiscoveryFailures;return DIERR_NOTINITIALIZED;}
    const HRESULT hr=original(instance,version,iid,out,outer);if(FAILED(hr)||!out||!*out)return hr;
    if(g_diDiscoveryProbeDepth)++g_diProbeFactoryReturns;else ++g_diFactoryReturns;
    if(DiOwnerIid(iid))return DiDiscoveryPublish(out,DiObserveOwner(*out),hr);
    if(IsEqualIID(iid,IID_IUnknown))return DiDiscoveryPublish(out,DiDiscoveryInstall((*(void***)*out)[0],DiQuery),hr);
    return hr;
}
// Loader-notification safe: only export lookup and MinHook, no COM or module retain.
void TryDi8Factory(HMODULE module=nullptr){
    if(g_diFactoryEnabled)return;if(!module)module=GetModuleHandleW(L"dinput8.dll");if(!module)return;
    {std::lock_guard<std::mutex> lock(g_diDiscoveryMx);if(g_diFactoryEnabled||g_diFactoryBusy)return;g_diFactoryBusy=true;}
    void* target=(void*)GetProcAddress(module,"DirectInput8Create");bool ok=false;
    if(target){void* original=nullptr;const auto hr=VIMH_CreateHook(target,(void*)&DiFactoryEntry,&original);
        if(hr==MH_OK&&original){g_diFactoryModule=module;g_diFactoryTarget=target;g_diFactoryOriginal.store(original,std::memory_order_release);const auto enabled=VIMH_EnableHook(target);ok=enabled==MH_OK||enabled==MH_ERROR_ENABLED;}}
    {std::lock_guard<std::mutex> lock(g_diDiscoveryMx);g_diFactoryEnabled=ok;g_diFactoryBusy=false;}
    if(!ok)++g_diDiscoveryFailures;
}
