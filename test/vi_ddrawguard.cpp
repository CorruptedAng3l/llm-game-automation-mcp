// Test-only native boundary guard. Real DirectDraw object vtables are untouched.
// Native NORMAL calls run; native exclusive cooperative/mode calls never run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <share.h>
#include "vi_mh_rename.h"
#include "MinHook.h"
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"ole32.lib")
static std::recursive_mutex guardMx;static std::mutex logMx;static FILE* output;static HMODULE nativeModule;static std::map<void*,void*> cooperative;
static std::atomic<LONG> exclusiveCalls{0},modeCalls{0},normalCalls{0};
static HRESULT STDMETHODCALLTYPE GuardCoop(void* self,HWND window,DWORD flags){bool exclusive=(flags&(DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN))!=0;
 {std::lock_guard<std::mutex> lock(logMx);if(output){fprintf(output,"NATIVE_COOP flags=0x%08lX blocked=%d\n",flags,(int)exclusive);fflush(output);}}
 if(exclusive){++exclusiveCalls;return DDERR_NOEXCLUSIVEMODE;}++normalCalls;void* original=nullptr;{std::lock_guard<std::recursive_mutex> lock(guardMx);auto it=cooperative.find((*(void***)self)[20]);if(it!=cooperative.end())original=it->second;}if(!original)return DDERR_GENERIC;
 using Fn=HRESULT(STDMETHODCALLTYPE*)(void*,HWND,DWORD);return ((Fn)original)(self,window,flags);
}
static HRESULT STDMETHODCALLTYPE GuardMode1(void*,DWORD w,DWORD h,DWORD bpp){++modeCalls;std::lock_guard<std::mutex> lock(logMx);if(output){fprintf(output,"NATIVE_MODE blocked=1 width=%lu height=%lu bpp=%lu\n",w,h,bpp);fflush(output);}return DDERR_NOEXCLUSIVEMODE;}
static HRESULT STDMETHODCALLTYPE GuardMode2(void* self,DWORD w,DWORD h,DWORD bpp,DWORD,DWORD){return GuardMode1(self,w,h,bpp);}
static bool Hook(void* target,void* detour,void** original){MH_STATUS hr=VIMH_CreateHook(target,detour,original);if(hr==MH_ERROR_ALREADY_CREATED)return true;if(hr!=MH_OK)return false;return VIMH_EnableHook(target)==MH_OK;}
static bool GuardOne(IUnknown* native,unsigned version){std::lock_guard<std::recursive_mutex> lock(guardMx);MH_STATUS init=VIMH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;void** table=*(void***)native;
 if(!cooperative.count(table[20])){void* original=nullptr;if(!Hook(table[20],(void*)&GuardCoop,&original)||!original)return false;cooperative[table[20]]=original;}
 void* unused=nullptr;return Hook(table[21],version==0?(void*)&GuardMode1:(void*)&GuardMode2,&unused);
}
static bool GuardAll(IUnknown* native){std::lock_guard<std::recursive_mutex> lock(guardMx);
 const GUID* ids[]={&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw4,&IID_IDirectDraw7};bool protectedOne=false;
 for(unsigned i=0;i<4;i++){IUnknown* alias=nullptr;if(FAILED(native->QueryInterface(*ids[i],(void**)&alias))||!alias)continue;bool ok=GuardOne(alias,i);alias->Release();if(!ok)return false;protectedOne=true;
 }return protectedOne;
}
static HMODULE RealModule(){std::lock_guard<std::recursive_mutex> lock(guardMx);if(!nativeModule){wchar_t path[MAX_PATH]{};UINT n=GetSystemDirectoryW(path,MAX_PATH);if(!n||n+11>=MAX_PATH)return nullptr;wcscat_s(path,L"\\ddraw.dll");nativeModule=LoadLibraryW(path);}return nativeModule;}
extern "C" __declspec(dllexport) HRESULT WINAPI GuardCreate(GUID* guid,IDirectDraw** out,IUnknown* outer){HMODULE module=RealModule();if(!module)return DDERR_GENERIC;using Fn=HRESULT(WINAPI*)(GUID*,IDirectDraw**,IUnknown*);auto fn=(Fn)GetProcAddress(module,"DirectDrawCreate");if(!fn)return DDERR_GENERIC;HRESULT hr=fn(guid,out,outer);if(SUCCEEDED(hr)&&out&&*out&&!GuardAll(*out)){(*out)->Release();*out=nullptr;return DDERR_GENERIC;}return hr;}
extern "C" __declspec(dllexport) HRESULT WINAPI GuardCreateEx(GUID* guid,void** out,REFIID id,IUnknown* outer){HMODULE module=RealModule();if(!module)return DDERR_GENERIC;using Fn=HRESULT(WINAPI*)(GUID*,void**,REFIID,IUnknown*);auto fn=(Fn)GetProcAddress(module,"DirectDrawCreateEx");if(!fn)return DDERR_GENERIC;HRESULT hr=fn(guid,out,id,outer);if(SUCCEEDED(hr)&&out&&*out&&!GuardAll((IUnknown*)*out)){((IUnknown*)*out)->Release();*out=nullptr;return DDERR_GENERIC;}return hr;}
extern "C" __declspec(dllexport) void WINAPI GuardLog(const wchar_t* path){std::lock_guard<std::mutex> lock(logMx);if(!output)output=_wfsopen(path,L"w",_SH_DENYNO);}
extern "C" __declspec(dllexport) void WINAPI GuardStats(LONG* counts){if(counts){counts[0]=exclusiveCalls;counts[1]=modeCalls;counts[2]=normalCalls;}}
// Called before injection. Prime both COM classes while all pointers are native,
// so the guard is below the product's CoCreateInstance wrapper as well.
extern "C" __declspec(dllexport) HRESULT WINAPI GuardPrime(){HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized))return initialized;
 const GUID* ids[]={&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw4,&IID_IDirectDraw7};for(const GUID* cls:{&CLSID_DirectDraw,&CLSID_DirectDraw7}){for(unsigned i=0;i<4;i++){IUnknown* native=nullptr;HRESULT hr=CoCreateInstance(*cls,nullptr,CLSCTX_INPROC_SERVER,*ids[i],(void**)&native);if(FAILED(hr)||!native)return FAILED(hr)?hr:E_NOINTERFACE;using Initialize=HRESULT(STDMETHODCALLTYPE*)(void*,GUID*);HRESULT setup=((Initialize)(*(void***)native)[18])(native,nullptr);bool ok=(SUCCEEDED(setup)||setup==DDERR_ALREADYINITIALIZED)&&GuardOne(native,i);native->Release();if(!ok)return DDERR_GENERIC;}}return DD_OK;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD why,LPVOID){if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
