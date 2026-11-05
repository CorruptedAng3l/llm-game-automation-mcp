// Code-entry registry, never a COM object registry. Callback originals are atomics;
// all COM probes, MinHook calls and original callbacks run outside this mutex.
constexpr unsigned kClaimDiEntries=16;
struct ClaimDiEntry {
 void* target=nullptr;std::atomic<void*> original{nullptr};bool created=false,enabled=false,busy=false;
};
ClaimDiEntry g_claimDiAcquireEntries[kClaimDiEntries],g_claimDiCoopEntries[kClaimDiEntries];
std::mutex g_claimDiEntryMx;
std::atomic<unsigned> g_claimDiAcquireCovered{0},g_claimDiCoopCovered{0},g_claimDiProbeTried{0},g_claimDiProbeCovered{0};
std::atomic<unsigned long long> g_claimDiHookFailed{0},g_claimDiHookBusy{0},g_claimDiProbeFailed{0};
template<unsigned Slot> HRESULT STDMETHODCALLTYPE ClaimDiAcquireEntry(void* self){
 auto original=(PFN_DIACQ)g_claimDiAcquireEntries[Slot].original.load(std::memory_order_acquire);
 return original?DIAcquireImpl(original,self):DIERR_NOTINITIALIZED;
}
template<unsigned Slot> HRESULT STDMETHODCALLTYPE ClaimDiCoopEntry(void* self,HWND window,DWORD flags){
 auto original=(PFN_DICOOP)g_claimDiCoopEntries[Slot].original.load(std::memory_order_acquire);
 return original?ClaimedDiSetCoopImpl(original,self,window,flags):DIERR_NOTINITIALIZED;
}
#define VI_CLAIM_DI_ENTRIES(F) (void*)&F<0>,(void*)&F<1>,(void*)&F<2>,(void*)&F<3>,(void*)&F<4>,(void*)&F<5>,(void*)&F<6>,(void*)&F<7>,(void*)&F<8>,(void*)&F<9>,(void*)&F<10>,(void*)&F<11>,(void*)&F<12>,(void*)&F<13>,(void*)&F<14>,(void*)&F<15>
void* const kClaimDiAcquireDetours[kClaimDiEntries]={VI_CLAIM_DI_ENTRIES(ClaimDiAcquireEntry)};
void* const kClaimDiCoopDetours[kClaimDiEntries]={VI_CLAIM_DI_ENTRIES(ClaimDiCoopEntry)};
#undef VI_CLAIM_DI_ENTRIES
bool ClaimDiKnownAcquire(void* target){
 std::lock_guard<std::mutex> lock(g_claimDiEntryMx);
 for(const auto& entry:g_claimDiAcquireEntries)if(entry.target==target&&entry.enabled)return true;
 return false;
}
bool ClaimDiRememberAcquire(void* target,PFN_DIACQ original){
 if(!target||!original){++g_claimDiHookFailed;return false;}
 std::lock_guard<std::mutex> lock(g_claimDiEntryMx);
 for(const auto& entry:g_claimDiCoopEntries)if(entry.target==target){++g_claimDiHookFailed;return false;}
 for(const auto& entry:g_claimDiAcquireEntries)if(entry.target==target)return entry.enabled;
 for(auto& entry:g_claimDiAcquireEntries)if(!entry.target){
  entry.target=target;entry.original.store((void*)original,std::memory_order_release);entry.enabled=true;++g_claimDiAcquireCovered;return true;
 }
 ++g_claimDiHookFailed;return false;
}
bool ClaimDiHookKnownAcquire(const char* name,void* target,void* detour,PFN_DIACQ* original){
 if(ClaimDiKnownAcquire(target))return true; // exact typed entry already routes to this guard
 if(!HookAddr(name,target,detour,(void**)original)||!*original){++g_claimDiHookFailed;return false;}
 return ClaimDiRememberAcquire(target,*original); // an unrelated ALREADY_CREATED with null original is not proof
}
bool ClaimDiInstallEntry(void* target,bool cooperative){
 if(!target){++g_claimDiHookFailed;return false;}
 ClaimDiEntry* slot=nullptr;void* detour=nullptr;bool created=false;
 {
  std::lock_guard<std::mutex> lock(g_claimDiEntryMx);
  auto* entries=cooperative?g_claimDiCoopEntries:g_claimDiAcquireEntries;
  auto* other=cooperative?g_claimDiAcquireEntries:g_claimDiCoopEntries;
  for(unsigned i=0;i<kClaimDiEntries;i++)if(other[i].target==target){++g_claimDiHookFailed;return false;}
  for(unsigned i=0;i<kClaimDiEntries;i++)if(entries[i].target==target){slot=&entries[i];if(slot->enabled)return true;detour=cooperative?kClaimDiCoopDetours[i]:kClaimDiAcquireDetours[i];break;}
  if(!slot)for(unsigned i=0;i<kClaimDiEntries;i++)if(!entries[i].target){slot=&entries[i];slot->target=target;detour=cooperative?kClaimDiCoopDetours[i]:kClaimDiAcquireDetours[i];break;}
  if(!slot){++g_claimDiHookFailed;return false;}
  if(slot->busy){++g_claimDiHookBusy;return false;}
  slot->busy=true;created=slot->created;
 }
 bool ok=false;
 if(!created){
  void* original=nullptr;const MH_STATUS result=VIMH_CreateHook(target,detour,&original);
  if(result==MH_OK&&original){slot->original.store(original,std::memory_order_release);created=true;}
  // An unrelated already-created hook is not an alias/coverage success.
 }
 if(created){const MH_STATUS result=VIMH_EnableHook(target);ok=result==MH_OK||result==MH_ERROR_ENABLED;}
 {
  std::lock_guard<std::mutex> lock(g_claimDiEntryMx);slot->created=created;slot->busy=false;
  if(ok&&!slot->enabled){slot->enabled=true;if(cooperative)++g_claimDiCoopCovered;else ++g_claimDiAcquireCovered;}
  if(!ok)++g_claimDiHookFailed;
 }
 return ok;
}
using PFN_ClaimDiCreateDevice=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,void**,LPUNKNOWN);
using PFN_ClaimDiRelease=ULONG(STDMETHODCALLTYPE*)(void*);
void ClaimDiReleaseBorrowed(void* object){if(object)((PFN_ClaimDiRelease)(*(void***)object)[2])(object);}
void ClaimDiProbeMouse(void* owner,unsigned bit){
 void* device=nullptr;
 const HRESULT result=((PFN_ClaimDiCreateDevice)(*(void***)owner)[3])(owner,GUID_SysMouse,&device,nullptr);
 if(SUCCEEDED(result)&&device){
  void** table=*(void***)device;
  const bool acquire=ClaimDiInstallEntry(table[7],false),cooperative=ClaimDiInstallEntry(table[13],true);
  if(acquire&&cooperative)g_claimDiProbeCovered.fetch_or(bit);else ++g_claimDiProbeFailed;
 }else ++g_claimDiProbeFailed;
 ClaimDiReleaseBorrowed(device);
}
void TryClaimedDi8(){
 HMODULE module=GetModuleHandleW(L"dinput8.dll");if(!module)return;
 static std::atomic<bool> attempted{false};if(attempted.exchange(true))return;
 using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);
 auto create=(Create)GetProcAddress(module,"DirectInput8Create");
 for(unsigned wide=0;wide<2;wide++){
  const unsigned bit=1u<<wide;g_claimDiProbeTried.fetch_or(bit);void* owner=nullptr;
  const HRESULT result=create?create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,wide?IID_IDirectInput8W:IID_IDirectInput8A,&owner,nullptr):E_NOINTERFACE;
  if(SUCCEEDED(result)&&owner)ClaimDiProbeMouse(owner,bit);else ++g_claimDiProbeFailed;
  ClaimDiReleaseBorrowed(owner);
 }
}
void TryClaimedDiLegacy(){
 HMODULE module=GetModuleHandleW(L"dinput.dll");if(!module)return;
 static std::atomic<bool> attempted{false};if(attempted.exchange(true))return;
 using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,void**,LPUNKNOWN);
 for(unsigned wide=0;wide<2;wide++){
  const unsigned bit=4u<<wide;g_claimDiProbeTried.fetch_or(bit);void* owner=nullptr;
  auto create=(Create)GetProcAddress(module,wide?"DirectInputCreateW":"DirectInputCreateA");
  const HRESULT result=create?create(GetModuleHandleW(nullptr),0x0300,&owner,nullptr):E_NOINTERFACE;
  if(SUCCEEDED(result)&&owner)ClaimDiProbeMouse(owner,bit);else ++g_claimDiProbeFailed;
  ClaimDiReleaseBorrowed(owner);
 }
}
