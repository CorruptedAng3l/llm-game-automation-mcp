#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output;
static bool armed=false;
static unsigned scroll=0,reads=0;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_GETMINMAXINFO){auto* size=(MINMAXINFO*)l;size->ptMinTrackSize={1,1};return 0;}
 if(m==WM_APP+1){armed=false;scroll=reads=0;POINT p{(LONG)w,(LONG)l};ClientToScreen(h,&p);SetCursorPos(p.x,p.y);fprintf(output,"RESET %lu %ld\n",(unsigned long)w,(long)l);fflush(output);return 0;}
 if(m==WM_MOUSEMOVE){armed=true;return 0;}
 if(m==WM_APP+2){SetWindowLongPtrW(h,GWL_STYLE,WS_POPUP);SetWindowPos(h,nullptr,0,0,(int)w,(int)l,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);fprintf(output,"RESIZED\n");fflush(output);return 0;}
 if(m==WM_TIMER&&armed){POINT p{};GetCursorPos(&p);ScreenToClient(h,&p);RECT r{};GetClientRect(h,&r);++reads;if(p.x<8||p.y<8||p.x>=r.right-8||p.y>=r.bottom-8)++scroll;fprintf(output,"READ %ld %ld scroll=%u reads=%u\n",p.x,p.y,scroll,reads);fflush(output);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_edgeguard";RegisterClassW(&c);
 HWND h=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"edge-scroll reader",WS_OVERLAPPEDWINDOW,-28000,600,320,240,nullptr,nullptr,instance,nullptr);ShowWindow(h,SW_SHOWNOACTIVATE);SetTimer(h,1,1,nullptr);
 fprintf(output,"READY\n");fflush(output);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}fclose(output);return 0;
}
