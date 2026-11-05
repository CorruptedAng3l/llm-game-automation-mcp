// Synthetic startup consumer. Render calls are deliberately gated on observed
// activation/API signals; it is not UE4 or a retail-game compatibility proof.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <mutex>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using U64=unsigned long long;FILE* output=nullptr;std::mutex logMx;HWND window=nullptr;bool stop=false,appActive=false,windowActive=false,focusMessage=false;
U64 presents=0,appMessages=0,windowMessages=0,focusMessages=0,controls=0;const wchar_t* mode=L"app";
void Log(const char* f,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list a;va_start(a,f);vfprintf(output,f,a);va_end(a);fputc('\n',output);fflush(output);}
const char* Bool(bool b){return b?"true":"false";}
void WINAPI Key(BYTE,BYTE,DWORD,ULONG_PTR){}SHORT WINAPI NoKey(int){return 0;}
HWND WINAPI Outside(){return GetDesktopWindow();}HWND WINAPI NullFocus(){return nullptr;}
BOOL WINAPI Gui(DWORD,PGUITHREADINFO p){if(!p||p->cbSize!=sizeof(*p)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO value{sizeof(value)};value.hwndActive=GetDesktopWindow();*p=value;return TRUE;}
BOOL WINAPI Position(LPPOINT p){if(!p){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}*p=POINT{5000,6000};return TRUE;}
bool ApiGate(){return GetForegroundWindow()==window&&GetActiveWindow()==window&&GetFocus()==window;}
bool Gate(){if(!wcscmp(mode,L"api"))return ApiGate();if(!wcscmp(mode,L"activate"))return appActive&&windowActive;if(!wcscmp(mode,L"setfocus"))return focusMessage;return appActive;}
void Snapshot(U64 token){Log("{\"event\":\"snapshot\",\"token\":%llu,\"tick\":%llu,\"present_calls\":%llu,\"app_active\":%s,\"window_active\":%s,\"focus_message\":%s,\"api_gate\":%s,\"gate\":%s,\"app_messages\":%llu,\"window_messages\":%llu,\"focus_messages\":%llu,\"visible\":%s,\"iconic\":%s}",token,GetTickCount64(),presents,Bool(appActive),Bool(windowActive),Bool(focusMessage),Bool(ApiGate()),Bool(Gate()),appMessages,windowMessages,focusMessages,Bool(IsWindowVisible(window)!=FALSE),Bool(IsIconic(window)!=FALSE));}
LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_ACTIVATEAPP){appActive=w!=0;++appMessages;Log("{\"event\":\"activateapp\",\"tick\":%llu,\"active\":%s}",GetTickCount64(),Bool(appActive));}
 if(m==WM_ACTIVATE){windowActive=LOWORD(w)!=WA_INACTIVE;++windowMessages;Log("{\"event\":\"activate\",\"tick\":%llu,\"active\":%s}",GetTickCount64(),Bool(windowActive));}
 if(m==WM_SETFOCUS){focusMessage=true;++focusMessages;Log("{\"event\":\"setfocus\",\"tick\":%llu}",GetTickCount64());}
 if(m==WM_KILLFOCUS)focusMessage=false;
 if(m==WM_APP+0xB3){++controls;bool ok=true;if(w==8)stop=true;else if(w==1){ok=PostMessageW(h,WM_ACTIVATEAPP,FALSE,0)!=FALSE;ok=(PostMessageW(h,WM_ACTIVATE,WA_INACTIVE,0)!=FALSE)&&ok;ok=(PostMessageW(h,WM_KILLFOCUS,0,0)!=FALSE)&&ok;}else if(w!=0)ok=false;Snapshot((U64)(ULONG_PTR)l);Log("{\"event\":\"control\",\"token\":%llu,\"ok\":%s}",(U64)(ULONG_PTR)l,Bool(ok));return 0;}
 if(m==WM_CLOSE||m==WM_DESTROY){stop=true;return 0;}return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<3||argc>4){if(argv)LocalFree(argv);return 2;}
 output=_wfsopen(argv[1],L"w",_SH_DENYNO);std::wstring selected=argv[2],eventName=argc==4?argv[3]:L"";LocalFree(argv);if(!output)return 3;mode=selected.c_str();if(wcscmp(mode,L"app")&&wcscmp(mode,L"activate")&&wcscmp(mode,L"api")&&wcscmp(mode,L"setfocus"))return 3;
 const bool safe=capture_spy::Install()&&capture_spy::Add("keybd_event",(void*)&Key)&&capture_spy::Add("GetAsyncKeyState",(void*)&NoKey)&&capture_spy::Add("GetForegroundWindow",(void*)&Outside)&&capture_spy::Add("GetGUIThreadInfo",(void*)&Gui)&&capture_spy::Add("GetActiveWindow",(void*)&NullFocus)&&capture_spy::Add("GetFocus",(void*)&NullFocus)&&capture_spy::Add("GetCursorPos",(void*)&Position)&&capture_spy::Add("GetPhysicalCursorPos",(void*)&Position);
 Log("{\"event\":\"guards\",\"safe\":%s,\"terminal_desktop_writers\":19,\"native_writer_called\":false,\"focus_source\":\"fabricated outside/null before vinput\"}",Bool(safe));if(!safe)return 4;
 WNDCLASSW cls{};cls.lpfnWndProc=Window;cls.hInstance=instance;cls.lpszClassName=L"ViStartupActivationOwned";if(!RegisterClassW(&cls))return 5;
 const int sx=GetSystemMetrics(SM_XVIRTUALSCREEN),sy=GetSystemMetrics(SM_YVIRTUALSCREEN),sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 if(sw<=0||sh<=0||!(-26000LL+640<=sx||-26000LL>=(LONGLONG)sx+sw||500LL+480<=sy||500LL>=(LONGLONG)sy+sh))return 5;
 // Created before DLL injection, preserving WS_VISIBLE without a native ShowWindow
 // bypass. This fixture does NOT validate the product's deferred early creation.
 window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"owned activation-gated startup",WS_POPUP|WS_VISIBLE,-26000,500,640,480,nullptr,nullptr,instance,nullptr);if(!window)return 6;
 DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=640;desc.BufferDesc.Height=480;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;ID3D11RenderTargetView* target=nullptr;ID3D11Texture2D* back=nullptr;
 HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
 if(FAILED(result)||!swap||!device||!context)return 7;result=swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back);if(FAILED(result)||!back)return 7;result=device->CreateRenderTargetView(back,nullptr,&target);back->Release();if(FAILED(result)||!target)return 7;
 BOOL fullscreen=TRUE;const HRESULT fullscreenResult=swap->GetFullscreenState(&fullscreen,nullptr);if(FAILED(fullscreenResult)||fullscreen)return 7;
 HANDLE resume=eventName.empty()?nullptr:OpenEventW(SYNCHRONIZE,FALSE,eventName.c_str());if(!eventName.empty()&&!resume)return 8;
 // Graphics initialization can itself deliver activation messages. Preserve that
 // history, settle the setup queue finitely, then arm only our local consumer.
 // This epoch does not measure early creation or absence of natural activation.
 MSG setup{};unsigned setupMessages=0;const U64 setupEnd=GetTickCount64()+250;
 while(setupMessages<256&&GetTickCount64()<setupEnd&&PeekMessageW(&setup,nullptr,0,0,PM_REMOVE)){++setupMessages;TranslateMessage(&setup);DispatchMessageW(&setup);}
 if(stop||PeekMessageW(&setup,nullptr,0,0,PM_NOREMOVE)){Log("{\"event\":\"setup_queue_unsettled\",\"messages\":%u}",setupMessages);if(resume)CloseHandle(resume);return 8;}
 Log("{\"event\":\"consumer_epoch\",\"setup_messages\":%u,\"setup_app_active\":%s,\"setup_window_active\":%s,\"setup_focus_message\":%s,\"app_messages\":%llu,\"window_messages\":%llu,\"focus_messages\":%llu,\"local_gate_reset_only\":true}",setupMessages,Bool(appActive),Bool(windowActive),Bool(focusMessage),appMessages,windowMessages,focusMessages);
 appActive=false;windowActive=false;focusMessage=false;
 Log("{\"event\":\"before_dll\",\"present_calls\":%llu,\"app_active\":%s,\"api_gate\":%s}",presents,Bool(appActive),Bool(ApiGate()));
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"actual_offscreen_checked\":true,\"window_visible\":%s,\"windowed_swapchain\":true,\"ordinary_pump_paused\":%s,\"mode\":\"%s\"}",GetCurrentProcessId(),GetCurrentThreadId(),(U64)(ULONG_PTR)window,Bool(IsWindowVisible(window)!=FALSE),Bool(resume!=nullptr),!wcscmp(mode,L"api")?"api":!wcscmp(mode,L"activate")?"activate":!wcscmp(mode,L"setfocus")?"setfocus":"app");
 const U64 end=GetTickCount64()+60000;if(resume){DWORD waited=WAIT_TIMEOUT;while(waited==WAIT_TIMEOUT&&GetTickCount64()<end)waited=WaitForSingleObject(resume,10);CloseHandle(resume);stop=waited!=WAIT_OBJECT_0;Log("{\"event\":\"pump_resumed\",\"tick\":%llu,\"signaled\":%s,\"wait_result\":%lu}",GetTickCount64(),Bool(!stop),waited);}
 U64 lastLog=0;while(!stop&&GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(Gate()){
   const float color[4]={0.12f,0.31f,0.59f,1.0f};context->ClearRenderTargetView(target,color);const HRESULT hr=swap->Present(0,0);++presents;if(presents==1)Log("{\"event\":\"first_present\",\"tick\":%llu,\"hresult\":%ld}",GetTickCount64(),(long)hr);
  }if(GetTickCount64()-lastLog>=100){Snapshot(0);lastLog=GetTickCount64();}Sleep(2);}
 target->Release();context->Release();device->Release();swap->Release();DestroyWindow(window);Log("{\"event\":\"exit\",\"present_calls\":%llu,\"native_writer_called\":false}",presents);{std::lock_guard<std::mutex> lock(logMx);FILE* old=output;output=nullptr;if(old)fclose(old);}return 0;
}
