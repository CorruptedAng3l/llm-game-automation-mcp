// Pure callback model: no Windows headers or desktop/input/device API imports.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <type_traits>
#define WINAPI __stdcall
using BYTE=unsigned char;using DWORD=unsigned long;using ULONG_PTR=std::uintptr_t;using HMODULE=void*;
constexpr DWORD ERROR_ACCESS_DENIED=5,ERROR_BUSY=170,ERROR_PROC_NOT_FOUND=127;
thread_local DWORD lastError=0;
DWORD GetLastError(){return lastError;}void SetLastError(DWORD value){lastError=value;}
#include "vi_cursor_guard.h"
std::uint64_t modelNow=1000;
std::atomic<std::uint64_t> g_jobClaimExpiry{0};
std::uint64_t HousekeepingNow(){return modelNow;}
#include "vi_claim_native_fence.inl"
vi_cursor_guard::Counts g_cursorCounts[vi_cursor_guard::ApiCount];
vi_cursor_guard::Owner owner;
std::function<void()> beforeReserve,lowerAction;
bool probeReentry=false;unsigned probeDepth=0,maxProbeDepth=0;void PermissionReentry();
bool CursorNativeAllowed(){owner.agentClaimLive=AgentClaimLive();bool allowed=vi_cursor_guard::NativeWriteAllowed(owner);if(probeReentry)PermissionReentry();SetLastError(9001);if(beforeReserve){auto f=beforeReserve;beforeReserve={};f();}return allowed;}
unsigned lowerCalls=0;BYTE gotKey=0,gotScan=0;DWORD gotFlags=0,entryError=0;ULONG_PTR gotExtra=0;bool claimAttempt=false,claimDenied=false;
void WINAPI Lower(BYTE key,BYTE scan,DWORD flags,ULONG_PTR extra){++lowerCalls;gotKey=key;gotScan=scan;gotFlags=flags;gotExtra=extra;entryError=GetLastError();if(lowerAction)lowerAction();SetLastError(7331);}
#if HAS_KEYBD
#include "vi_keybd_event_guard.inl"
#else
using PFN_KeybdEvent=void(WINAPI*)(BYTE,BYTE,DWORD,ULONG_PTR);
PFN_KeybdEvent o_keybd_event=Lower;
#endif
static_assert(std::is_same_v<PFN_KeybdEvent,void(WINAPI*)(BYTE,BYTE,DWORD,ULONG_PTR)>);
std::atomic<bool> g_cursorInstallFinished{false};
void* g_cursorEntries[vi_cursor_guard::ApiCount]={};void* g_cursorDetours[vi_cursor_guard::ApiCount]={};bool g_cursorInstalled[vi_cursor_guard::ApiCount]={};
int tokens[10]{};bool keyExport=true,failKeyHook=false,aliasKey=false,verifyKey=true,positionAliases=false;unsigned mutations[10]={};
int named(const char* name){const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","GetClipCursor","GetCursorPos","GetPhysicalCursorPos","keybd_event"};for(int i=0;i<9;++i)if(!std::strcmp(name,names[i]))return i;return -1;}
int keyApi(){for(unsigned i=0;i<vi_cursor_guard::ApiCount;++i)if(!std::strcmp(vi_cursor_guard::Name(i),"keybd_event"))return static_cast<int>(i);return -1;}
HMODULE GetModuleHandleW(const wchar_t*){return &tokens[9];}
void* GetProcAddress(HMODULE,const char* name){const int i=named(name);if(i<0||(i==8&&!keyExport))return nullptr;return &tokens[i==8&&aliasKey?4:positionAliases&&i==1?0:positionAliases&&i==7?6:i];}
void WINAPI Stub(){}
using StubPointer=void(WINAPI*)();
StubPointer o_SetCursorPos=nullptr,o_SetPhysicalCursorPos=nullptr,o_ClipCursor=nullptr,o_SendInput=nullptr,o_mouse_event=nullptr,o_GetClipCursor=nullptr,o_GetCursorPos=nullptr,o_GetPhysicalCursorPos=nullptr;
void WINAPI h_SetCursorPos(){}void WINAPI h_SetPhysicalCursorPos(){}void WINAPI h_ClipCursor(){}void WINAPI h_SendInput(){}void WINAPI h_mouse_event(){}void WINAPI h_GetClipCursor(){}void WINAPI h_GetCursorPos(){}void WINAPI h_GetPhysicalCursorPos(){}
bool HookAddr(const char* name,void* entry,void*,void** original){const int i=named(name);for(int at=0;at<10;at++)if(entry==&tokens[at])++mutations[at];if(i==8&&failKeyHook)return false;*original=i==8?reinterpret_cast<void*>(&Lower):reinterpret_cast<void*>(&Stub);return true;}
bool CursorEntryReaches(void* entry,void*){return entry!=&tokens[8]||verifyKey;}
#include "readiness.inl"
#include "vi_cursor_install.inl"
bool AllReady(){
#if HAS_KEYBD
 return CursorInputProtectionReady();
#else
 return CursorProtectionReady();
#endif
}
void Dispatch(BYTE key=0xA5,BYTE scan=0xE1,DWORD flags=0xCAFEBABEUL,ULONG_PTR extra=~ULONG_PTR(0)-17){
 const int api=keyApi();
 if(api>=0&&g_cursorInstalled[api])reinterpret_cast<PFN_KeybdEvent>(g_cursorDetours[api])(key,scan,flags,extra);
 else Lower(key,scan,flags,extra); // Exact before behavior: export has no product detour.
}
void PermissionReentry(){++probeDepth;if(probeDepth>maxProbeDepth)maxProbeDepth=probeDepth;if(probeDepth<=4)Dispatch();--probeDepth;}
void Reset(bool install=true){
 modelNow=1000;g_jobClaimExpiry=0;owner={};owner.foregroundVerified=owner.foregroundOwned=owner.windowReady=owner.humanFresh=true;
 beforeReserve={};lowerAction={};lowerCalls=0;gotKey=gotScan=0;gotFlags=entryError=0;gotExtra=0;claimAttempt=claimDenied=false;probeReentry=false;probeDepth=maxProbeDepth=0;
 keyExport=verifyKey=true;failKeyHook=aliasKey=positionAliases=false;g_cursorInstallFinished=false;for(auto& count:mutations)count=0;
 for(unsigned i=0;i<vi_cursor_guard::ApiCount;++i){g_cursorCounts[i].blocked=0;g_cursorCounts[i].forwarded=0;g_cursorInstalled[i]=false;g_cursorEntries[i]=g_cursorDetours[i]=g_cursorOriginals[i]=nullptr;g_cursorAlias[i]=-1;}
 o_keybd_event=Lower;if(install)InstallCursorHooks();SetLastError(4567);
}
unsigned passed=0,failed=0;
void Check(bool v,const char* name){std::printf("%s %s\n",v?"PASS":"FAIL",name);v?++passed:++failed;}
int main(){
 std::puts("PURE_MODEL: all named Win32/loader/original APIs are private callbacks; no native input API called");
 Reset();Check(vi_cursor_guard::SetPosition==0&&vi_cursor_guard::GetPhysicalPosition==7,"legacy8 indices unchanged");
 Check(keyApi()==8&&vi_cursor_guard::ApiCount==9,"keybd appended after legacy8");
 Check(CursorProtectionReady(),"legacy8 version1 readiness still means its original8 APIs");
 Check(keyApi()>=0&&AllReady(),"new complete typed hook installation is ready");
 Dispatch();Check(lowerCalls==1&&gotKey==0xA5&&gotScan==0xE1&&gotFlags==0xCAFEBABEUL&&gotExtra==~ULONG_PTR(0)-17,"unclaimed owned foreground forwards exact typed arguments");
 Check(entryError==4567&&GetLastError()==7331,"policy probe and fence preserve incoming/outgoing original LastError");
 Check(g_claimNativeFence.Snapshot().active==0,"native reservation retires after return");
 Reset();probeReentry=true;Dispatch();Check(lowerCalls==1&&maxProbeDepth==1&&keyApi()>=0&&g_cursorCounts[keyApi()].blocked==1,"permission-query reentry refusesbusy before recursive foreground probing");
 Reset();o_keybd_event=nullptr;Dispatch();Check(lowerCalls==0&&GetLastError()==ERROR_PROC_NOT_FOUND&&keyApi()>=0&&g_cursorCounts[keyApi()].blocked==1,"missing original reports refusal without a native attempt");
 Reset();g_jobClaimExpiry=2000;Dispatch();Check(lowerCalls==0&&GetLastError()==ERROR_ACCESS_DENIED,"live claim blocks keybd even with verified foreground and fresh human intent");
 Check(keyApi()>=0&&g_cursorCounts[keyApi()].blocked==1,"void refusal increments scoped keybd blocked counter");
 Reset();owner.foregroundOwned=false;Dispatch();Check(lowerCalls==0&&GetLastError()==ERROR_ACCESS_DENIED,"background keybd never becomes target virtual input");
 Reset();owner.foregroundVerified=false;Dispatch();Check(lowerCalls==0,"disagreeing foreground witnesses refuse");
 Reset();owner.windowReady=false;Dispatch();Check(lowerCalls==0,"unready target refuses");
 Reset();owner.humanFresh=false;Dispatch();Check(lowerCalls==0,"expired human freshness without lease refuses");
 Reset();owner.humanFresh=false;owner.lease=true;Dispatch();Check(lowerCalls==1,"unclaimed explicit foreground lease preserves native policy");
 Reset();owner.humanFresh=false;owner.lease=true;owner.forcedVirtual=true;Dispatch();Check(lowerCalls==0,"forced virtual without fresh intent still refuses lease-only writer");
 Reset();owner.forcedVirtual=true;Dispatch();Check(lowerCalls==1,"fresh foreground human policy remains unchanged in forced virtual mode");
 Reset();g_jobClaimExpiry=modelNow;Dispatch();Check(lowerCalls==1,"expired claim allows only ordinary ownership policy");
 Reset();beforeReserve=[](){g_jobClaimExpiry=modelNow+1000;};Dispatch();Check(lowerCalls==0,"claim published after permission probe is caught by operation reservation");
 Reset();{ClaimNativeAdmission transition;Dispatch();Check(lowerCalls==0&&GetLastError()==ERROR_BUSY,"claim transition denies keybd without waiting");}
 Reset();lowerAction=[](){claimAttempt=true;ClaimNativeAdmission admission;claimDenied=!admission;};Dispatch();
 Check(claimAttempt&&claimDenied&&g_claimNativeFence.Snapshot().active==0,"claim inside lower callback refuses promptly then reservation drains");
 {ClaimNativeAdmission later;Check(static_cast<bool>(later),"later claim can begin after lower callback completed");}
 Reset();unsigned nested=0;lowerAction=[&](){if(!nested++){Dispatch(0x2A,0x1D,2,9);}};Dispatch();Check(lowerCalls==2&&g_claimNativeFence.Snapshot().active==0,"bounded unclaimed reentry releases both reservations without holding fence lock across original");
 Reset();lowerAction=[](){owner.humanFresh=false;modelNow+=400;};Dispatch();Dispatch();Check(lowerCalls==1,"later call resamples expired ownership instead of caching permission");
 Reset(false);keyExport=false;InstallCursorHooks();Check(CursorProtectionReady(),"missing keybd export does not redefine legacy8 readiness");Check(!AllReady(),"missing keybd export refuses composed input coverage");
 Reset(false);failKeyHook=true;InstallCursorHooks();Check(!AllReady(),"failed keybd installation is not ready");
 Reset(false);aliasKey=true;InstallCursorHooks();Check(!AllReady()&&keyApi()>=0&&g_cursorAlias[keyApi()]<0,"ABI-incompatible keybd alias is refused without guessed trampoline");
 Check(mutations[4]==0&&!g_cursorInstalled[vi_cursor_guard::MouseEvent],"keybd conflict skips earlier wrong-ABI target BEFORE any HookAddr mutation");
 Reset(false);positionAliases=true;InstallCursorHooks();Check(CursorProtectionReady()&&g_cursorAlias[vi_cursor_guard::SetPhysicalPosition]==vi_cursor_guard::SetPosition&&g_cursorAlias[vi_cursor_guard::GetPhysicalPosition]==vi_cursor_guard::GetPosition,"known compatible position aliases remain supported");
 Reset();verifyKey=false;Check(!AllReady(),"changed keybd entry is detected independently of installed flag");
 Reset();Check(keyApi()>=0&&g_cursorAlias[keyApi()]==-1,"new API alias diagnostic starts unaliased");
 std::printf("RESULT %u PASS %u FAIL\n",passed,failed);return failed?1:0;
}
