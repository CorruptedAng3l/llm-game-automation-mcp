// GDI source-selection witness only. This is not a DDraw/D3D9 wrapper emulator.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <algorithm>
#include <share.h>
#include "vi_capture_diagnostic_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
FILE* output=nullptr;HWND mainWindow=nullptr,proxyWindow=nullptr,secondWindow=nullptr;HINSTANCE module=nullptr;
bool nonblack=false;unsigned customText=0;
void Log(const char* format,...){if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
void Paint(HWND window,HDC dc){
 RECT client{};GetClientRect(window,&client);const bool proxy=window!=mainWindow;
 HBRUSH black=CreateSolidBrush(RGB(0,0,0));FillRect(dc,&client,black);DeleteObject(black);
 if(proxy||nonblack){
  for(LONG y=0;y<client.bottom;y+=40)for(LONG x=0;x<client.right;x+=40){RECT tile{x,y,(std::min)(x+40,client.right),(std::min)(y+40,client.bottom)};
   HBRUSH color=CreateSolidBrush(((x/40+y/40)&1)?RGB(25,60,230):RGB(220,35,20));FillRect(dc,&tile,color);DeleteObject(color);}
  RECT marker{4,4,24,24};HBRUSH green=CreateSolidBrush(RGB(20,220,30));FillRect(dc,&marker,green);DeleteObject(green);
 }
}
HWND Proxy(const wchar_t* title,int width,int height,bool visible){return CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"D3DProxyWindow",title,WS_POPUP|(visible?WS_VISIBLE:0),-25000,900,width,height,nullptr,nullptr,module,nullptr);}
LRESULT WindowCommon(HWND window,UINT message,WPARAM w,LPARAM l,bool ansi){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_PRINT||message==WM_PRINTCLIENT){Paint(window,(HDC)w);return 0;}
 if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);Paint(window,dc);EndPaint(window,&paint);return 0;}
 if(message==WM_GETTEXT){++customText;return ansi?DefWindowProcA(window,message,w,l):DefWindowProcW(window,message,w,l);}
 if(message==WM_APP+0x90){
  if(w==1&&proxyWindow){DestroyWindow(proxyWindow);proxyWindow=nullptr;}
  if(w==2&&!proxyWindow)proxyWindow=Proxy(L"Synthetic proxy replacement",640,480,true);
  Log("{\"kind\":\"snapshot\",\"token\":%llu,\"proxy\":%llu,\"custom_wm_gettext\":%u,\"absorbed_writes\":%llu}",(unsigned long long)(ULONG_PTR)l,(unsigned long long)(ULONG_PTR)proxyWindow,customText,capture_spy::writes.load());return 0;
 }
 if(message==WM_DESTROY){if(window==mainWindow)PostQuitMessage(0);return 0;}
 return ansi?DefWindowProcA(window,message,w,l):DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){return WindowCommon(window,message,w,l,false);}
LRESULT CALLBACK AnsiWindow(HWND window,UINT message,WPARAM w,LPARAM l){return WindowCommon(window,message,w,l,true);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=3){if(args)LocalFree(args);return 2;}
 output=_wfsopen(args[1],L"w",_SH_DENYNO);const std::wstring mode=args[2];LocalFree(args);if(!output)return 2;
 if(!capture_spy::Install()){Log("{\"kind\":\"fatal\",\"stage\":\"terminal_spy_installation\"}");return 3;}
 module=instance;nonblack=mode==L"nonblack";
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"GNW95 Class";
 if(!RegisterClassW(&wc))return 4;wc.lpszClassName=L"D3DProxyWindow";if(!RegisterClassW(&wc))return 4;
 if(mode==L"ansi"){
  WNDCLASSA narrow{};narrow.hInstance=instance;narrow.lpfnWndProc=AnsiWindow;narrow.lpszClassName="GNW95 ANSI Class";if(!RegisterClassA(&narrow))return 4;
  mainWindow=CreateWindowExA(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,narrow.lpszClassName,"Synthetic GNW95 input",WS_POPUP|WS_VISIBLE,-25000,500,320,240,nullptr,nullptr,instance,nullptr);
 }else mainWindow=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"GNW95 Class",L"Synthetic GNW95 input",WS_POPUP|WS_VISIBLE,-25000,500,320,240,nullptr,nullptr,instance,nullptr);
 if(!mainWindow)return 4;
 if(mode!=L"none")proxyWindow=Proxy(mode==L"foreign"?L"Foreign process proxy":L"Synthetic visible proxy",mode==L"foreign"?960:640,mode==L"foreign"?720:480,mode!=L"hidden");
 if(mode!=L"none"&&!proxyWindow)return 4;
 if(mode==L"two"){secondWindow=Proxy(L"Synthetic second proxy",480,360,true);if(!secondWindow)return 4;}
 Log("{\"kind\":\"ready\",\"pid\":%lu,\"hwnd\":%llu,\"proxy\":%llu,\"second_proxy\":%llu,\"terminal_spies\":true,\"api_entries_verified\":true,\"no_activation_or_physical_input\":true}",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)mainWindow,(unsigned long long)(ULONG_PTR)proxyWindow,(unsigned long long)(ULONG_PTR)secondWindow);
 const auto deadline=GetTickCount64()+120000;bool running=true;
 while(running&&GetTickCount64()<deadline){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}
 if(secondWindow)DestroyWindow(secondWindow);if(proxyWindow)DestroyWindow(proxyWindow);if(IsWindow(mainWindow))DestroyWindow(mainWindow);
 Log("{\"kind\":\"exit\",\"terminal_spies_retained\":true,\"absorbed_writes\":%llu}",capture_spy::writes.load());fclose(output);return 0;
}
