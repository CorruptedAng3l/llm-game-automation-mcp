#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <string>
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_ack::ArmDeadline())return 111;
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=3){if(args)LocalFree(args);return 2;}
 wchar_t expectedLog[2048]{};const DWORD logSize=GetEnvironmentVariableW(L"VI_LAUNCH_TARGET_LOG",expectedLog,2048);if(!logSize||logSize>=2048||_wcsicmp(args[1],expectedLog)){LocalFree(args);return 111;}
 FILE* out=_wfsopen(args[1],L"w",_SH_DENYNO);const std::wstring mode=args[2];LocalFree(args);if(!out)return 2;
 const bool safe=vi_launch_terminal::Install()&&vi_launch_terminal::Verify();vi_launch_terminal::Record(out,"before_windows",safe);if(!safe){fclose(out);return 111;}
 FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)){fclose(out);return 2;}
 ULARGE_INTEGER identity{};identity.LowPart=created.dwLowDateTime;identity.HighPart=created.dwHighDateTime;
 fprintf(out,"{\"event\":\"owned_identity\",\"pid\":%lu,\"creation_filetime\":%llu}\n",GetCurrentProcessId(),identity.QuadPart);fflush(out);
 if(!vi_launch_ack::Wait(out,L"target")||!vi_launch_ack::ArmNoActivation()){fclose(out);return 111;}
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Proc;cls.lpszClassName=L"ViLaunchOwnedFixture";if(!RegisterClassW(&cls)){fclose(out);return 2;}
 const bool mainVisible=mode==L"normal"||mode==L"paused_queue"||mode==L"main_and_popup"||mode==L"disabled_owner"||mode==L"disabled_large_main"||mode==L"selection_mismatch";
 const wchar_t* mainClass=cls.lpszClassName;
 if(mode==L"selection_mismatch"){WNDCLASSW engine=cls;engine.lpszClassName=L"UnityWndClass";if(!RegisterClassW(&engine)){fclose(out);return 2;}mainClass=engine.lpszClassName;}
 HWND main=CreateWindowExW(WS_EX_NOACTIVATE,mainClass,L"Synthetic main",WS_OVERLAPPEDWINDOW|(mainVisible?WS_VISIBLE:0),-28000,600,500,300,nullptr,nullptr,instance,nullptr);
 if(!main){fclose(out);return 2;}
 HWND splash=nullptr,popup=nullptr;
 if(mode==L"disabled_large_main"||mode==L"selection_mismatch"){
  splash=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,(mode==L"selection_mismatch"?L"Secondary generic frame":L"Disabled large splash"),WS_OVERLAPPEDWINDOW|WS_VISIBLE,-27000,600,760,560,nullptr,nullptr,instance,nullptr);
  if(!splash){fclose(out);return 2;}if(mode==L"disabled_large_main")EnableWindow(splash,FALSE);
 }
 if(mode!=L"normal"&&mode!=L"paused_queue"&&mode!=L"hidden_unproven"&&mode!=L"selection_mismatch"){
  const bool unowned=mode==L"ownerless"||mode==L"borderless";
  const DWORD style=WS_POPUP|WS_VISIBLE|(mode==L"borderless"?0:WS_CAPTION);
  popup=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Owned startup error",style,-28000,600,900,700,unowned?nullptr:main,nullptr,instance,nullptr);
  if(!popup){fclose(out);return 2;}
  CreateWindowExW(0,L"STATIC",L"Synthetic renderer initialization failed",WS_CHILD|WS_VISIBLE,10,30,390,100,popup,nullptr,instance,nullptr);
  if(mode==L"disabled_owner")EnableWindow(main,FALSE);
 }
 fprintf(out,"{\"event\":\"windows_ready\",\"pid\":%lu,\"thread\":%lu,\"main\":%llu,\"popup\":%llu,\"owner\":%llu,\"splash\":%llu,\"main_enabled\":%s,\"main_visible\":%s,\"popup_visible\":%s,\"splash_visible\":%s}\n",GetCurrentProcessId(),GetCurrentThreadId(),(unsigned long long)(ULONG_PTR)main,(unsigned long long)(ULONG_PTR)popup,(unsigned long long)(ULONG_PTR)(popup?GetWindow(popup,GW_OWNER):nullptr),(unsigned long long)(ULONG_PTR)splash,IsWindowEnabled(main)?"true":"false",IsWindowVisible(main)?"true":"false",popup&&IsWindowVisible(popup)?"true":"false",splash&&IsWindowVisible(splash)?"true":"false");fflush(out);
 const ULONGLONG until=GetTickCount64()+90000;
 if(mode==L"paused_queue")Sleep(20000); // bounded own queue pause; pipe/monitor remain live
 while(GetTickCount64()<until){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}
 // Spies remain installed through cleanup. Never restore any physical state.
 if(popup)DestroyWindow(popup);if(splash)DestroyWindow(splash);DestroyWindow(main);
 vi_launch_terminal::Record(out,"target_exit",safe);fclose(out);return 0;
}
