#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ddraw.h>
#include <shellapi.h>
#include <cstdio>
#include <vector>
#include <string>
#include <type_traits>
#include <share.h>
#include "vi_pattern.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ddraw.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"ole32.lib")
static FILE* output;static HWND window;static bool go=false,quit=false,stopRender=false;static unsigned phase=0;
static void Log(const char* text,HRESULT hr=DD_OK){fprintf(output,"%s hr=0x%08lX\n",text,(unsigned long)hr);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+1){go=true;return 0;}if(m==WM_APP+2){phase=(unsigned)w;fprintf(output,"PHASE %u\n",phase);fflush(output);return 0;}if(m==WM_APP+3){stopRender=true;return 0;}if(m==WM_DESTROY){quit=true;PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
static void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){quit=true;return;}TranslateMessage(&m);DispatchMessageW(&m);}}
template<class Draw> static HRESULT Mode(Draw* draw){if constexpr(std::is_same_v<Draw,IDirectDraw>)return draw->SetDisplayMode(800,600,32);else return draw->SetDisplayMode(800,600,32,0,0);}
template<class Surface,class Description> static HRESULT Fill(Surface* surface,unsigned frame,bool solid=false,DWORD color=0){Description d{};d.dwSize=sizeof(d);HRESULT hr=surface->Lock(nullptr,&d,DDLOCK_WAIT,nullptr);if(FAILED(hr))return hr;
 auto caps=d.ddsCaps;HRESULT capResult=surface->GetCaps(&caps);DWORD roles=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FRONTBUFFER|DDSCAPS_BACKBUFFER|DDSCAPS_FLIP|DDSCAPS_COMPLEX|DDSCAPS_SYSTEMMEMORY;if(FAILED(capResult)||((caps.dwCaps^d.ddsCaps.dwCaps)&roles)){surface->Unlock(nullptr);return DDERR_INVALIDCAPS;}
 std::vector<BYTE> row(800*4);for(int y=0;y<600;y++){if(solid){for(int x=0;x<800;x++)((DWORD*)row.data())[x]=color;}else vipat::Row(vipat::BGRA8,y,800,frame,row.data());memcpy((BYTE*)d.lpSurface+(ptrdiff_t)y*d.lPitch,row.data(),row.size());}
 return surface->Unlock(nullptr);
}
template<class Surface,class Description> static bool SolidPixel(Surface* surface,DWORD color){Description d{};d.dwSize=sizeof(d);HRESULT hr=surface->Lock(nullptr,&d,DDLOCK_WAIT,nullptr);if(FAILED(hr))return false;DWORD pixel=*(DWORD*)d.lpSurface;surface->Unlock(nullptr);return (pixel&0xffffff)==(color&0xffffff);}
template<class T,class D>static bool Alias(IUnknown* source,REFIID iid,IUnknown* expected){T* p=nullptr;HRESULT hr=source->QueryInterface(iid,(void**)&p);if(FAILED(hr)||!p)return false;IUnknown* identity=nullptr;hr=p->QueryInterface(IID_IUnknown,(void**)&identity);D d{};d.dwSize=sizeof(d);bool ok=SUCCEEDED(hr)&&identity==expected&&SUCCEEDED(p->GetSurfaceDesc(&d))&&d.dwWidth==800&&d.dwHeight==600&&(d.ddsCaps.dwCaps&DDSCAPS_PRIMARYSURFACE);fprintf(output,"ALIAS iid=%08lX query_hr=0x%08lX identity=%p expected=%p desc=%lux%lu caps=%08lX ok=%d\n",iid.Data1,(unsigned long)hr,identity,expected,d.dwWidth,d.dwHeight,d.ddsCaps.dwCaps,(int)ok);fflush(output);if(identity)identity->Release();p->Release();return ok;}
static bool Aliases(IUnknown* surface){IUnknown* id=nullptr;if(FAILED(surface->QueryInterface(IID_IUnknown,(void**)&id)))return false;bool ok=Alias<IDirectDrawSurface,DDSURFACEDESC>(surface,IID_IDirectDrawSurface,id)&&Alias<IDirectDrawSurface2,DDSURFACEDESC>(surface,IID_IDirectDrawSurface2,id)&&Alias<IDirectDrawSurface3,DDSURFACEDESC>(surface,IID_IDirectDrawSurface3,id)&&Alias<IDirectDrawSurface4,DDSURFACEDESC2>(surface,IID_IDirectDrawSurface4,id)&&Alias<IDirectDrawSurface7,DDSURFACEDESC2>(surface,IID_IDirectDrawSurface7,id);id->Release();return ok;}
template<class Draw,class Surface,class Description,class Caps> static int Run(Draw* draw,bool initialize=false){
 if(initialize){HRESULT setup=draw->Initialize(nullptr);Log("INITIALIZE_SELECTED_INTERFACE",setup);if(FAILED(setup)&&setup!=DDERR_ALREADYINITIALIZED)return 6;}
 HRESULT coop=draw->SetCooperativeLevel(window,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN|DDSCL_ALLOWREBOOT),mode=Mode(draw);Log("REQUEST_COOP",coop);Log("REQUEST_MODE",mode);
 HRESULT normal=draw->SetCooperativeLevel(window,DDSCL_NORMAL);Log("NORMAL_POSITIVE",normal);if(FAILED(coop)||FAILED(mode)||FAILED(normal))return 10;
 Description current{};current.dwSize=sizeof(current);HRESULT hr=draw->GetDisplayMode(&current);fprintf(output,"VIRTUAL_MODE width=%lu height=%lu bpp=%lu hr=0x%08lX\n",current.dwWidth,current.dwHeight,current.ddpfPixelFormat.dwRGBBitCount,(unsigned long)hr);fflush(output);if(FAILED(hr)||current.dwWidth!=800||current.dwHeight!=600||current.ddpfPixelFormat.dwRGBBitCount!=32)return 11;
 Description desc{};desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS|DDSD_BACKBUFFERCOUNT;desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX;desc.dwBackBufferCount=1;Surface *front=nullptr,*back=nullptr,*source=nullptr;
 hr=draw->CreateSurface(&desc,&front,nullptr);Log("CREATE_PRIMARY",hr);if(FAILED(hr)||!front)return 12;
 Caps caps{};caps.dwCaps=DDSCAPS_BACKBUFFER;hr=front->GetAttachedSurface(&caps,&back);Log("GET_BACK",hr);if(FAILED(hr)||!back){front->Release();return 13;}
 Description actual{};actual.dwSize=sizeof(actual);hr=front->GetSurfaceDesc(&actual);fprintf(output,"SURFACE width=%lu height=%lu caps=0x%08lX back_count=%lu hr=0x%08lX\n",actual.dwWidth,actual.dwHeight,actual.ddsCaps.dwCaps,actual.dwBackBufferCount,(unsigned long)hr);fflush(output);
 int result=0;if(FAILED(hr)||actual.dwWidth!=800||actual.dwHeight!=600||(actual.ddsCaps.dwCaps&DDSCAPS_PRIMARYSURFACE)==0||actual.dwBackBufferCount!=1)result=14;
 if(!Aliases(front)){Log("FAIL_ALIASES");result=18;}else Log("ALIASES_OK");
 if(!result){hr=Fill<Surface,Description>(front,0,true,0x00ff0000);if(SUCCEEDED(hr))hr=Fill<Surface,Description>(back,0,true,0x000000ff);if(SUCCEEDED(hr))hr=front->Flip(nullptr,DDFLIP_WAIT);
  bool swapped=SUCCEEDED(hr)&&SolidPixel<Surface,Description>(front,0x000000ff)&&SolidPixel<Surface,Description>(back,0x00ff0000);Log(swapped?"SWAP_OK":"FAIL_SWAP",hr);if(!swapped)result=15;}
 Description off{};off.dwSize=sizeof(off);off.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;off.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;off.dwWidth=800;off.dwHeight=600;
 if(!result){hr=draw->CreateSurface(&off,&source,nullptr);Log("CREATE_SOURCE",hr);if(FAILED(hr))result=16;}
 unsigned frame=0,lastPhase=~0u;ULONGLONG next=0,deadline=GetTickCount64()+90000;
 if(!result)Log("RENDER_READY");
 while(!result&&!quit&&!stopRender&&GetTickCount64()<deadline){Pump();if(GetTickCount64()>=next){next=GetTickCount64()+20;Surface* target=phase?source:back;hr=Fill<Surface,Description>(target,frame++);
   if(SUCCEEDED(hr))hr=phase==2?front->BltFast(0,0,source,nullptr,DDBLTFAST_WAIT):phase?front->Blt(nullptr,source,nullptr,DDBLT_WAIT,nullptr):front->Flip(nullptr,DDFLIP_WAIT);
   if(FAILED(hr)){Log("FAIL_RENDER",hr);result=17;break;}if(lastPhase!=phase){fprintf(output,"DRAWING phase=%u frame=%u\n",phase,frame);fflush(output);lastPhase=phase;}}
  Sleep(1);
 }
 if(source)source->Release();front->Release();front=nullptr;
 Caps frontCaps{};frontCaps.dwCaps=DDSCAPS_FRONTBUFFER;Surface* restored=nullptr;HRESULT attached=back->GetAttachedSurface(&frontCaps,&restored);bool retained=SUCCEEDED(attached)&&restored&&Aliases(restored);if(restored)restored->Release();Log(retained?"BACK_RETAINS_FRONT_OK":"FAIL_BACK_RETAINS_FRONT",attached);if(!retained)result=19;
 back->Release();Log("SURFACES_RELEASED");return result;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<3)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(!output)return 3;int api=_wtoi(argv[2]);bool probe=argc>3&&wcscmp(argv[3],L"probe")==0,legacyFactory=argc>3&&wcscmp(argv[3],L"create")==0,com7=argc>3&&wcscmp(argv[3],L"com7")==0,com1=argc>3&&wcscmp(argv[3],L"com1")==0;std::wstring boundary=std::wstring(argv[1])+L".boundary.log";LocalFree(argv);
 HMODULE guard=GetModuleHandleW(L"ddraw.dll");using SetLog=void(WINAPI*)(const wchar_t*);using Stats=void(WINAPI*)(LONG*);auto setLog=(SetLog)GetProcAddress(guard,"ViGuardLog");auto stats=(Stats)GetProcAddress(guard,"ViGuardStats");if(!setLog||!stats){Log("NOTRUN_NATIVE_BOUNDARY_GUARD_MISSING");return 4;}setLog(boundary.c_str());
 if(com7||com1){using Prime=HRESULT(WINAPI*)();auto prime=(Prime)GetProcAddress(guard,"ViGuardPrime");if(!prime||FAILED(prime())){Log("NOTRUN_COM_GUARD_PRIME_FAILED");return 4;}Log("COM_GUARD_PRIMED_BEFORE_INJECTION");}
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_ddraw_windowed";RegisterClassW(&c);window=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"DDraw800x600 guarded",WS_POPUP,-28000,600,800,600,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);Log("READY_GUARDED");
 if(probe){IDirectDraw7* n=nullptr;HRESULT hr=DirectDrawCreateEx(nullptr,(void**)&n,IID_IDirectDraw7,nullptr);Log("PROBE_FACTORY",hr);if(n){Log("PROBE_NORMAL_BEFORE",n->SetCooperativeLevel(window,DDSCL_NORMAL));ULONG refs=n->AddRef();fprintf(output,"PROBE_ADDREF %lu\n",refs);Log("PROBE_NORMAL_AFTER",n->SetCooperativeLevel(window,DDSCL_NORMAL));for(const GUID* id:{&IID_IUnknown,&IID_IDirectDraw,&IID_IDirectDraw4}){IUnknown* q=nullptr;hr=n->QueryInterface(*id,(void**)&q);fprintf(output,"PROBE_QUERY iid=%08lX hr=0x%08lX\n",id->Data1,(unsigned long)hr);if(q)q->Release();}n->Release();n->Release();}DestroyWindow(window);fclose(output);return 0;}
 while(!go&&!quit){Pump();Sleep(1);}IDirectDraw7* base=nullptr;HRESULT hr=DD_OK;if(com7||com1){IUnknown* unknown=nullptr;hr=CoCreateInstance(com7?CLSID_DirectDraw7:CLSID_DirectDraw,nullptr,CLSCTX_INPROC_SERVER,IID_IUnknown,(void**)&unknown);Log("CREATE_COM_UNKNOWN",hr);if(SUCCEEDED(hr)&&unknown){hr=unknown->QueryInterface(IID_IDirectDraw7,(void**)&base);unknown->Release();}}
 else if(legacyFactory){IDirectDraw* old=nullptr;hr=DirectDrawCreate(nullptr,&old,nullptr);Log("CREATE_LEGACY",hr);if(SUCCEEDED(hr)&&old){hr=old->QueryInterface(IID_IDirectDraw7,(void**)&base);old->Release();}}else hr=DirectDrawCreateEx(nullptr,(void**)&base,IID_IDirectDraw7,nullptr);Log("CREATE_DDRAW7",hr);int result=5;
 if(SUCCEEDED(hr)&&base){if(api==7)result=Run<IDirectDraw7,IDirectDrawSurface7,DDSURFACEDESC2,DDSCAPS2>(base,com7||com1);
  else if(api==4){IDirectDraw4* p=nullptr;hr=base->QueryInterface(IID_IDirectDraw4,(void**)&p);if(SUCCEEDED(hr)){result=Run<IDirectDraw4,IDirectDrawSurface4,DDSURFACEDESC2,DDSCAPS2>(p,com7||com1);p->Release();}}
  else if(api==2){IDirectDraw2* p=nullptr;hr=base->QueryInterface(IID_IDirectDraw2,(void**)&p);if(SUCCEEDED(hr)){result=Run<IDirectDraw2,IDirectDrawSurface,DDSURFACEDESC,DDSCAPS>(p,com7||com1);p->Release();}}
  else{IDirectDraw* p=nullptr;hr=base->QueryInterface(IID_IDirectDraw,(void**)&p);if(SUCCEEDED(hr)){result=Run<IDirectDraw,IDirectDrawSurface,DDSURFACEDESC,DDSCAPS>(p,com7||com1);p->Release();}}base->Release();}
 LONG counts[3]{};stats(counts);fprintf(output,"NATIVE_BOUNDARY exclusive=%ld mode=%ld normal=%ld\nEXIT %d\n",counts[0],counts[1],counts[2],result);fflush(output);while(!quit){Pump();Sleep(1);}DestroyWindow(window);fclose(output);return result;
}
