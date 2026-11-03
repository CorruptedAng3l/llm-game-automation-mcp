// Based on the reviewed public c63ab25 fixture: stale game picture, separate
// aspect-fitted DDrawCompatPresentationWindow. Optional in-process hiding forces
// the external CLI hint path without hiding anything from other processes.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <share.h>
#include "vi_mh_rename.h"
#include "MinHook.h"
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
static HWND presentation;static bool hidden=false,uniform=false;static FILE* output;
using Top=HWND(WINAPI*)(HWND);using Window=HWND(WINAPI*)(HWND,UINT);static Top nativeTop;static Window nativeWindow;
static HWND WINAPI HiddenTop(HWND parent){HWND h=nativeTop(parent);if(hidden&&h==presentation)h=nativeWindow(h,GW_HWNDNEXT);return h;}
static HWND WINAPI HiddenWindow(HWND h,UINT command){HWND next=nativeWindow(h,command);if(hidden&&command==GW_HWNDNEXT&&next==presentation)next=nativeWindow(next,GW_HWNDNEXT);return next;}
static void DrawGame(HDC dc,HWND h){RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));RECT square{140,100,180,140};HBRUSH yellow=CreateSolidBrush(RGB(230,230,0));FillRect(dc,&square,yellow);DeleteObject(yellow);}
static void DrawPicture(HDC dc,HWND h){RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(uniform?WHITE_BRUSH:BLACK_BRUSH));if(uniform)return;
 HBRUSH red=CreateSolidBrush(RGB(220,20,20)),blue=CreateSolidBrush(RGB(20,20,220)),green=CreateSolidBrush(RGB(20,220,20));RECT left{80,0,400,480},right{400,0,720,480},marker{80,0,160,80};FillRect(dc,&left,red);FillRect(dc,&right,blue);FillRect(dc,&marker,green);DeleteObject(red);DeleteObject(blue);DeleteObject(green);
}
static LRESULT CALLBACK Game(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_PRINTCLIENT||m==WM_PRINT){fprintf(output,"GAME_PRINT message=%u\n",m);fflush(output);DrawGame((HDC)w,h);GdiFlush();return 0;}if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);DrawGame(dc,h);EndPaint(h,&ps);return 0;}if(m==WM_APP+1){uniform=w!=0;InvalidateRect(presentation,nullptr,TRUE);UpdateWindow(presentation);GdiFlush();fprintf(output,"UNIFORM %d\n",(int)uniform);fflush(output);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK Present(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_ERASEBKGND)return 1;if(m==WM_PRINTCLIENT){DrawPicture((HDC)w,h);GdiFlush();return 0;}if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);DrawPicture(dc,h);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);hidden=argc>2&&wcscmp(argv[2],L"hide")==0;LocalFree(argv);if(!output)return 3;
 WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Game;c.lpszClassName=L"OverlayGame";RegisterClassW(&c);c.lpfnWndProc=Present;c.lpszClassName=L"DDrawCompatPresentationWindow";RegisterClassW(&c);RECT r{0,0,320,240};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
 HWND game=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"OverlayGame",L"vi overlay game",WS_OVERLAPPEDWINDOW,-28000,600,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
 presentation=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"DDrawCompatPresentationWindow",L"",WS_POPUP,-28000,1000,800,480,nullptr,nullptr,instance,nullptr);ShowWindow(game,SW_SHOWNOACTIVATE);ShowWindow(presentation,SW_SHOWNOACTIVATE);
 if(hidden){HMODULE user=GetModuleHandleW(L"user32.dll");if(VIMH_Initialize()!=MH_OK||VIMH_CreateHook((void*)GetProcAddress(user,"GetTopWindow"),(void*)&HiddenTop,(void**)&nativeTop)!=MH_OK||VIMH_CreateHook((void*)GetProcAddress(user,"GetWindow"),(void*)&HiddenWindow,(void**)&nativeWindow)!=MH_OK||VIMH_EnableHook(MH_ALL_HOOKS)!=MH_OK){fprintf(output,"FAIL HIDE_HOOKS\n");fflush(output);return 4;}}
 fprintf(output,"READY game=0x%llX overlay=0x%llX hide=%d\n",(unsigned long long)(ULONG_PTR)game,(unsigned long long)(ULONG_PTR)presentation,(int)hidden);fflush(output);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}fclose(output);return 0;
}
