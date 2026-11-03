// A slow placement consumer. WM mouse messages never place anything. Actual DI
// button + GetCursorPos reads must see pickup, every point and the final dwell.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
static FILE* output;
static bool polling=true,drift=false,cursorOnly=false;
static ULONGLONG nextPoll=0;
static void Log(const char* format,...){fprintf(output,"%llu ",GetTickCount64());va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_MOUSEMOVE){nextPoll=GetTickCount64()+180;return 0;}
 if(m==WM_APP+1){polling=w!=0;Log("POLLING %d",(int)polling);return 0;}
 if(m==WM_APP+2){drift=true;Log("DRIFT_ARMED");return 0;}
 if(m==WM_APP+3){SetWindowPos(h,nullptr,0,0,420,300,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Log("RESIZED");return 0;}
 if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)(COLOR_WINDOW+1));EndPaint(h,&p);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(!output)return 3;
 for(int i=2;i<argc;i++){if(!wcscmp(argv[i],L"--cursor-only"))cursorOnly=true;}LocalFree(argv);
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_placement";RegisterClassW(&c);
 HWND h=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"polled placement",WS_OVERLAPPEDWINDOW,-28000,600,380,280,nullptr,nullptr,instance,nullptr);if(!h)return 4;ShowWindow(h,SW_SHOWNOACTIVATE);
 IDirectInput8W* di=nullptr;IDirectInputDevice8W* mouse=nullptr;
 if(FAILED(DirectInput8Create(instance,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&di,nullptr))||FAILED(di->CreateDevice(GUID_SysMouse,&mouse,nullptr)))return 5;
 mouse->SetDataFormat(&c_dfDIMouse2);mouse->SetCooperativeLevel(h,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);mouse->Acquire();
 Log("READY pid=%lu hwnd=0x%llX",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)h);
 bool running=true,wasDown=false,picked=false;unsigned distinct=0;POINT last{-1,-1};ULONGLONG finalAt=0;
 while(running){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
  if(polling&&GetTickCount64()>=nextPoll){nextPoll=GetTickCount64()+180;POINT p{};GetCursorPos(&p);ScreenToClient(h,&p);DIMOUSESTATE2 state{};HRESULT hr=cursorOnly?DIERR_NOTACQUIRED:mouse->GetDeviceState(sizeof(state),&state);bool down=SUCCEEDED(hr)&&(state.rgbButtons[0]&0x80)!=0;
   Log("READ x=%ld y=%ld down=%d",p.x,p.y,(int)down);
   if(down&&!wasDown){picked=p.x==40&&p.y==50;distinct=0;last=p;finalAt=0;Log(picked?"PICKUP":"MISSED_PICKUP");}
   if(down&&picked){if(p.x!=last.x||p.y!=last.y){++distinct;last=p;Log("STEP x=%ld y=%ld count=%u",p.x,p.y,distinct);}if(p.x==240&&p.y==170){if(!finalAt){finalAt=GetTickCount64();Log("FINAL_SEEN");}}else finalAt=0;}
   if(!down&&wasDown){const bool placed=picked&&distinct==5&&p.x==240&&p.y==170&&finalAt&&GetTickCount64()-finalAt>=400;Log(placed?"PLACED":"DROPPED");picked=false;}
   wasDown=down;
   if(drift&&down){POINT moved{15,15};ClientToScreen(h,&moved);SetCursorPos(moved.x,moved.y);drift=false;Log("GAME_CURSOR_DRIFT");}
  }Sleep(1);
 }
 mouse->Release();di->Release();fclose(output);return 0;
}
