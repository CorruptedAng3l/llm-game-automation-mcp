// Actual CLI restore paths, simulated foreground APIs and an owned real stalled pipe.
// No native activation, AttachThreadInput, keyboard or mouse operation is performed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <thread>
static HWND simulated=nullptr,targetWindow=nullptr,outsideWindow=nullptr,alternateWindow=nullptr;
static bool changeOnLookup=false,changeOnAttach=false,lookupOwn=false,attachOwn=false;
static unsigned foregroundCalls=0,topCalls=0;
static HWND ActualForeground(){return GetForegroundWindow();}
static DWORD ActualOwner(HWND h,LPDWORD pid){return GetWindowThreadProcessId(h,pid);}
static HWND WINAPI SampleForeground(){return simulated;}
static BOOL WINAPI AttemptForeground(HWND){++foregroundCalls;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI AttemptTop(HWND){++topCalls;return FALSE;}
static BOOL WINAPI AttemptAttach(DWORD,DWORD,BOOL attach){if(!attach)return TRUE;if(changeOnAttach){simulated=attachOwn?alternateWindow:outsideWindow;return TRUE;}SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static DWORD WINAPI SampleOwner(HWND h,LPDWORD pid){DWORD tid=ActualOwner(h,pid);if(h==targetWindow){if(changeOnLookup){changeOnLookup=false;simulated=lookupOwn?alternateWindow:outsideWindow;}return tid+1;}return tid;}
#define GetForegroundWindow SampleForeground
#define SetForegroundWindow AttemptForeground
#define BringWindowToTop AttemptTop
#define AttachThreadInput AttemptAttach
#define GetWindowThreadProcessId SampleOwner
#define wmain vinput_cli_entry_unused
#include "vinput_cli.cpp"
#undef wmain
#undef GetForegroundWindow
#undef SetForegroundWindow
#undef BringWindowToTop
#undef AttachThreadInput
#undef GetWindowThreadProcessId
#include "vi_foreground_observer.h"
static unsigned checks=0,failures=0;
static void Check(bool yes,const char* name){++checks;if(!yes)++failures;printf("%s %s\n",yes?"PASS":"FAIL",name);}
static void Reset(){simulated=targetWindow;changeOnLookup=changeOnAttach=lookupOwn=attachOwn=false;foregroundCalls=topCalls=0;}
static BOOL CALLBACK Find(HWND h,LPARAM value){auto* f=(WinFind*)value;DWORD pid=0;ActualOwner(h,&pid);if(pid==f->pid&&IsWindowVisible(h)){f->h=h;return FALSE;}return TRUE;}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;HWND original=ActualForeground();
 std::wstring command=L"\""+std::wstring(argv[1])+L"\" \""+argv[2]+L"\"";STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
 if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return 3;
 struct Stop {PROCESS_INFORMATION p;~Stop(){TerminateProcess(p.hProcess,0);WaitForSingleObject(p.hProcess,1000);CloseHandle(p.hProcess);CloseHandle(p.hThread);}} stop{child};
 ULONGLONG deadline=GetTickCount64()+3000;do{WinFind f{child.dwProcessId,nullptr};EnumWindows(Find,(LPARAM)&f);outsideWindow=f.h;if(outsideWindow)break;Sleep(5);}while(GetTickCount64()<deadline);if(!outsideWindow)return 4;
 targetWindow=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"restore-latency",WS_OVERLAPPEDWINDOW,-28000,600,400,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!targetWindow)return 5;ShowWindow(targetWindow,SW_SHOWNOACTIVATE);
 alternateWindow=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"owned-popup",WS_OVERLAPPEDWINDOW,-28000,600,400,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!alternateWindow)return 6;ShowWindow(alternateWindow,SW_SHOWNOACTIVATE);
 g_launchPid=GetCurrentProcessId();g_initialLaunchPid=GetCurrentProcessId();g_previousForeground=outsideWindow;NoteForeground(outsideWindow);
 {
  std::atomic<bool> ready{false},requestSeen{false};std::thread server([&]{wchar_t name[80];swprintf_s(name,L"\\\\.\\pipe\\vinput_%lu",GetCurrentProcessId());HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_WAIT,1,4096,4096,0,nullptr);ready=true;if(pipe==INVALID_HANDLE_VALUE)return;
   if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){char data[4096];DWORD n=0;if(ReadFile(pipe,data,sizeof(data),&n,nullptr)&&n){requestSeen=true;Sleep(350);const char reply[]="{\"ok\":true}\n";WriteFile(pipe,reply,sizeof(reply)-1,&n,nullptr);}}
   DisconnectNamedPipe(pipe);CloseHandle(pipe);});
  while(!ready)Sleep(1);Reset();ULONGLONG start=GetTickCount64();bool restored=RestoreForeground();ULONGLONG elapsed=GetTickCount64()-start;server.join();
  printf("MEASURE stalled_reply_ms=%llu request_seen=%d restored=%d\n",elapsed,(int)requestSeen.load(),(int)restored);
  Check(requestSeen&&!restored,"owned server received actual restore fallback and left simulated target foreground");
  Check(elapsed<150,"stalled restore reply cannot stall watcher beyond150ms");
 }
 Reset();ULONGLONG start=GetTickCount64();bool restored=RestoreForeground();ULONGLONG missingElapsed=GetTickCount64()-start;printf("MEASURE absent_pipe_ms=%llu\n",missingElapsed);Check(!restored&&missingElapsed<150,"absent pipe and restore observation have bounded deadline");
 Reset();std::atomic<bool> mutexAvailable{false};std::thread observer([&]{Sleep(150);if(g_restoreMx.try_lock()){mutexAvailable=true;g_restoreMx.unlock();}});start=GetTickCount64();GuardForeground(0,"stalled-fixture");ULONGLONG guardElapsed=GetTickCount64()-start;observer.join();printf("MEASURE guard_ms=%llu mutex_available_at150=%d\n",guardElapsed,(int)mutexAvailable.load());
 Check(guardElapsed<150&&mutexAvailable,"failed retry does not sleep250ms holding restore mutex");
 Reset();changeOnLookup=true;restored=RestoreForeground();Check(restored&&foregroundCalls==0&&simulated==outsideWindow,"newer choice before first API call receives no handback");
 Reset();changeOnAttach=true;restored=RestoreForeground();Check(restored&&foregroundCalls==1&&topCalls==0&&simulated==outsideWindow,"newer choice during attach receives no fallback handback");
 Reset();changeOnLookup=lookupOwn=true;restored=RestoreForeground();Check(!restored&&foregroundCalls==0&&simulated==alternateWindow,"new own HWND before first call is not falsely reported restored");
 Reset();changeOnAttach=attachOwn=true;restored=RestoreForeground();Check(!restored&&foregroundCalls==1&&topCalls==0&&simulated==alternateWindow,"own popup swap during attach is not falsely reported restored");
 const DWORD oracleOwned[]={101,102};
 Check(ViForegroundOutsideOwned(201,oracleOwned),"observer accepts initial external foreground PID");
 Check(ViForegroundOutsideOwned(202,oracleOwned),"observer accepts changed external foreground PID");
 Check(!ViForegroundOutsideOwned(101,oracleOwned),"observer rejects own fixture PID");
 Check(!ViForegroundOutsideOwned(102,oracleOwned),"observer rejects owned helper PID");
 Check(ViForegroundOutsideOwned(0,oracleOwned),"observer permits transient absent foreground");
 HWND observed=ActualForeground();DWORD observedOwner=0;ActualOwner(observed,&observedOwner);
 const DWORD owned[]={GetCurrentProcessId(),child.dwProcessId};
 printf("OBSERVER before=%p after=%p owner=%lu external_changed=%d\n",original,observed,observedOwner,(int)(original!=observed));
 Check(ViForegroundOutsideOwned(observedOwner,owned),"final actual foreground sample excludes all owned fixture PIDs");
 DestroyWindow(alternateWindow);DestroyWindow(targetWindow);printf("%u PASS / %u FAIL\n",checks-failures,failures);return failures?1:0;
}
