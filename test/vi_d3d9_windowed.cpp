#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <share.h>
#pragma comment(lib,"d3d9.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output;static HWND window;static bool go=false,quit=false;
static void Log(const char* name,HRESULT hr=S_OK){fprintf(output,"%s hr=0x%08lX\n",name,(unsigned long)hr);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+1){go=true;return 0;}if(m==WM_DESTROY){quit=true;PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
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
 WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Proc;c.lpszClassName=L"vi_d3d9_windowed";RegisterClassW(&c);window=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"D3D9 guarded",WS_POPUP,-28000,600,800,600,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);Log("READY_GUARDED");while(!go&&!quit){Pump();Sleep(1);}int result=Run(ex,normal);LONG counts[3]{};stats(counts);fprintf(output,"NATIVE_BOUNDARY blocked=%ld normal=%ld bad_mode=%ld\nEXIT %d\n",counts[0],counts[1],counts[2],result);fflush(output);while(!quit){Pump();Sleep(1);}DestroyWindow(window);fclose(output);return result;
}
