#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <share.h>
#include <atomic>
#include "vi_r2_spies.h"
#include "vi_d3d9_fixture_geometry.h"
#pragma comment(lib,"d3d9.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output;static HWND window;static bool go=false,quit=false;
static DWORD windowThread;static capture_spy::WindowPosition ownPosition=nullptr;static std::atomic<unsigned long long> geometryAllowed{0},geometryRefused{0},geometryAfterFailed{0};
static const wchar_t* lifetimeName=L"ViD3D9WindowedFixtureLifetime.v1";static const int lifetimeCookie=7;
static void WINAPI Key(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
static bool DesktopRect(RECT& r){const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(w<=0||h<=0||(long long)x+w>LONG_MAX||(long long)y+h>LONG_MAX)return false;r={x,y,x+w,y+h};return true;}
static bool OwnWindow(HWND h){DWORD pid=0;return h&&h==window&&IsWindow(h)&&GetWindowThreadProcessId(h,&pid)==windowThread&&pid==GetCurrentProcessId()&&GetPropW(h,lifetimeName)==(HANDLE)&lifetimeCookie;}
static bool OffscreenClient(HWND h,const RECT& desktop){RECT c{};POINT origin{};if(!GetClientRect(h,&c)||c.right<1||c.bottom<1||c.right>8192||c.bottom>8192||!ClientToScreen(h,&origin)||(long long)origin.x+c.right>LONG_MAX||(long long)origin.y+c.bottom>LONG_MAX)return false;RECT r{origin.x,origin.y,origin.x+c.right,origin.y+c.bottom};return vi_d9_fixture_geometry::Outside(r,desktop);}
static BOOL WINAPI OwnWindowPosition(HWND h,HWND insertAfter,int x,int y,int width,int height,UINT flags){
 RECT before{},desktop{},predicted{};auto refuse=[](){++geometryRefused;++capture_spy::writes;SetLastError(ERROR_ACCESS_DENIED);return FALSE;};
 if(!ownPosition||(insertAfter&&insertAfter!=HWND_NOTOPMOST)||GetCurrentThreadId()!=windowThread||!OwnWindow(h)||!DesktopRect(desktop)||!GetWindowRect(h,&before)||!OffscreenClient(h,desktop)||!vi_d9_fixture_geometry::Predict(before,desktop,x,y,width,height,flags,predicted))return refuse();
 const BOOL ok=ownPosition(h,nullptr,x,y,width,height,flags|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);RECT after{};
 if(!ok||!OwnWindow(h)||!GetWindowRect(h,&after)||!vi_d9_fixture_geometry::Outside(after,desktop)||!OffscreenClient(h,desktop)||after.left!=predicted.left||after.top!=predicted.top||after.right!=predicted.right||after.bottom!=predicted.bottom){++geometryAfterFailed;return FALSE;}
 ++geometryAllowed;return TRUE;
}
static void Log(const char* name,HRESULT hr=S_OK){fprintf(output,"%s hr=0x%08lX\n",name,(unsigned long)hr);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+1){const bool valid=OwnWindow(h)&&(w==1||w==8);if(valid){if(w==1)go=true;else quit=true;}fprintf(output,"{\"event\":\"control\",\"token\":%llu,\"action\":%llu,\"ok\":%s}\n",(unsigned long long)(ULONG_PTR)l,(unsigned long long)w,valid?"true":"false");fflush(output);return 0;}if(m==WM_CLOSE||m==WM_DESTROY){quit=true;return 0;}return DefWindowProcW(h,m,w,l);}
static void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){quit=true;return;}TranslateMessage(&m);DispatchMessageW(&m);}}
static D3DPRESENT_PARAMETERS Parameters(UINT w,UINT h,bool normal){D3DPRESENT_PARAMETERS p{};p.BackBufferWidth=w;p.BackBufferHeight=h;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.Windowed=normal;p.FullScreen_RefreshRateInHz=normal?0:60;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;return p;}
static D3DDISPLAYMODEEX Mode(UINT w,UINT h){D3DDISPLAYMODEEX m{};m.Size=sizeof(m);m.Width=w;m.Height=h;m.RefreshRate=60;m.Format=D3DFMT_X8R8G8B8;m.ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;return m;}
static bool Verify(IDirect3DDevice9* device,IDirect3DSwapChain9* chain,UINT width,UINT height,const char* phase){D3DPRESENT_PARAMETERS p{};HRESULT hr=chain->GetPresentParameters(&p);RECT client{};GetClientRect(window,&client);fprintf(output,"PP phase=%s hr=0x%08lX windowed=%d width=%u height=%u refresh=%u client=%ldx%ld\n",phase,(unsigned long)hr,(int)p.Windowed,p.BackBufferWidth,p.BackBufferHeight,p.FullScreen_RefreshRateInHz,client.right,client.bottom);fflush(output);
 bool ok=SUCCEEDED(hr)&&p.Windowed&&p.BackBufferWidth==width&&p.BackBufferHeight==height&&p.FullScreen_RefreshRateInHz==0&&(UINT)client.right==width&&(UINT)client.bottom==height;
 IDirect3DSurface9 *back=nullptr,*host=nullptr;hr=chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back);if(SUCCEEDED(hr))hr=device->ColorFill(back,nullptr,0xff1276b4);if(SUCCEEDED(hr))hr=device->CreateOffscreenPlainSurface(width,height,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&host,nullptr);if(SUCCEEDED(hr))hr=device->GetRenderTargetData(back,host);D3DLOCKED_RECT bits{};bool locked=false;if(SUCCEEDED(hr)){hr=host->LockRect(&bits,nullptr,D3DLOCK_READONLY);locked=SUCCEEDED(hr);}bool pixel=locked&&((*(DWORD*)bits.pBits&0xffffff)==0x1276b4);if(locked)host->UnlockRect();if(host)host->Release();if(back)back->Release();Log(pixel?"NATIVE_PIXEL_OK":"FAIL_NATIVE_PIXEL",hr);HRESULT presented=chain->Present(nullptr,nullptr,nullptr,nullptr,0);Log(SUCCEEDED(presented)?"NATIVE_PRESENT_OK":"FAIL_NATIVE_PRESENT",presented);return ok&&pixel&&SUCCEEDED(presented);
}
static int Run(bool ex,bool normal){IDirect3D9* base=nullptr;IDirect3D9Ex* extended=nullptr;HRESULT hr=S_OK;if(ex){hr=Direct3DCreate9Ex(D3D_SDK_VERSION,&extended);base=extended;}else base=Direct3DCreate9(D3D_SDK_VERSION);Log("FACTORY",base?hr:E_FAIL);if(!base)return 5;
 auto pp=Parameters(800,600,normal);auto mode=Mode(800,600);IDirect3DDevice9* device=nullptr;IDirect3DDevice9Ex* devEx=nullptr;
 if(ex){hr=extended->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,normal?nullptr:&mode,&devEx);device=devEx;}else hr=base->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);Log("CREATE_DEVICE",hr);int result=10;
 if(SUCCEEDED(hr)&&device){IDirect3DSwapChain9* chain=nullptr;hr=device->GetSwapChain(0,&chain);bool first=SUCCEEDED(hr)&&chain&&Verify(device,chain,800,600,"create");if(chain)chain->Release();if(!first)result=11;else if(normal)result=0;else{
  pp=Parameters(640,480,false);mode=Mode(640,480);hr=ex?devEx->ResetEx(&pp,&mode):device->Reset(&pp);Log("RESET_DEVICE",hr);chain=nullptr;if(SUCCEEDED(hr))hr=device->GetSwapChain(0,&chain);bool reset=SUCCEEDED(hr)&&chain&&Verify(device,chain,640,480,"reset");if(chain)chain->Release();if(!reset)result=12;else{
   pp=Parameters(320,240,false);chain=nullptr;hr=device->CreateAdditionalSwapChain(&pp,&chain);Log("ADDITIONAL_CHAIN",hr);bool extra=SUCCEEDED(hr)&&chain&&Verify(device,chain,320,240,"additional");if(chain)chain->Release();result=extra?0:13;
  }}device->Release();}
 base->Release();Log("COM_RELEASED");return result;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int argc=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<3)return 2;output=_wfsopen(args[1],L"w",_SH_DENYNO);if(!output)return 3;bool ex=wcscmp(args[2],L"ex")==0,normal=argc>3&&wcscmp(args[3],L"windowed")==0;std::wstring boundary=std::wstring(args[1])+L".boundary.log";LocalFree(args);
 HMODULE guard=GetModuleHandleW(L"d3d9.dll");using SetLog=void(WINAPI*)(const wchar_t*);using Stats=void(WINAPI*)(LONG*);auto setLog=(SetLog)GetProcAddress(guard,"ViGuardLog");auto stats=(Stats)GetProcAddress(guard,"ViGuardStats");if(!setLog||!stats){Log("NOTRUN_NATIVE_GUARD_MISSING");return 4;}setLog(boundary.c_str());
 const bool safe=capture_spy::Install(&OwnWindowPosition)&&capture_spy::Add("keybd_event",(void*)&Key);for(const auto& hook:capture_spy::hooks)if(hook.detour==(void*)&OwnWindowPosition)ownPosition=(capture_spy::WindowPosition)hook.original;
 fprintf(output,"{\"event\":\"guards\",\"safe\":%s,\"terminal_desktop_writers\":19,\"own_geometry_only_exception\":true}\n",safe&&ownPosition?"true":"false");fflush(output);if(!safe||!ownPosition)return 4;
 RECT desktop{},initial{-28000,600,-27200,1200};if(!DesktopRect(desktop)||!vi_d9_fixture_geometry::Outside(initial,desktop))return 5;
 WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Proc;c.lpszClassName=L"vi_d3d9_windowed";if(!RegisterClassW(&c))return 5;windowThread=GetCurrentThreadId();window=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"D3D9 guarded",WS_POPUP|WS_VISIBLE,-28000,600,800,600,nullptr,nullptr,instance,nullptr);if(!window||!SetPropW(window,lifetimeName,(HANDLE)&lifetimeCookie)||!OwnWindow(window))return 6;
 RECT actual{};if(!GetWindowRect(window,&actual)||!vi_d9_fixture_geometry::Outside(actual,desktop)||!OffscreenClient(window,desktop)||!IsWindowVisible(window))return 6;
 fprintf(output,"{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"lifetime_cookie\":%llu,\"actual_offscreen_checked\":true,\"window_visible\":true}\n",GetCurrentProcessId(),windowThread,(unsigned long long)(ULONG_PTR)window,(unsigned long long)(ULONG_PTR)&lifetimeCookie);fflush(output);Log("READY_GUARDED");const ULONGLONG end=GetTickCount64()+60000;while(!go&&!quit&&GetTickCount64()<end){Pump();Sleep(1);}int result=go&&!quit?Run(ex,normal):20;LONG counts[3]{};stats(counts);fprintf(output,"NATIVE_BOUNDARY blocked=%ld normal=%ld bad_mode=%ld\nEXIT %d\n",counts[0],counts[1],counts[2],result);fflush(output);while(!quit&&GetTickCount64()<end){Pump();Sleep(1);}DestroyWindow(window);
 fprintf(output,"{\"event\":\"guard_cleanup\",\"allowed_own_geometry\":%llu,\"refused_geometry\":%llu,\"absorbed_desktop_writes\":%llu,\"geometry_after_failed\":%llu,\"guards_kept_through_cleanup\":true}\n",geometryAllowed.load(),geometryRefused.load(),capture_spy::writes.load(),geometryAfterFailed.load());fclose(output);return result;
}
