// SAME source before/after. All COM, module/probe and MinHook callbacks below
// terminate in local fabricated C++ data. No Windows headers, libraries, DLLs,
// device acquisition, cursor/input/foreground APIs or process helpers.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define STDMETHODCALLTYPE __stdcall
#define WINAPI __stdcall
using DWORD=std::uint32_t;using ULONG=std::uint32_t;using BYTE=unsigned char;using HRESULT=std::int32_t;using ULONGLONG=std::uint64_t;
using HWND=void*;using HMODULE=void*;using HINSTANCE=void*;using LPUNKNOWN=void*;
struct GUID{unsigned id;};using REFGUID=const GUID&;using REFIID=const GUID&;
const GUID GUID_SysMouse{100},IID_IDirectInput8A{1},IID_IDirectInput8W{2};
constexpr DWORD DIRECTINPUT_VERSION=0x0800,DISCL_EXCLUSIVE=1,DISCL_NONEXCLUSIVE=2,DISCL_FOREGROUND=4,DISCL_BACKGROUND=8,DISCL_NOWINKEY=16;
constexpr HRESULT DI_OK=0,DIERR_OTHERAPPHASPRIO=-1,DIERR_NOTINITIALIZED=-2,DIERR_INVALIDPARAM=-3,E_NOINTERFACE=-4,NativeFailure=-5;
constexpr BYTE DI8DEVTYPE_MOUSE=0x12,DI8DEVTYPE_KEYBOARD=0x13;
#define GET_DIDEVICE_TYPE(x) static_cast<BYTE>((x)&0xff)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct DIDEVCAPS{DWORD dwSize=0,dwFlags=0,dwDevType=0,reserved[8]{};};using LPDIDEVCAPS=DIDEVCAPS*;
using PFN_DIACQ=HRESULT(STDMETHODCALLTYPE*)(void*);using PFN_DICAPS=HRESULT(STDMETHODCALLTYPE*)(void*,LPDIDEVCAPS);
using PFN_DICOOP=HRESULT(STDMETHODCALLTYPE*)(void*,HWND,DWORD);
unsigned passed=0,failed=0;
void Check(bool ok,const char* text){std::printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++passed:++failed;}
ULONGLONG g_now=100;std::atomic<ULONGLONG> g_jobClaimExpiry{0};
ULONGLONG HousekeepingNow(){return g_now;}
#include "vi_claim_native_fence.inl"
struct Counters{unsigned di_acq=0,di_fake=0;} C;bool g_faking=false;
bool Faking(){return g_faking;}
struct Device{
 void** table=nullptr;int kind=2;bool capsFail=false,acquired=false;HRESULT acquireResult=DI_OK,coopResult=DI_OK;
 unsigned caps=0,acquires=0,coops=0,unacquires=0,releases=0;DWORD flags=0;HWND hwnd=nullptr;
 void(*onCaps)(Device*)=nullptr;void(*onAcquire)(Device*)=nullptr;
};
HRESULT STDMETHODCALLTYPE FakeCaps(void* self,LPDIDEVCAPS caps){auto& d=*(Device*)self;++d.caps;if(d.onCaps)d.onCaps(&d);if(d.capsFail)return NativeFailure;caps->dwDevType=(DWORD)(d.kind==2?DI8DEVTYPE_MOUSE:d.kind==1?DI8DEVTYPE_KEYBOARD:0x15);return DI_OK;}
HRESULT STDMETHODCALLTYPE FakeAcquire(void* self){auto& d=*(Device*)self;++d.acquires;if(d.onAcquire)d.onAcquire(&d);if(SUCCEEDED(d.acquireResult))d.acquired=true;return d.acquireResult;}
HRESULT STDMETHODCALLTYPE FakeAcquireB(void* self){return FakeAcquire(self);}
HRESULT STDMETHODCALLTYPE FakeAcquireC(void* self){return FakeAcquire(self);}
HRESULT STDMETHODCALLTYPE FakeAcquireExtra(void* self){return FakeAcquire(self);}
HRESULT STDMETHODCALLTYPE FakeAcquireKnown(void* self){return FakeAcquire(self);}
HRESULT STDMETHODCALLTYPE FakeUnacquire(void* self){auto& d=*(Device*)self;++d.unacquires;d.acquired=false;return DI_OK;}
HRESULT STDMETHODCALLTYPE FakeCoop(void* self,HWND hwnd,DWORD flags){
 auto& d=*(Device*)self;++d.coops;d.flags=flags;d.hwnd=hwnd;
 const auto ex=flags&(DISCL_EXCLUSIVE|DISCL_NONEXCLUSIVE),fg=flags&(DISCL_FOREGROUND|DISCL_BACKGROUND);
 if((ex!=1&&ex!=2)||(fg!=4&&fg!=8)||(flags&~DWORD(31))||(d.kind==2&&((ex==1&&fg==8)||(flags&DISCL_NOWINKEY))))return DIERR_INVALIDPARAM;
 return d.coopResult;
}
HRESULT STDMETHODCALLTYPE FakeCoopB(void* s,HWND h,DWORD f){return FakeCoop(s,h,f);}
HRESULT STDMETHODCALLTYPE FakeCoopC(void* s,HWND h,DWORD f){return FakeCoop(s,h,f);}
ULONG STDMETHODCALLTYPE FakeDeviceRelease(void* self){++((Device*)self)->releases;return 1;}
#if __has_include("vi_pad_api_census.h")
#include "vi_pad_api_census.h"
#endif
#include "actual_devtype.inl"
#include "actual_policy.inl"
#include "actual_acquire.inl"
PFN_DIACQ o_DIAcquire=nullptr;
#include "actual_known_wrapper.inl"
struct Hook{void* detour=nullptr;void* original=nullptr;bool enabled=false;};std::map<void*,Hook> hooks;
using MH_STATUS=int;constexpr MH_STATUS MH_OK=0,MH_ERROR_ALREADY_CREATED=1,MH_ERROR_ENABLED=2,MH_ERROR_NOT_CREATED=3;
unsigned creates=0,enables=0;void* forceAlready=nullptr;void* forceEnableFail=nullptr;
MH_STATUS VIMH_CreateHook(void* target,void* detour,void** original){++creates;if(target==forceAlready||hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=target;hooks[target]={detour,target,false};return MH_OK;}
MH_STATUS VIMH_EnableHook(void* target){++enables;if(target==forceEnableFail)return MH_ERROR_NOT_CREATED;auto i=hooks.find(target);if(i==hooks.end())return MH_ERROR_NOT_CREATED;if(i->second.enabled)return MH_ERROR_ENABLED;i->second.enabled=true;return MH_OK;}
bool HookAddr(const char*,void* target,void* detour,void** original){auto result=VIMH_CreateHook(target,detour,original);if(result==MH_ERROR_ALREADY_CREATED)return true;return result==MH_OK&&VIMH_EnableHook(target)==MH_OK;}
struct Owner{void** table=nullptr;unsigned index=0,releases=0;};
Device devices[4];Owner owners[4];void* deviceTables[4][14]{};void* ownerTable[4]{};int modules[3]{};
HRESULT STDMETHODCALLTYPE FakeCreateMouse(void* self,REFGUID guid,void** out,LPUNKNOWN){auto& o=*(Owner*)self;if(guid.id!=100)return NativeFailure;*out=&devices[o.index];return DI_OK;}
ULONG STDMETHODCALLTYPE FakeOwnerRelease(void* self){++((Owner*)self)->releases;return 1;}
HRESULT WINAPI FakeCreate8(HINSTANCE,DWORD version,REFIID iid,void** out,LPUNKNOWN){if(version!=0x800||iid.id<1||iid.id>2)return NativeFailure;*out=&owners[iid.id-1];return DI_OK;}
HRESULT WINAPI FakeCreateLegacyA(HINSTANCE,DWORD version,void** out,LPUNKNOWN){if(version!=0x300)return NativeFailure;*out=&owners[2];return DI_OK;}
HRESULT WINAPI FakeCreateLegacyW(HINSTANCE,DWORD version,void** out,LPUNKNOWN){if(version!=0x300)return NativeFailure;*out=&owners[3];return DI_OK;}
HMODULE GetModuleHandleW(const wchar_t* name){if(!name)return &modules[2];return std::wcscmp(name,L"dinput8.dll")==0?(void*)&modules[0]:std::wcscmp(name,L"dinput.dll")==0?(void*)&modules[1]:nullptr;}
void* GetProcAddress(HMODULE module,const char* name){if(module==&modules[0]&&std::strcmp(name,"DirectInput8Create")==0)return (void*)&FakeCreate8;if(module==&modules[1]&&std::strcmp(name,"DirectInputCreateA")==0)return (void*)&FakeCreateLegacyA;if(module==&modules[1]&&std::strcmp(name,"DirectInputCreateW")==0)return (void*)&FakeCreateLegacyW;return nullptr;}
#include "actual_hooks.inl"
HRESULT CallAcquire(Device& d){auto i=hooks.find(d.table[7]);if(i!=hooks.end()&&i->second.enabled)return ((PFN_DIACQ)i->second.detour)(&d);return DIAcquireImpl((PFN_DIACQ)d.table[7],&d);}
HRESULT CallCoop(Device& d,DWORD flags){auto i=hooks.find(d.table[13]);if(i!=hooks.end()&&i->second.enabled)return ((PFN_DICOOP)i->second.detour)(&d,&modules[2],flags);return ClaimedDiSetCoopImpl((PFN_DICOOP)d.table[13],&d,&modules[2],flags);}
void Reset(Device& d){d.kind=2;d.capsFail=false;d.acquired=false;d.acquireResult=d.coopResult=DI_OK;d.caps=d.acquires=d.coops=d.unacquires=0;d.flags=0;d.hwnd=nullptr;d.onCaps=d.onAcquire=nullptr;g_faking=false;g_jobClaimExpiry=0;g_now=100;}
bool callbackClaimRefused=false,callbackActive=false;
void ClaimDuringAcquire(Device*){callbackActive=g_claimNativeFence.Snapshot().active==1;ClaimNativeAdmission claim;callbackClaimRefused=!claim;}
HRESULT nestedInspection=DI_OK;
void ReenterCaps(Device* d){d->onCaps=nullptr;nestedInspection=ClaimedDiSetCoopImpl(&FakeCoop,d,nullptr,DISCL_EXCLUSIVE|DISCL_FOREGROUND);}
int main(){
 ownerTable[2]=(void*)&FakeOwnerRelease;ownerTable[3]=(void*)&FakeCreateMouse;
 const PFN_DIACQ acquire[4]={&FakeAcquire,&FakeAcquireB,&FakeAcquireC,&FakeAcquireC};
 const PFN_DICOOP coop[4]={&FakeCoop,&FakeCoopB,&FakeCoopC,&FakeCoopC};
 for(unsigned i=0;i<4;i++){owners[i].table=ownerTable;owners[i].index=i;deviceTables[i][2]=(void*)&FakeDeviceRelease;deviceTables[i][3]=(void*)&FakeCaps;deviceTables[i][7]=(void*)acquire[i];deviceTables[i][8]=(void*)&FakeUnacquire;deviceTables[i][13]=(void*)coop[i];devices[i].table=deviceTables[i];}
 auto& d=devices[0];Reset(d);
 d.capsFail=true;d.acquireResult=NativeFailure;Check(CallAcquire(d)==NativeFailure&&d.acquires==1&&d.caps==0,"unclaimed Acquire preserves native failure and never queries failed caps");
 g_faking=true;Check(CallAcquire(d)==DI_OK&&d.acquires==2&&d.caps==0,"unclaimed existing fakefocus emulation is unchanged");
 Reset(d);d.capsFail=true;Check(CallCoop(d,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&d.flags==5&&d.caps==0,"unclaimed SetCoop preserves exact flags and no extra caps query");
 d.coopResult=NativeFailure;Check(CallCoop(d,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND)==NativeFailure&&d.flags==10,"unclaimed SetCoop preserves native refusal");
 Reset(d);g_jobClaimExpiry=1000;Check(FAILED(CallAcquire(d))&&d.acquires==0&&!d.acquired,"claimed mouse Acquire refuses before native acquisition");
 g_faking=true;Check(FAILED(CallAcquire(d))&&d.acquires==0,"claimed refusal cannot be converted to fake DI_OK");
 for(int kind:{1,0}){Reset(d);d.kind=kind;g_jobClaimExpiry=1000;Check(CallAcquire(d)==DI_OK&&d.acquires==1,"claimed proven keyboard/joystick Acquire retains original");Check(CallCoop(d,DISCL_EXCLUSIVE|DISCL_FOREGROUND)==DI_OK&&d.flags==5,"claimed proven keyboard/joystick SetCoop retains original flags");}
 Reset(d);d.capsFail=true;g_jobClaimExpiry=1000;Check(CallAcquire(d)==DIERR_NOTINITIALIZED&&d.acquires==0&&d.caps==2,"claimed unknown caps refuse Acquire after bounded legacy-size retry");
 Check(CallCoop(d,5)==DIERR_NOTINITIALIZED&&d.coops==0,"claimed unknown caps never forward cooperative flags");
 d.capsFail=false;d.kind=1;Check(CallAcquire(d)==DI_OK&&d.acquires==1,"same pointer classified fresh after caps failure/type change");
 Reset(d);g_jobClaimExpiry=1000;Check(CallCoop(d,5)==DI_OK&&d.flags==6&&d.coops==1,"claimed exclusive foreground mouse is normalized nonexclusive");
 Check(CallCoop(d,10)==DI_OK&&d.flags==10,"claimed legal nonexclusive background remains background");
 d.coopResult=NativeFailure;Check(CallCoop(d,6)==NativeFailure&&d.flags==6,"claimed normalized native failure remains failure");
 for(DWORD invalid:{DWORD(0),DWORD(3|4),DWORD(1|4|8),DWORD(1|8),DWORD(2|4|16),DWORD(2|4|32)}){Reset(d);g_jobClaimExpiry=1000;Check(FAILED(CallCoop(d,invalid))&&d.coops==0,"claimed invalid/system-mouse-unsupported flags cannot become success");}
 Reset(d);Check(CallAcquire(d)==DI_OK&&d.acquired,"owned model pre-claim acquisition control");g_jobClaimExpiry=1000;Check(FAILED(CallAcquire(d))&&d.acquired&&d.unacquires==0,"prior acquired device is explicitly not released or silently unacquired");
 Reset(d);{ClaimNativeAdmission transition;Check((bool)transition,"model owns an actual shared admission transition");Check(FAILED(CallAcquire(d))&&d.acquires==0&&d.caps==0,"pending transition refuses unproved Acquire without native/caps call");Check(FAILED(CallCoop(d,5))&&d.coops==0,"pending transition refuses cooperative mutation");}
 Check(CallAcquire(d)==DI_OK,"unclaimed path resumes after transition ends");
 Reset(d);d.onAcquire=&ClaimDuringAcquire;Check(CallAcquire(d)==DI_OK&&callbackActive&&callbackClaimRefused&&g_jobClaimExpiry==0,"original callback runs unlocked but reserved; reentrant claim cannot commit");
 Check(g_claimNativeFence.Snapshot().active==0,"operation reservation retires after original return");
 Reset(d);g_jobClaimExpiry=1000;d.onCaps=&ReenterCaps;Check(FAILED(CallAcquire(d))&&FAILED(nestedInspection)&&d.coops==0,"capability callback reentry is finite and never forwards mouse exclusive flags");
 Reset(d);g_jobClaimExpiry=100;Check(CallAcquire(d)==DI_OK,"exact expiry boundary keeps unclaimed behavior");
 Reset(d);TryClaimedDi8();TryClaimedDiLegacy();Check(hooks.size()==6,"actual A/W and legacy probe bodies install three shared/distinct typed pairs");
 Check(owners[0].releases==1&&owners[1].releases==1&&owners[2].releases==1&&owners[3].releases==1&&devices[0].releases==1&&devices[3].releases==1,"probe COM references are released, never retained in registry");
 const auto installed=creates;TryClaimedDi8();TryClaimedDiLegacy();Check(creates==installed,"repeated module checks do not duplicate installation");
 for(unsigned i=0;i<4;i++){Reset(devices[i]);g_jobClaimExpiry=1000;Check(FAILED(CallAcquire(devices[i]))&&devices[i].acquires==0&&CallCoop(devices[i],5)==DI_OK&&devices[i].flags==6,"installed A/W/shared/legacy entry routes through same claimed guard");}
 const auto aliasCreates=creates;Check(ClaimDiInstallEntry((void*)&FakeAcquire,false)&&creates==aliasCreates,"known exact Acquire entry deduplicates without another hook");
 Check(!ClaimDiInstallEntry((void*)&FakeAcquire,true),"cross-method same-address alias never claims compatible ABI");
 forceAlready=(void*)&FakeAcquireExtra;Check(!ClaimDiInstallEntry(forceAlready,false),"unrelated already-created hook is refused, not declared covered");forceAlready=nullptr;
 Check(ClaimDiHookKnownAcquire("known",(void*)&FakeAcquireKnown,(void*)&h_DIAcquire,&o_DIAcquire)&&o_DIAcquire==&FakeAcquireKnown,"existing own Acquire wrapper can record its actual original");
 const auto knownCreates=creates;Check(ClaimDiInstallEntry((void*)&FakeAcquireKnown,false)&&creates==knownCreates,"new probe reuses proven old typed Acquire wrapper");
 Reset(d);g_jobClaimExpiry=1000;auto known=hooks.find((void*)&FakeAcquireKnown);Check(known!=hooks.end()&&FAILED(((PFN_DIACQ)known->second.detour)(&d))&&d.acquires==0,"proven old typed Acquire entry actually invokes the guarded production wrapper");
 std::printf("{\"passed\":%u,\"failed\":%u,\"completed\":true,\"scope\":\"exact production guard/fence with fabricated COM/module/MinHook callbacks; no desktop or DirectInput API\"}\n",passed,failed);return failed?1:0;
}
