// D3D9/9Ex fullscreen parameters are translated at native method boundaries.
// Shared method-code hooks do not retain or replace application COM vtables.
std::recursive_mutex g_d9Mx;std::map<void*,void*> g_d9Methods[8];std::atomic<bool> g_d9Hooked{false},g_d9Blocked{false};std::atomic<HMODULE> g_d9Module{nullptr},g_d9Retained{nullptr};
std::map<void*,void*> g_d9Tables[8];std::map<HMODULE,bool> g_d9CodeModules;
std::atomic<long> g_d9Requests{0},g_d9Emulated{0},g_d9Refused{0},g_d9Failed{0};std::atomic<HRESULT> g_d9LastResult{S_OK};
#include "vi_d3d9_probe_policy.inl"
enum{D9_CREATE=0,D9_CREATEEX=1,D9_RESET=2,D9_RESETEX=3,D9_ADDITIONAL=4,D9_PRESENT=5,D9_PRESENTEX=6,D9_CHAINPRESENT=7};
// Another interceptor can replace a table entry while this call is in flight.
// Dispatch from the table observed at admission, not its mutable current entry.
template<class F>F D9Original(void* self,unsigned slot,unsigned method){std::lock_guard<std::recursive_mutex> lock(g_d9Mx);void** table=*(void***)self;auto cached=g_d9Tables[method].find(table);if(cached!=g_d9Tables[method].end())return(F)cached->second;auto it=g_d9Methods[method].find(table[slot]);return it==g_d9Methods[method].end()?nullptr:(F)it->second;}
bool D9Patch(void* self,unsigned slot,unsigned method,void* detour,const char* name){std::lock_guard<std::recursive_mutex> lock(g_d9Mx);void** table=*(void***)self;void* target=table[slot];auto& entries=g_d9Methods[method];if(target==detour)return g_d9Tables[method].count(table)!=0;
 for(unsigned i=0;i<8;++i)if(i!=method&&g_d9Methods[i].count(target)){Log("d3d9: incompatible method entry alias refused");return false;}
 if(entries.count(target)){g_d9Tables[method][table]=entries[target];return entries[target]!=nullptr;}
 HMODULE held=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)target,&held)){Log("d3d9: method implementation has no retainable module");return false;}if(g_d9CodeModules.count(held))FreeLibrary(held);else g_d9CodeModules[held]=true;
 auto it=entries.emplace(target,nullptr).first;bool ok=HookAddr(name,target,detour,&it->second);if(!ok||!it->second){entries.erase(it);return false;}g_d9Tables[method][table]=it->second;return true;}
HRESULT D9Window(HWND window,UINT width,UINT height){if(!FullscreenOwnWindow(window)||GetAncestor(window,GA_ROOT)!=window)return D3DERR_INVALIDCALL;
 // Windowed presentation does not require replacing the game's window style.
 // Preserve its frame/border policy and size that actual style's client area.
 DWORD style=(DWORD)GetWindowLongPtrW(window,GWL_STYLE),ex=(DWORD)GetWindowLongPtrW(window,GWL_EXSTYLE);RECT r{0,0,(LONG)width,(LONG)height};if(!AdjustWindowRectEx(&r,style,GetMenu(window)!=nullptr,ex&~WS_EX_TOPMOST))return D3DERR_INVALIDCALL;
 UINT flags=SWP_NOMOVE|SWP_NOACTIVATE;if(!(ex&WS_EX_TOPMOST))flags|=SWP_NOZORDER;
 BOOL ok=o_SetWindowPos?o_SetWindowPos(window,HWND_NOTOPMOST,0,0,r.right-r.left,r.bottom-r.top,flags):SetWindowPos(window,HWND_NOTOPMOST,0,0,r.right-r.left,r.bottom-r.top,flags);RECT client{};
 return ok&&GetClientRect(window,&client)&&(UINT)client.right==width&&(UINT)client.bottom==height&&!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)?S_OK:D3DERR_INVALIDCALL;
}
template<class F>HRESULT D9Parameters(D3DPRESENT_PARAMETERS* params,HWND focus,DWORD behavior,F native){if(!params)return D3DERR_INVALIDCALL;if(!g_cfg.nofullscreen||params->Windowed)return native(params,false);++g_d9Requests;
 if(g_fullscreenPolicy==1){g_d9ProbeAdmissions.fetch_or(1u);++g_d9Refused;g_d9Blocked=true;g_d9LastResult=D3DERR_INVALIDCALL;return D3DERR_INVALIDCALL;}
 HWND window=params->hDeviceWindow?params->hDeviceWindow:focus;UINT width=params->BackBufferWidth,height=params->BackBufferHeight;
 if((behavior&D3DCREATE_ADAPTERGROUP_DEVICE)||!FullscreenOwnWindow(window)||!width||!height||width>8192||height>8192||(unsigned long long)width*height>16*1024*1024){++g_d9Failed;g_d9LastResult=D3DERR_INVALIDCALL;return D3DERR_INVALIDCALL;}
 std::unique_lock<std::mutex> transaction(g_ddModeMx,std::try_to_lock);if(!transaction.owns_lock())return D3DERR_WASSTILLDRAWING;
 HRESULT hr=D9Window(window,width,height);if(FAILED(hr)){++g_d9Failed;g_d9LastResult=D3DERR_INVALIDCALL;return D3DERR_INVALIDCALL;}
 D3DPRESENT_PARAMETERS copy=*params;copy.Windowed=TRUE;copy.FullScreen_RefreshRateInHz=0;copy.hDeviceWindow=window;hr=native(&copy,true);*params=copy;g_d9LastResult=hr;
 if(SUCCEEDED(hr)){++g_d9Emulated;g_d9Blocked=false;}else ++g_d9Failed;return hr;
}
#include "vi_d3d9_diagnostics.inl"
#include "vi_cap9.inl"
bool D9Device(IDirect3DDevice9*);
using D9Create=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
HRESULT STDMETHODCALLTYPE h_D9Create(IDirect3D9* self,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out){D9OperationCall observed(D9_CREATE);auto original=D9Original<D9Create>(self,16,D9_CREATE);if(!original)return observed.Finish(D3DERR_INVALIDCALL);HRESULT hr=D9Parameters(pp,focus,flags,[&](D3DPRESENT_PARAMETERS* p,bool){return observed.Native([&]{return original(self,adapter,type,focus,flags,p,out);});});if(SUCCEEDED(hr)&&out&&*out&&!D9Device(*out)){(*out)->Release();*out=nullptr;return observed.Finish(D3DERR_INVALIDCALL);}return observed.Finish(hr);}
using D9CreateEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
HRESULT STDMETHODCALLTYPE h_D9CreateEx(IDirect3D9Ex* self,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,D3DPRESENT_PARAMETERS* pp,D3DDISPLAYMODEEX* mode,IDirect3DDevice9Ex** out){D9OperationCall observed(D9_CREATEEX);auto original=D9Original<D9CreateEx>(self,20,D9_CREATEEX);if(!original)return observed.Finish(D3DERR_INVALIDCALL);HRESULT hr=D9Parameters(pp,focus,flags,[&](D3DPRESENT_PARAMETERS* p,bool changed){return observed.Native([&]{return original(self,adapter,type,focus,flags,p,changed?nullptr:mode,out);});});if(SUCCEEDED(hr)&&out&&*out&&!D9Device(*out)){(*out)->Release();*out=nullptr;return observed.Finish(D3DERR_INVALIDCALL);}return observed.Finish(hr);}
using D9Reset=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE h_D9Reset(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* pp){D9OperationCall observed(D9_RESET);auto original=D9Original<D9Reset>(self,16,D9_RESET);if(!original)return observed.Finish(D3DERR_INVALIDCALL);D3DDEVICE_CREATION_PARAMETERS cp{};HRESULT hr=self->GetCreationParameters(&cp);if(FAILED(hr))return observed.Finish(hr);hr=D9Parameters(pp,cp.hFocusWindow,cp.BehaviorFlags,[&](D3DPRESENT_PARAMETERS* p,bool){return observed.Native([&]{return original(self,p);});});return observed.Finish(hr);}
using D9ResetEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
HRESULT STDMETHODCALLTYPE h_D9ResetEx(IDirect3DDevice9Ex* self,D3DPRESENT_PARAMETERS* pp,D3DDISPLAYMODEEX* mode){D9OperationCall observed(D9_RESETEX);auto original=D9Original<D9ResetEx>(self,132,D9_RESETEX);if(!original)return observed.Finish(D3DERR_INVALIDCALL);D3DDEVICE_CREATION_PARAMETERS cp{};HRESULT hr=self->GetCreationParameters(&cp);if(FAILED(hr))return observed.Finish(hr);hr=D9Parameters(pp,cp.hFocusWindow,cp.BehaviorFlags,[&](D3DPRESENT_PARAMETERS* p,bool changed){return observed.Native([&]{return original(self,p,changed?nullptr:mode);});});return observed.Finish(hr);}
using D9Additional=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9**);
HRESULT STDMETHODCALLTYPE h_D9Additional(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* pp,IDirect3DSwapChain9** out){D9OperationCall observed(D9_ADDITIONAL);auto original=D9Original<D9Additional>(self,13,D9_ADDITIONAL);if(!original)return observed.Finish(D3DERR_INVALIDCALL);D3DDEVICE_CREATION_PARAMETERS cp{};HRESULT hr=self->GetCreationParameters(&cp);if(FAILED(hr))return observed.Finish(hr);hr=D9Parameters(pp,cp.hFocusWindow,cp.BehaviorFlags,[&](D3DPRESENT_PARAMETERS* p,bool){return observed.Native([&]{return original(self,p,out);});});if(SUCCEEDED(hr)&&out&&*out)D9Chain(*out);return observed.Finish(hr);}
bool D9Device(IDirect3DDevice9* device){bool ok=D9Patch(device,16,D9_RESET,(void*)&h_D9Reset,"IDirect3DDevice9::Reset")&&D9Patch(device,13,D9_ADDITIONAL,(void*)&h_D9Additional,"IDirect3DDevice9::CreateAdditionalSwapChain");IDirect3DDevice9Ex* ex=nullptr;if(SUCCEEDED(device->QueryInterface(IID_IDirect3DDevice9Ex,(void**)&ex))&&ex){ok=ok&&D9Patch(ex,16,D9_RESET,(void*)&h_D9Reset,"IDirect3DDevice9Ex::Reset")&&D9Patch(ex,13,D9_ADDITIONAL,(void*)&h_D9Additional,"IDirect3DDevice9Ex::CreateAdditionalSwapChain")&&D9Patch(ex,132,D9_RESETEX,(void*)&h_D9ResetEx,"IDirect3DDevice9Ex::ResetEx");ex->Release();}D9ObserveDevice(device);return ok;}
bool D9Interface(IDirect3D9* native){bool ok=D9Patch(native,16,D9_CREATE,(void*)&h_D9Create,"IDirect3D9::CreateDevice");IDirect3D9Ex* ex=nullptr;if(SUCCEEDED(native->QueryInterface(IID_IDirect3D9Ex,(void**)&ex))&&ex){ok=ok&&D9Patch(ex,16,D9_CREATE,(void*)&h_D9Create,"IDirect3D9Ex::CreateDevice")&&D9Patch(ex,20,D9_CREATEEX,(void*)&h_D9CreateEx,"IDirect3D9Ex::CreateDeviceEx");ex->Release();}return ok;}
using D9Factory=IDirect3D9*(WINAPI*)(UINT);using D9FactoryEx=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);D9Factory o_D9Factory=nullptr;D9FactoryEx o_D9FactoryEx=nullptr;
std::atomic<void*> g_d9FactoryTargets[2]{};std::atomic<bool> g_d9FactoryHooksReady{false};
IDirect3D9* WINAPI h_D9Factory(UINT);HRESULT WINAPI h_D9FactoryEx(UINT,IDirect3D9Ex**);
#include "vi_d3d9_lifetime.inl"
IDirect3D9* WINAPI h_D9Factory(UINT sdk){if(!o_D9Factory||!D9Retain(0))return nullptr;auto native=o_D9Factory(sdk);if(native&&!D9Interface(native)){native->Release();return nullptr;}return native;}
HRESULT WINAPI h_D9FactoryEx(UINT sdk,IDirect3D9Ex** out){if(!o_D9FactoryEx||!D9Retain(1))return D3DERR_INVALIDCALL;HRESULT hr=o_D9FactoryEx(sdk,out);if(SUCCEEDED(hr)&&out&&*out&&!D9Interface(*out)){(*out)->Release();*out=nullptr;return D3DERR_INVALIDCALL;}return hr;}
void TryHookD9(HMODULE module=nullptr){if(g_d9Hooked)return;if(!module)module=GetModuleHandleW(L"d3d9.dll");if(!module||g_d9Hooked.exchange(true))return;g_d9Module=module;auto target=(void*)GetProcAddress(module,"Direct3DCreate9"),extended=(void*)GetProcAddress(module,"Direct3DCreate9Ex");g_d9FactoryTargets[0]=target;g_d9FactoryTargets[1]=extended;HookAddr("Direct3DCreate9",target,(void*)&h_D9Factory,(void**)&o_D9Factory);HookAddr("Direct3DCreate9Ex",extended,(void*)&h_D9FactoryEx,(void**)&o_D9FactoryEx);g_d9FactoryHooksReady.store(true,std::memory_order_release);}
bool D9BlockedBeforePresent(){return g_cfg.nofullscreen&&g_d9Blocked&&g_d9Refused>0&&C.present_ours==0;}
#include "vi_d3d9_late.inl"
