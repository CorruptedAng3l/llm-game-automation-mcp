// Native D3D9 boundary guard: never delegates fullscreen or a fullscreen mode.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <share.h>
#include "vi_mh_rename.h"
#include "MinHook.h"
#pragma comment(lib,"dxguid.lib")
static std::recursive_mutex mx;static std::mutex logMx;static std::map<void*,void*> methods[5],tables[5];static HMODULE module;static FILE* output;static std::atomic<LONG> blocked{0},normal{0},badMode{0};
enum{CREATE=0,CREATEEX=1,RESET=2,RESETEX=3,ADDITIONAL=4};
template<class F>static F Original(void* self,unsigned slot,unsigned kind){std::lock_guard<std::recursive_mutex> lock(mx);void** table=*(void***)self;auto cached=tables[kind].find(table);if(cached!=tables[kind].end())return(F)cached->second;auto target=table[slot];auto it=methods[kind].find(target);if(it==methods[kind].end()){std::lock_guard<std::mutex> logLock(logMx);if(output){fprintf(output,"GUARD_LOOKUP_MISS self=%p vt=%p target=%p kind=%u slot=%u count=%zu\n",self,*(void***)self,target,kind,slot,methods[kind].size());for(auto entry:methods[kind])fprintf(output,"GUARD_KNOWN target=%p original=%p\n",entry.first,entry.second);fflush(output);}}return it==methods[kind].end()?nullptr:(F)it->second;}
static bool Patch(void* self,unsigned slot,unsigned kind,void* detour){std::lock_guard<std::recursive_mutex> lock(mx);void** table=*(void***)self;void* target=table[slot];auto& entries=methods[kind];if(entries.count(target)){tables[kind][table]=entries[target];return entries[target]!=nullptr;}auto it=entries.emplace(target,nullptr).first;MH_STATUS hr=VIMH_CreateHook(target,detour,&it->second);if(hr==MH_OK)hr=VIMH_EnableHook(target);if(hr!=MH_OK||!it->second){{std::lock_guard<std::mutex> logLock(logMx);if(output){fprintf(output,"GUARD_PATCH_FAILED kind=%u slot=%u target=%p hr=%s original=%p\n",kind,slot,target,VIMH_StatusToString(hr),it->second);fflush(output);}}entries.erase(it);return false;}tables[kind][table]=it->second;return true;}
static bool Permitted(const char* name,D3DPRESENT_PARAMETERS* pp,D3DDISPLAYMODEEX* mode=nullptr){bool legal=pp&&pp->Windowed&&!mode;if(!pp||!pp->Windowed)++blocked;else if(mode)++badMode;else ++normal;
 std::lock_guard<std::mutex> lock(logMx);if(output){fprintf(output,"NATIVE_%s windowed=%d mode=%d width=%u height=%u refresh=%u allowed=%d\n",name,pp?(int)pp->Windowed:-1,mode?1:0,pp?pp->BackBufferWidth:0,pp?pp->BackBufferHeight:0,pp?pp->FullScreen_RefreshRateInHz:0,(int)legal);fflush(output);}return legal;
}
static bool Device(IDirect3DDevice9*);
using Create=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
static HRESULT STDMETHODCALLTYPE GuardCreate(IDirect3D9* s,UINT a,D3DDEVTYPE t,HWND h,DWORD f,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out){if(!Permitted("CREATE",p))return D3DERR_INVALIDCALL;auto fn=Original<Create>(s,16,CREATE);if(!fn)return D3DERR_INVALIDCALL;HRESULT hr=fn(s,a,t,h,f,p,out);{std::lock_guard<std::mutex> lock(logMx);if(output){fprintf(output,"REAL_CREATE_RETURN hr=0x%08lX device=%p pp=%p\n",(unsigned long)hr,out?*out:nullptr,p);fflush(output);}}if(SUCCEEDED(hr)&&out&&*out&&!Device(*out)){(*out)->Release();*out=nullptr;return D3DERR_INVALIDCALL;}return hr;}
using CreateEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
static HRESULT STDMETHODCALLTYPE GuardCreateEx(IDirect3D9Ex* s,UINT a,D3DDEVTYPE t,HWND h,DWORD f,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m,IDirect3DDevice9Ex** out){if(!Permitted("CREATEEX",p,m))return D3DERR_INVALIDCALL;auto fn=Original<CreateEx>(s,20,CREATEEX);if(!fn)return D3DERR_INVALIDCALL;HRESULT hr=fn(s,a,t,h,f,p,m,out);if(SUCCEEDED(hr)&&out&&*out&&!Device(*out)){(*out)->Release();*out=nullptr;return D3DERR_INVALIDCALL;}return hr;}
using Reset=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
static HRESULT STDMETHODCALLTYPE GuardReset(IDirect3DDevice9* s,D3DPRESENT_PARAMETERS* p){if(!Permitted("RESET",p))return D3DERR_INVALIDCALL;auto fn=Original<Reset>(s,16,RESET);return fn?fn(s,p):D3DERR_INVALIDCALL;}
using ResetEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
static HRESULT STDMETHODCALLTYPE GuardResetEx(IDirect3DDevice9Ex* s,D3DPRESENT_PARAMETERS* p,D3DDISPLAYMODEEX* m){if(!Permitted("RESETEX",p,m))return D3DERR_INVALIDCALL;auto fn=Original<ResetEx>(s,132,RESETEX);return fn?fn(s,p,m):D3DERR_INVALIDCALL;}
using Additional=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9**);
static HRESULT STDMETHODCALLTYPE GuardAdditional(IDirect3DDevice9* s,D3DPRESENT_PARAMETERS* p,IDirect3DSwapChain9** out){if(!Permitted("ADDITIONAL",p))return D3DERR_INVALIDCALL;auto fn=Original<Additional>(s,13,ADDITIONAL);return fn?fn(s,p,out):D3DERR_INVALIDCALL;}
static bool Device(IDirect3DDevice9* s){bool ok=Patch(s,16,RESET,(void*)&GuardReset)&&Patch(s,13,ADDITIONAL,(void*)&GuardAdditional);IDirect3DDevice9Ex* ex=nullptr;if(SUCCEEDED(s->QueryInterface(IID_IDirect3DDevice9Ex,(void**)&ex))&&ex){ok=ok&&Patch(ex,16,RESET,(void*)&GuardReset)&&Patch(ex,13,ADDITIONAL,(void*)&GuardAdditional)&&Patch(ex,132,RESETEX,(void*)&GuardResetEx);ex->Release();}return ok;}
static bool Interface(IDirect3D9* s){MH_STATUS init=VIMH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;bool ok=Patch(s,16,CREATE,(void*)&GuardCreate);IDirect3D9Ex* ex=nullptr;if(SUCCEEDED(s->QueryInterface(IID_IDirect3D9Ex,(void**)&ex))&&ex){ok=ok&&Patch(ex,16,CREATE,(void*)&GuardCreate)&&Patch(ex,20,CREATEEX,(void*)&GuardCreateEx);ex->Release();}return ok;}
static HMODULE Native(){std::lock_guard<std::recursive_mutex> lock(mx);if(!module){wchar_t path[MAX_PATH]{};UINT n=GetSystemDirectoryW(path,MAX_PATH);if(!n||n+10>=MAX_PATH)return nullptr;wcscat_s(path,L"\\d3d9.dll");module=LoadLibraryW(path);}return module;}
extern "C" __declspec(dllexport) IDirect3D9* WINAPI GuardFactory(UINT sdk){HMODULE m=Native();if(!m)return nullptr;using F=IDirect3D9*(WINAPI*)(UINT);auto fn=(F)GetProcAddress(m,"Direct3DCreate9");if(!fn)return nullptr;auto native=fn(sdk);if(native&&!Interface(native)){native->Release();return nullptr;}return native;}
extern "C" __declspec(dllexport) HRESULT WINAPI GuardFactoryEx(UINT sdk,IDirect3D9Ex** out){HMODULE m=Native();if(!m)return D3DERR_INVALIDCALL;using F=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);auto fn=(F)GetProcAddress(m,"Direct3DCreate9Ex");if(!fn)return D3DERR_INVALIDCALL;HRESULT hr=fn(sdk,out);if(SUCCEEDED(hr)&&out&&*out&&!Interface(*out)){(*out)->Release();*out=nullptr;return D3DERR_INVALIDCALL;}return hr;}
extern "C" __declspec(dllexport) void WINAPI GuardLog(const wchar_t* path){std::lock_guard<std::mutex> lock(logMx);if(!output)output=_wfsopen(path,L"w",_SH_DENYNO);}
extern "C" __declspec(dllexport) void WINAPI GuardStats(LONG* out){if(out){out[0]=blocked;out[1]=normal;out[2]=badMode;}}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
