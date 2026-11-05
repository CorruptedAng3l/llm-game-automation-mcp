#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <cstdio>
#pragma comment(lib,"dxguid.lib")
enum{MH_OK,MH_ERROR_ENABLED,MH_ERROR_ALREADY_CREATED};
struct Hook{void* detour=nullptr;bool enabled=false;};static std::map<void*,Hook> hooks;
static int VIMH_CreateHook(void* target,void* detour,void** original){if(hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=target;hooks[target]={detour,false};return MH_OK;}
static int VIMH_EnableHook(void* target){hooks[target].enabled=true;return MH_OK;}
static bool CursorEntryReaches(void* target,void* detour){auto i=hooks.find(target);return i!=hooks.end()&&i->second.enabled&&i->second.detour==detour;}
static std::mutex g_claimDiEntryMx;struct Claim{void* target=nullptr;};static Claim g_claimDiAcquireEntries[16],g_claimDiCoopEntries[16];
static constexpr unsigned kDiPollSlots=8;struct Poll{bool enabled=false;};static Poll g_diStateEntries[kDiPollSlots],g_diDataEntries[kDiPollSlots];
static bool DiPollEntryKnown(void*){return false;}static std::set<void*> stateTargets,bufferTargets;static unsigned rightsCalls=0,released=0;static bool refuseGuard=false;
static bool ClaimDiInstallEntry(void*,bool){return !refuseGuard;}static bool DiPollInstall(void* target,bool buffer){(buffer?bufferTargets:stateTargets).insert(target);return true;}
struct Object{void** table;};static void* ownerTable[8]{};static void* probeTable[40]{};static void* deviceTable[40]{};static void* aliasTable[40]{};static Object owner{ownerTable},probe{probeTable},device{deviceTable},aliasDevice{aliasTable};
static bool nativeFailure=false;static unsigned nativeFactoryCalls=0;static const GUID customDevice={0x24681234,0x5678,0x1234,{0,1,2,3,4,5,6,7}};
static ULONG STDMETHODCALLTYPE Release(void*){++released;return 1;}
static HRESULT STDMETHODCALLTYPE Acquire(void*){++rightsCalls;return S_OK;}
static HRESULT STDMETHODCALLTYPE Coop(void*,HWND,DWORD){++rightsCalls;return S_OK;}
static volatile unsigned pollRoles[8]{}; // Distinct effects prevent linker ICF folding the negative-control entries.
static HRESULT STDMETHODCALLTYPE StateProbe(void*,DWORD,void*){++pollRoles[0];return S_OK;}static HRESULT STDMETHODCALLTYPE DataProbe(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD){++pollRoles[1];return S_OK;}
static HRESULT STDMETHODCALLTYPE StateActual(void*,DWORD,void*){++pollRoles[2];return S_OK;}static HRESULT STDMETHODCALLTYPE DataActual(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD){++pollRoles[3];return S_OK;}
static HRESULT STDMETHODCALLTYPE StateAlias(void*,DWORD,void*){++pollRoles[4];return S_OK;}static HRESULT STDMETHODCALLTYPE DataAlias(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD){++pollRoles[5];return S_OK;}
static HRESULT STDMETHODCALLTYPE SetFormat(void*,LPCDIDATAFORMAT){++pollRoles[6];return S_OK;}static HRESULT STDMETHODCALLTYPE ActionMap(void*,void*,const void*,DWORD){++pollRoles[7];return S_OK;}
static HRESULT STDMETHODCALLTYPE Query(void*,REFIID iid,void** out){if(IsEqualIID(iid,IID_IDirectInputDevice8W)){*out=&aliasDevice;return S_OK;}*out=nullptr;return E_NOINTERFACE;}
static HRESULT STDMETHODCALLTYPE Create(void*,REFGUID guid,void** out,LPUNKNOWN){if(nativeFailure){*out=nullptr;return E_FAIL;}*out=IsEqualGUID(guid,customDevice)?&device:&probe;return S_OK;}
static HRESULT WINAPI Factory(HINSTANCE,DWORD,REFIID,void** out,LPUNKNOWN){++nativeFactoryCalls;if(nativeFailure){*out=nullptr;return E_FAIL;}*out=&owner;return S_OK;}
static HMODULE module=(HMODULE)(ULONG_PTR)1;static unsigned refs=0;
static HMODULE WINAPI Module(LPCWSTR){return module;}static BOOL WINAPI Retain(DWORD,LPCWSTR,HMODULE* out){if(!module)return FALSE;*out=module;++refs;return TRUE;}static BOOL WINAPI Free(HMODULE){if(refs)--refs;return TRUE;}
static FARPROC WINAPI Export(HMODULE,LPCSTR){return (FARPROC)&Factory;}
static void ClaimDiReleaseBorrowed(void* p){if(p)((ULONG(STDMETHODCALLTYPE*)(void*))(*(void***)p)[2])(p);}
using PFN_ClaimDiCreateDevice=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,void**,LPUNKNOWN);
static bool g_diHooked=false;static std::atomic<unsigned> g_di8ProbeRoles{0};static std::string g_hooksFail;
static void TryClaimedDi8(){}static void Log(const char*,...){}
struct JOut{template<class T>void num(const char*,T){}void bol(const char*,bool){}void str(const char*,const std::string&){}std::string done(){return "{}";}};
#define GetModuleHandleW Module
#define GetModuleHandleExW Retain
#define GetProcAddress Export
#define FreeLibrary Free
// This focused discovery test models the separately tested format subsystem.
using DiSetFormatFn=HRESULT(STDMETHODCALLTYPE*)(void*,LPCDIDATAFORMAT);using DiReleaseFn=ULONG(STDMETHODCALLTYPE*)(void*);using DiActionMapFn=HRESULT(STDMETHODCALLTYPE*)(void*,void*,const void*,DWORD);
struct ModelLifetime{};static bool DiFormatEnsure(void*){return true;}static bool DiFormatLink(void*,void*){return true;}static std::shared_ptr<ModelLifetime> DiFormatFind(void*){return nullptr;}static void DiFormatRetire(const std::shared_ptr<ModelLifetime>&){}
static HRESULT DiSetFormatCall(DiSetFormatFn f,void* s,LPCDIDATAFORMAT d){return f(s,d);}static HRESULT DiSetActionMapCall(DiActionMapFn f,void* s,void* d,const void* n,DWORD flags){return f(s,d,n,flags);}
#include "actual_discovery.inl"
template<class F>static F Entry(void* target){auto i=hooks.find(target);return (F)(i!=hooks.end()&&i->second.enabled?i->second.detour:target);}
static unsigned passed=0,failed=0;static void Check(bool b,const char* what){printf("%s %s\n",b?"PASS":"FAIL",what);b?++passed:++failed;}
int main(){
 ownerTable[0]=(void*)&Query;ownerTable[2]=(void*)&Release;ownerTable[3]=(void*)&Create;
 for(void** table:{probeTable,deviceTable,aliasTable}){table[0]=(void*)&Query;table[2]=(void*)&Release;table[7]=(void*)&Acquire;table[11]=(void*)&SetFormat;table[13]=(void*)&Coop;table[30]=(void*)&ActionMap;}
 probeTable[9]=(void*)&StateProbe;probeTable[10]=(void*)&DataProbe;deviceTable[9]=(void*)&StateActual;deviceTable[10]=(void*)&DataActual;aliasTable[9]=(void*)&StateAlias;aliasTable[10]=(void*)&DataAlias;
 TryDi8Factory();Check(rightsCalls==0&&nativeFactoryCalls==0&&refs==0,"export installation requests no COM factory, module retain or device rights");
 using FactoryFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);void* actualOwner=nullptr;
 auto createFactory=Entry<FactoryFn>((void*)&Factory);Check(createFactory(nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8A,&actualOwner,nullptr)==S_OK&&actualOwner==&owner,"application factory native result and owner preserved");
 Check(Entry<PFN_ClaimDiCreateDevice>(ownerTable[3])!=(PFN_ClaimDiCreateDevice)ownerTable[3],"actual owner CreateDevice boundary is hooked");
 void* actualDevice=nullptr;Check(Entry<PFN_ClaimDiCreateDevice>(ownerTable[3])(actualOwner,customDevice,&actualDevice,nullptr)==S_OK&&actualDevice==&device,"actual non-probe device returned without replay");
 Check(stateTargets.count(deviceTable[9])&&bufferTargets.count(deviceTable[10]),"distinct actual device state and buffered implementations discovered");
 void* wide=nullptr;using QueryFn=HRESULT(STDMETHODCALLTYPE*)(void*,REFIID,void**);
 Check(Entry<QueryFn>(deviceTable[0])(actualDevice,IID_IDirectInputDevice8W,&wide)==S_OK&&wide==&aliasDevice,"known device QueryInterface result preserved");
 Check(stateTargets.count(aliasTable[9])&&bufferTargets.count(aliasTable[10]),"distinct queried wide-device poll implementations discovered");
 nativeFailure=true;void* absent=nullptr;Check(createFactory(nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8A,&absent,nullptr)==E_FAIL&&absent==nullptr,"native factory failure unchanged and no uncertain replay");nativeFailure=false;
 const auto before=released;refuseGuard=true;actualDevice=nullptr;
 Check(Entry<PFN_ClaimDiCreateDevice>(ownerTable[3])(actualOwner,customDevice,&actualDevice,nullptr)==DIERR_NOTINITIALIZED&&actualDevice==nullptr&&released==before+1,"failed required rights guards release exactly returned reference and refuse publication");
 Check(rightsCalls==0,"all factory/device/alias discovery leaves native rights calls at zero");
 module=nullptr;void* stale=nullptr;Check(createFactory(nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8A,&stale,nullptr)==DIERR_NOTINITIALIZED&&stale==nullptr,"missing factory module refuses stale original before execution");
 bool counted=false;
#ifdef VI_DI_DISCOVERY
 counted=g_diFactoryReturns==1&&g_diDeviceReturns==2&&g_diQueriedDevices==1&&g_diProbeFactoryReturns==0;
#endif
 Check(counted,"application discovery counters exclude probes, failures and polling-consumption claims");
 printf("di discovery: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
