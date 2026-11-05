// Exact production factory wrappers and late probe, with finite module/GUI/COM fakes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
static HMODULE expected=(HMODULE)(ULONG_PTR)0x10000000,current=expected;
static void* targets[2]={(void*)(ULONG_PTR)0x10001000,(void*)(ULONG_PTR)0x10002000};
static void* exports[2]={targets[0],targets[1]};
static bool route=true,retainAllowed=true,refuseDuringFactory=false;static unsigned references=0,calls=0,unowned=0,windows=0;
static bool returnFactory=false,refuseDuringInspection=false;static unsigned creates=0,released=0;static void* factoryTable[24]{};struct ModelCom{void** table;};static ModelCom factoryObject{factoryTable};
static ULONG STDMETHODCALLTYPE ModelRelease(IDirect3D9*){++released;return 1;}
static ULONGLONG clockMs=10000;static ULONGLONG WINAPI ModelClock(){return clockMs;}
static HMODULE WINAPI ModelModule(LPCWSTR){return current;}
static BOOL WINAPI ModelRetain(DWORD flags,LPCWSTR address,HMODULE* out){if(flags!=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS||!current||current!=expected||(void*)address!=(void*)expected||!retainAllowed)return FALSE;*out=current;++references;return TRUE;}
static BOOL WINAPI ModelFree(HMODULE){if(references)--references;return TRUE;}
static FARPROC WINAPI ModelExport(HMODULE,LPCSTR name){return (FARPROC)exports[!strcmp(name,"Direct3DCreate9Ex")?1:0];}
static ATOM WINAPI ModelClass(const WNDCLASSW*){return 1;}
static HWND WINAPI ModelWindow(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID){++windows;return (HWND)(ULONG_PTR)1;}
static BOOL WINAPI ModelDestroy(HWND){return TRUE;}
static LRESULT WINAPI ModelDefault(HWND,UINT,WPARAM,LPARAM){return 0;}
static bool CursorEntryReaches(void*,void*){return route;}
std::atomic<HMODULE> g_d9Module{expected},g_d9Retained{nullptr};std::atomic<bool> g_d9Hooked{true},g_d9FactoryHooksReady{true};std::atomic<void*> g_d9FactoryTargets[2]{};std::atomic<long> g_d9Refused{0};
extern std::atomic<unsigned> g_d9ProbeAdmissions;static bool stopBeforePolicy=false;
struct ModelPolicy{int value=0;int load()const{return value;}ModelPolicy& operator=(int next){if(next==1)stopBeforePolicy=(g_d9ProbeAdmissions.load()&1u)!=0;value=next;return *this;}operator int()const{return value;}}g_fullscreenPolicy;
std::atomic<bool> g_d9PresentHooked{false};
#include "actual_probe_policy.inl"
thread_local bool g_d9Internal=false;
using D9Factory=IDirect3D9*(WINAPI*)(UINT);using D9FactoryEx=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
static IDirect3D9* WINAPI Native(UINT){++calls;if(!references)++unowned;if(refuseDuringFactory)++g_d9Refused;return returnFactory?(IDirect3D9*)&factoryObject:nullptr;}
static HRESULT WINAPI NativeEx(UINT,IDirect3D9Ex** out){++calls;if(!references)++unowned;*out=nullptr;return E_NOINTERFACE;}
D9Factory o_D9Factory=Native;D9FactoryEx o_D9FactoryEx=NativeEx;
IDirect3D9* WINAPI h_D9Factory(UINT);HRESULT WINAPI h_D9FactoryEx(UINT,IDirect3D9Ex**);
static bool D9Interface(IDirect3D9*){if(refuseDuringInspection){g_d9ProbeAdmissions.fetch_or(1u);++g_d9Refused;}return returnFactory;}static bool D9Device(IDirect3DDevice9*){return false;}
static HRESULT STDMETHODCALLTYPE ModelCreate(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9** out){++creates;*out=nullptr;return D3DERR_INVALIDCALL;}
template<class T>static T D9Original(void*,unsigned,unsigned){return (T)&ModelCreate;}
using D9Create=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using D9CreateEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
enum{D9_CREATE,D9_CREATEEX};
#define GetModuleHandleW ModelModule
#define GetModuleHandleExW ModelRetain
#define FreeLibrary ModelFree
#define GetProcAddress ModelExport
#define RegisterClassW ModelClass
#define CreateWindowExW ModelWindow
#define DestroyWindow ModelDestroy
#define DefWindowProcW ModelDefault
#define GetTickCount64 ModelClock
#include "actual_lifetime.inl"
#include "actual_factories.inl"
#include "actual_late.inl"
struct ConfigRequest{std::string fullscreen;};static bool JHas(const ConfigRequest&,const char*){return true;}static std::string JStr(const ConfigRequest& j,const char*){return j.fullscreen;}
static void ApplyConfig(const char* value){ConfigRequest j{value};
#include "actual_config.inl"
}
static unsigned passed=0,failed=0;
static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
static void ArmAt(ULONGLONG at){(void)at;
#ifdef VI_D9_LATE_GRACE
 g_d9LateArmedAt=at;
#endif
}
static void Reset(){current=expected;route=true;retainAllowed=true;refuseDuringFactory=false;returnFactory=refuseDuringInspection=false;creates=released=0;factoryTable[2]=(void*)&ModelRelease;references=calls=unowned=windows=0;g_d9Retained=nullptr;g_d9FactoryTargets[0]=targets[0];g_d9FactoryTargets[1]=targets[1];exports[0]=targets[0];exports[1]=targets[1];g_d9LateAttempted=false;g_d9FactoryHooksReady=true;g_d9Refused=0;g_fullscreenPolicy=0;g_d9ProbeAdmissions=0;g_d9PresentHooked=false;clockMs=10000;ArmAt(8000);}
int main(){
 Reset();TryD9LateHooks();Check(calls==2&&unowned==0&&references==1,"late factories execute only after owning the exact module");Check(!g_d9Internal,"late internal flag restored");
 Reset();current=nullptr;TryD9LateHooks();Check(calls==0&&references==0,"unloaded factory module never executes stale trampoline");
 Reset();current=(HMODULE)(ULONG_PTR)0x20000000;TryD9LateHooks();Check(calls==0&&references==0,"different-base reload refuses stale factories");
 Reset();route=false;TryD9LateHooks();Check(calls==0&&references==0,"same-base reload without live detour refuses stale factories");
 Reset();exports[0]=(void*)(ULONG_PTR)0x10003000;exports[1]=(void*)(ULONG_PTR)0x10004000;TryD9LateHooks();Check(calls==0&&references==0,"changed export identities release tentative references");
 Reset();retainAllowed=false;TryD9LateHooks();Check(calls==0,"failed module pin prevents all factory execution");
 Reset();g_d9FactoryHooksReady=false;TryD9LateHooks();Check(calls==0&&!g_d9LateAttempted,"installation in progress does not consume late one-shot probe");
 Reset();h_D9Factory(D3D_SDK_VERSION);IDirect3D9Ex* ex=nullptr;h_D9FactoryEx(D3D_SDK_VERSION,&ex);Check(calls==2&&unowned==0&&references==1,"normal factory wrappers share exact owned module lifetime");
 Reset();current=nullptr;h_D9Factory(D3D_SDK_VERSION);h_D9FactoryEx(D3D_SDK_VERSION,&ex);Check(calls==0,"normal wrappers refuse stale originals too");
 Reset();route=false;h_D9Factory(D3D_SDK_VERSION);h_D9FactoryEx(D3D_SDK_VERSION,&ex);Check(calls==0&&references==0,"normal wrappers reject changed entry routes without leaking tentative refs");
 Reset();g_d9Refused=1;TryD9LateHooks();Check(calls==0&&windows==0&&!g_d9LateAttempted,"explicit fullscreen refusal suppresses all late discovery without consuming attempt");
 Reset();refuseDuringFactory=true;TryD9LateHooks();Check(calls==1,"refusal during first factory suppresses the next probe factory");
 Reset();g_fullscreenPolicy=1;TryD9LateHooks();Check(calls==0&&windows==0&&!g_d9LateAttempted,"configured fullscreen refusal prevents probes before any recorded rejection");
 Reset();{D9ProbeNativeAdmission prior;g_d9ProbeAdmissions.fetch_or(1u);D9ProbeNativeAdmission later;Check((bool)prior&&!(bool)later&&g_d9ProbeAdmissions==3,"atomic refusal commit rejects later native admissions without waiting for prior callback");}Check(g_d9ProbeAdmissions==1,"prior reservation cleanup preserves permanent refusal stop bit");
 Reset();ArmAt(0);TryD9LateHooks();Check(calls==0&&windows==0&&!g_d9LateAttempted,"first monitor opportunity arms grace without native discovery");clockMs=11999;TryD9LateHooks();Check(calls==0&&!g_d9LateAttempted,"startup configuration grace remains inactive until full two seconds");clockMs=12000;TryD9LateHooks();Check(calls==2&&g_d9LateAttempted,"preexisting uncovered device path probes after grace");
 Reset();g_d9PresentHooked=true;TryD9LateHooks();Check(calls==0&&windows==0&&!g_d9LateAttempted,"application-created covered Present hooks need no diagnostic devices");
 Reset();returnFactory=refuseDuringInspection=true;TryD9LateHooks();Check(calls==1&&creates==0&&released==1,"deterministic refusal between factory and Create prevents native device creation and releases factory");
 Reset();ApplyConfig("refuse");Check(stopBeforePolicy&&g_fullscreenPolicy==1&&(g_d9ProbeAdmissions.load()&1u),"actual config branch commits stop before publishing refusal policy");ApplyConfig("borderless");D9ProbeNativeAdmission afterConfig;Check(!(bool)afterConfig&&g_fullscreenPolicy==0,"later policy relaxation does not silently resume diagnostic native creates");
 printf("d3d9 lifetime: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
