// Owned offscreen geometry/capture fixture. All cursor/input/foreground writes
// are absorbed below the product; only our own NOACTIVATE windows are resized.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <mutex>
#include "vi_cursor_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output=nullptr;static HWND primary=nullptr,secondary=nullptr;static WNDPROC outerOriginal=nullptr;static HWND outerWindow=nullptr;
static std::atomic<bool> resizeHover{false},resizePrint{false};static std::atomic<unsigned long long> down[2]{},up[2]{},moves[2]{},sizes[2]{},printCallbacks{0};static std::atomic<LONG> lastX[2]{},lastY[2]{};static std::mutex logMx;
static const wchar_t kLifetime[]=L"vinput.capture_scope.lifetime.v1";
static int Index(HWND h){return h==secondary?1:0;}
static void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
static bool Resize(HWND h,int width,int height){DWORD pid=0;if(!h||!GetWindowThreadProcessId(h,&pid)||pid!=GetCurrentProcessId())return false;RECT r{0,0,width,height};AdjustWindowRectEx(&r,(DWORD)GetWindowLongPtrW(h,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(h,GWL_EXSTYLE));return SetWindowPos(h,nullptr,-28000,600,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;}
static void State(unsigned long long token,const char* event,bool ok=true){RECT a{},b{};GetClientRect(primary,&a);GetClientRect(secondary,&b);Log("{\"event\":\"%s\",\"token\":%llu,\"ok\":%s,\"primary\":%llu,\"secondary\":%llu,\"aw\":%ld,\"ah\":%ld,\"bw\":%ld,\"bh\":%ld,\"adown\":%llu,\"aup\":%llu,\"amoves\":%llu,\"asizes\":%llu,\"ax\":%ld,\"ay\":%ld,\"bdown\":%llu,\"bup\":%llu,\"bmoves\":%llu,\"bsizes\":%llu,\"bx\":%ld,\"by\":%ld,\"native_writes_absorbed\":%llu,\"foreground_absorbed\":%llu}",event,token,ok?"true":"false",(unsigned long long)(ULONG_PTR)primary,(unsigned long long)(ULONG_PTR)secondary,a.right,a.bottom,b.right,b.bottom,down[0].load(),up[0].load(),moves[0].load(),sizes[0].load(),lastX[0].load(),lastY[0].load(),down[1].load(),up[1].load(),moves[1].load(),sizes[1].load(),lastX[1].load(),lastY[1].load(),vi_cursor_spy::Total(),vi_cursor_spy::foregroundEffects.load());}
static LRESULT CALLBACK Outer(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEMOVE&&resizeHover.exchange(false)){const bool ok=Resize(h,800,600);State(0,"hover_resize",ok);}return outerOriginal?CallWindowProcW(outerOriginal,h,m,w,l):DefWindowProcW(h,m,w,l);}
static void Paint(HWND h,HDC dc){RECT r{};GetClientRect(h,&r);HBRUSH a=CreateSolidBrush(RGB(30,80,150)),b=CreateSolidBrush(RGB(220,160,40));for(int y=0;y<r.bottom;y+=24)for(int x=0;x<r.right;x+=24){RECT tile{x,y,x+24,y+24};FillRect(dc,&tile,((x/24+y/24)&1)?a:b);}DeleteObject(a);DeleteObject(b);}
// Test-only lower backend. It never calls a native PrintWindow trampoline.
// Only owned fixture HWNDs and caller-owned memory DCs are accepted.
static BOOL WINAPI SurrogatePrint(HWND h,HDC dc,UINT){DWORD owner=0;if((h!=primary&&h!=secondary)||!GetWindowThreadProcessId(h,&owner)||owner!=GetCurrentProcessId()||GetObjectType(dc)!=OBJ_MEMDC){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}++printCallbacks;if(resizePrint.exchange(false)){const bool ok=Resize(h,900,700);State(0,"print_resize",ok);}Paint(h,dc);Log("{\"event\":\"print_callback\",\"count\":%llu,\"hwnd\":%llu,\"memory_dc_only\":true,\"native_printwindow_called\":false}",printCallbacks.load(),(unsigned long long)(ULONG_PTR)h);return TRUE;}
static bool InstallPrintSurrogate(){void* entry=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"PrintWindow");void* ignored=nullptr;return entry&&VIMH_CreateHook(entry,(void*)&SurrogatePrint,&ignored)==MH_OK&&VIMH_EnableHook(entry)==MH_OK&&vi_cursor_spy::Reaches(entry,(void*)&SurrogatePrint);}
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_APP+0x50){State((unsigned long long)w,"snapshot");return 0;}
 if(m==WM_APP+0x51){State((unsigned long long)w,"resize",Resize(primary,800,600));return 0;}
 if(m==WM_APP+0x52){const bool ok=Resize(primary,900,700)&&Resize(primary,800,600);State((unsigned long long)w,"resize_aba",ok);return 0;}
 if(m==WM_APP+0x53){ShowWindow(secondary,SW_SHOWNOACTIVATE);State((unsigned long long)w,"show_secondary");return 0;}
 if(m==WM_APP+0x54){resizeHover=true;if(!outerOriginal){outerWindow=primary;outerOriginal=(WNDPROC)SetWindowLongPtrW(primary,GWLP_WNDPROC,(LONG_PTR)Outer);}State((unsigned long long)w,"arm_hover",outerOriginal!=nullptr);return 0;}
 if(m==WM_APP+0x55){if(outerOriginal&&outerWindow&&GetWindowLongPtrW(outerWindow,GWLP_WNDPROC)==(LONG_PTR)Outer)SetWindowLongPtrW(outerWindow,GWLP_WNDPROC,(LONG_PTR)outerOriginal);outerOriginal=nullptr;outerWindow=nullptr;State((unsigned long long)w,"restore_outer");return 0;}
 if(m==WM_APP+0x56){resizePrint=true;State((unsigned long long)w,"arm_print");return 0;}
 if(m==WM_APP+0x58){State((unsigned long long)w,"reset_size",Resize(primary,640,480));return 0;}
 if(m==WM_APP+0x57){const auto old=(ULONG_PTR)GetPropW(primary,kLifetime);auto next=old+1001;if(!next)next=1;const bool ok=SetPropW(primary,kLifetime,(HANDLE)next)!=FALSE;Log("{\"event\":\"rotate_lifetime\",\"token\":%llu,\"ok\":%s,\"old_cookie\":%llu,\"new_cookie\":%llu,\"numeric_hwnd_reuse\":false}",(unsigned long long)w,ok?"true":"false",(unsigned long long)old,(unsigned long long)next);return 0;}
 if(m==WM_SIZE){++sizes[Index(h)];return 0;}
 if(m==WM_MOUSEMOVE){const int i=Index(h);++moves[i];lastX[i]=(LONG)(SHORT)LOWORD(l);lastY[i]=(LONG)(SHORT)HIWORD(l);return 0;}
 if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN){const int i=Index(h);++down[i];lastX[i]=(LONG)(SHORT)LOWORD(l);lastY[i]=(LONG)(SHORT)HIWORD(l);return 0;}
 if(m==WM_LBUTTONUP||m==WM_RBUTTONUP){++up[Index(h)];return 0;}
 if(m==WM_PRINTCLIENT||m==WM_PRINT){Paint(h,(HDC)w);return 0;}
 if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);Paint(h,dc);EndPaint(h,&p);return 0;}
 if(m==WM_DESTROY){if(h==primary)PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count!=2)return 2;output=_wfsopen(args[1],L"w",_SH_DENYNO);LocalFree(args);if(!output)return 3;
 const bool safe=vi_cursor_spy::Install()&&vi_cursor_spy::VerifyBeforeProduct()&&InstallPrintSurrogate();if(!safe){Log("{\"event\":\"fatal\",\"safe_spies\":false}");return 4;}
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"ViCaptureScope";RegisterClassW(&wc);
 primary=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Capture scope primary",WS_OVERLAPPEDWINDOW,-28000,600,656,519,nullptr,nullptr,instance,nullptr);secondary=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Capture scope secondary",WS_OVERLAPPEDWINDOW,-28000,600,656,519,nullptr,nullptr,instance,nullptr);if(!primary||!secondary)return 5;
 Resize(primary,640,480);Resize(secondary,640,480);ShowWindow(primary,SW_SHOWNOACTIVATE);
 Log("{\"event\":\"ready\",\"pid\":%lu,\"primary\":%llu,\"secondary\":%llu,\"safe_spies\":true,\"native_write_trampolines\":false,\"owned_print_surrogate\":true,\"native_printwindow_called\":false}",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)primary,(unsigned long long)(ULONG_PTR)secondary);
 const auto until=GetTickCount64()+120000;while(GetTickCount64()<until){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)return 0;TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}return 0;
}
