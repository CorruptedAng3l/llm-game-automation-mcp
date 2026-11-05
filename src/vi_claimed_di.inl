// Future-call mouse protection only. Never retains a COM object or calls it on a worker.
// Shared ClaimNativeOperation is defined before this include; its mutex is not held
// across any capability or original COM callback.
std::atomic<unsigned long long> g_claimDiAcquireBlocked{0},g_claimDiUnknownRefused{0},g_claimDiTransitionRefused{0};
std::atomic<unsigned long long> g_claimDiCoopNormalized{0},g_claimDiCoopFailed{0},g_claimDiInvalidFlags{0},g_claimDiInspectionReentry{0};
thread_local unsigned g_claimDiInspectionDepth=0;
int ClaimedDiType(void* self){
 if(g_claimDiInspectionDepth){++g_claimDiInspectionReentry;return -1;}
 struct Inspect {Inspect(){++g_claimDiInspectionDepth;}~Inspect(){--g_claimDiInspectionDepth;}} inspect;
 return DevType(self); // current-call borrowed self, fresh capabilities; no pointer cache
}
// A denied reservation with no current live claim is a transition/contention,
// not authority to call the native mouse operation after a stale false sample.
int ClaimedDiProtectedType(void* self){
 if(!AgentClaimLive()){++g_claimDiTransitionRefused;return -2;}
 return ClaimedDiType(self);
}
using PFN_DICOOP=HRESULT(STDMETHODCALLTYPE*)(void*,HWND,DWORD);
HRESULT ClaimedDiSetCoopImpl(PFN_DICOOP original,void* self,HWND window,DWORD flags){
 ClaimNativeOperation operation;
 if(operation)return original(self,window,flags); // unclaimed: exact arguments/result, no extra GetCaps
 const int type=ClaimedDiProtectedType(self);
 if(type==-2)return DIERR_OTHERAPPHASPRIO;
 if(type<0){++g_claimDiUnknownRefused;return DIERR_NOTINITIALIZED;}
 if(type!=2)return original(self,window,flags); // proven keyboard/other device, unchanged
 const DWORD exclusive=flags&(DISCL_EXCLUSIVE|DISCL_NONEXCLUSIVE);
 const DWORD foreground=flags&(DISCL_FOREGROUND|DISCL_BACKGROUND);
 if((exclusive!=DISCL_EXCLUSIVE&&exclusive!=DISCL_NONEXCLUSIVE)||
    (foreground!=DISCL_FOREGROUND&&foreground!=DISCL_BACKGROUND)||
    (exclusive==DISCL_EXCLUSIVE&&foreground==DISCL_BACKGROUND)||
    (flags&~(DISCL_EXCLUSIVE|DISCL_NONEXCLUSIVE|DISCL_FOREGROUND|DISCL_BACKGROUND))){
  ++g_claimDiInvalidFlags;return DIERR_INVALIDPARAM; // NOWINKEY is keyboard-only
 }
 const DWORD safe=(flags&~DISCL_EXCLUSIVE)|DISCL_NONEXCLUSIVE;
 if(safe!=flags)++g_claimDiCoopNormalized;
 const HRESULT result=original(self,window,safe);
 if(FAILED(result))++g_claimDiCoopFailed;
 return result; // an acquired device can reject this; never silently Unacquire or fake success
}
