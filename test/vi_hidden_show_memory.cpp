// Production queued-show adapter, memory-only dependencies. No native calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <cstdio>
struct CaptureScope{HWND window=nullptr;unsigned long long adoption=0;};struct WndProcBinding{DWORD thread=8;};
static HWND g_hwnd=(HWND)1;static unsigned long long g_inputReadyGeneration=0;static std::recursive_mutex g_inputMx;static ULONGLONG clockMs=100;
static std::atomic<const char*> g_acProduct{nullptr};static std::atomic<unsigned long long> g_jobClaimEpoch{1},g_ownerSeq{0};static std::atomic<ULONGLONG> g_jobClaimExpiry{10000};static bool g_ownerHolding=false,g_adoptionActivationSafe=true;
static thread_local unsigned g_explicitNoActivation=0;static bool human=false,foreground=false,scopeValid=true,bindingValid=true,barrierValid=true,visible=false,showMakesVisible=false,showSeesStrictBarrier=false,showChangesGeometry=false,showRetargets=false;
static unsigned calls=0;static int command=0;static HWND shown=nullptr;static auto binding=std::make_shared<WndProcBinding>();
static ULONGLONG HousekeepingNow(){return clockMs;}static bool AdoptionHumanSelectionPending(){return human;}static bool ComputeRealFg(){return foreground;}
static bool CaptureScopeCurrent(const CaptureScope&){return scopeValid;}static bool WndBindingLive(const std::shared_ptr<WndProcBinding>& b){return bindingValid&&b==binding;}static std::shared_ptr<WndProcBinding> WndBindingFind(HWND){return binding;}
static DWORD FThread(){return 8;}static BOOL FVisible(HWND){return visible;}static BOOL WINAPI FShow(HWND h,int cmd){++calls;shown=h;command=cmd;showSeesStrictBarrier=g_explicitNoActivation>0;if(showMakesVisible)visible=true;if(showChangesGeometry)scopeValid=false;if(showRetargets)g_hwnd=(HWND)2;return FALSE;}
static auto o_ShowWindow=&FShow;struct ScopedAdoptionActivationBarrier{HHOOK hook=barrierValid?(HHOOK)1:nullptr;};
#define GetCurrentThreadId FThread
#define IsWindowVisible FVisible
#include "../src/vi_hidden_show.inl"
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};auto reset=[](){clockMs=100;g_hiddenShow={};g_hiddenShow.ticket=1;g_hiddenShow.pending=true;g_hiddenShow.scope.window=(HWND)1;g_hiddenShow.binding=binding;g_hiddenShow.claimEpoch=1;g_hiddenShow.ownerSeq=0;g_hiddenShow.expires=5100;g_jobClaimEpoch=1;g_ownerSeq=0;g_jobClaimExpiry=10000;g_ownerHolding=false;g_acProduct=nullptr;human=foreground=false;scopeValid=bindingValid=barrierValid=g_adoptionActivationSafe=true;visible=showMakesVisible=showSeesStrictBarrier=showChangesGeometry=showRetargets=false;g_hwnd=(HWND)1;g_inputReadyGeneration=0;calls=0;o_ShowWindow=&FShow;};auto dispatch=[](){HiddenShowDispatch((HWND)1,1,(LPARAM)kHiddenShowCookie,binding);};
 reset();dispatch();check(calls==1&&shown==(HWND)1&&command==SW_SHOWNA&&showSeesStrictBarrier&&g_explicitNoActivation==0,"exact once ShowNA under strict thread fence, no retained thread state");check(g_hiddenShow.completed&&!g_hiddenShow.pending&&g_hiddenShow.callAttempted&&!g_hiddenShow.visibleAfter&&g_hiddenShow.reason=="visibility_unconfirmed_after_dispatch","native Show BOOL is never treated as visibility success");dispatch();check(calls==1,"duplicate queue dispatch never replays");
 reset();showMakesVisible=true;dispatch();check(g_hiddenShow.visibleAfter&&g_hiddenShow.reason=="observed_visible_after_dispatch","visibility success requires independent post-dispatch observation");
 reset();showMakesVisible=showChangesGeometry=true;dispatch();check(g_hiddenShow.visibleAfter,"own native show geometry notifications do not erase actual same-lifetime visibility observation");reset();showMakesVisible=showRetargets=true;dispatch();check(!g_hiddenShow.visibleAfter,"retarget during native callback cannot publish old target visibility as current");
 for(unsigned kind=0;kind<12;++kind){reset();if(kind==0)clockMs=5100;if(kind==1)g_acProduct="test";if(kind==2)g_jobClaimEpoch=2;if(kind==3){g_hiddenShow.claimed=true;g_jobClaimExpiry=100;}if(kind==4)g_ownerSeq=1;if(kind==5)g_ownerHolding=true;if(kind==6)human=true;if(kind==7)foreground=true;if(kind==8)scopeValid=false;if(kind==9)bindingValid=false;if(kind==10)g_adoptionActivationSafe=false;if(kind==11)barrierValid=false;dispatch();check(calls==0&&!g_hiddenShow.pending&&g_hiddenShow.completed,("stale/ownership/safety refusal "+std::to_string(kind)).c_str());}
 reset();HiddenShowDispatch((HWND)1,1,0,binding);check(!calls&&g_hiddenShow.pending,"foreign private cookie does not consume queued ticket");HiddenShowDispatch((HWND)1,2,(LPARAM)kHiddenShowCookie,binding);check(!calls&&g_hiddenShow.pending,"other ticket cannot consume queued request");HiddenShowDispatch((HWND)2,1,(LPARAM)kHiddenShowCookie,binding);check(!calls&&!g_hiddenShow.pending,"wrong target refuses without a call");
 reset();clockMs=5100;HiddenShowExpireLocked();check(!g_hiddenShow.pending&&g_hiddenShow.reason=="expired_without_dispatch"&&!g_hiddenShow.callAttempted,"status-only expiration prevents delayed UI show");reset();clockMs=5100;g_hiddenShow.callAttempted=true;HiddenShowExpireLocked();check(g_hiddenShow.pending,"inflight native callback is not mislabeled as undispatched expiration");
 printf("HIDDEN_SHOW_MEMORY_RESULT %u %u\n",pass,fail);return fail?1:0;}
