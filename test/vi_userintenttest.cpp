// Fake physical samples and hidden/offscreen HWNDs: never changes foreground or generates OS input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include "../src/vi_user_intent.h"
#pragma comment(lib,"user32.lib")
static DWORD tick=100000;
static bool alt=false,tab=false,mouse=false;
static HWND hit=nullptr;
static HWND shell=nullptr,physicalForeground=nullptr;
static vi_user_intent::Detector* loseOnAltSample=nullptr;
static SHORT WINAPI Key(int key){if(key==VK_MENU && loseOnAltSample){auto detector=loseOnAltSample;loseOnAltSample=nullptr;detector->Lost(tick);}return (SHORT)(((key==VK_MENU && alt)||(key==VK_TAB && tab)||(key==VK_LBUTTON && mouse))?0x8000:0);}
static BOOL WINAPI Cursor(LPPOINT p){*p={100,100};return TRUE;}
static HWND WINAPI Hit(POINT){return hit;}
static DWORD WINAPI Tick(){return tick;}
static HWND WINAPI Shell(){return shell;}
static HWND WINAPI Foreground(){return physicalForeground;}
static BOOL WINAPI Last(PLASTINPUTINFO p){p->dwTime=tick;return TRUE;}
static int failed=0,checks=0;
static void Check(bool yes,const char* name){++checks;printf("%s %s\n",yes?"PASS":"FAIL",name);if(!yes)++failed;}
static void Clear(HWND h){const wchar_t* keys[]={vi_user_intent::kProof,vi_user_intent::kKind,vi_user_intent::kActive,vi_user_intent::kHeartbeat};for(auto key:keys)RemovePropW(h,key);alt=tab=mouse=false;tick+=10000;}
static void Setup(vi_user_intent::Detector& d,HWND h){d.Configure({Key,Cursor,Hit,nullptr,Tick,Last,Shell});d.SetTarget(h,GetCurrentProcessId());}
static void SelectMouse(vi_user_intent::Detector& d,HWND target){d.Refresh(nullptr);hit=target;mouse=true;d.MouseActivate(target,GetCurrentProcessId(),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN),nullptr);++tick;d.Refresh(target);mouse=false;}
struct HostedState {HWND window,explorer;volatile LONG stop;};
int wmain(int argc,wchar_t** argv){
 if(argc==4 && !wcscmp(argv[1],L"--hidden-host")){
  HANDLE mapping=(HANDLE)(ULONG_PTR)_wcstoui64(argv[2],nullptr,16),ready=(HANDLE)(ULONG_PTR)_wcstoui64(argv[3],nullptr,16);
  auto state=(HostedState*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(HostedState));if(!state)return 3;
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MultitaskingViewFrame";RegisterClassW(&wc);
  state->window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"hidden-host",WS_POPUP,-28000,600,10,10,nullptr,nullptr,wc.hInstance,nullptr);
  wc.lpszClassName=L"CabinetWClass";RegisterClassW(&wc);state->explorer=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"ordinary-shell",WS_POPUP,-28000,600,10,10,nullptr,nullptr,wc.hInstance,nullptr);SetEvent(ready);
  while(!state->stop){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}
  if(state->explorer)DestroyWindow(state->explorer);if(state->window)DestroyWindow(state->window);UnmapViewOfFile(state);CloseHandle(mapping);CloseHandle(ready);return 0;
 }
 HWND a=CreateWindowExW(0,L"STATIC",L"intent-a",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 HWND b=CreateWindowExW(0,L"STATIC",L"intent-b",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 HWND outside=GetDesktopWindow();DWORD pid=GetCurrentProcessId();
 if(!a || !b)return 2;
 Check(!vi_user_intent::Matches(a,a,pid+1),"wrong expected PID cannot authorize a same-root window");
 Check(vi_user_intent::Matches(a,a,pid),"validated own target matches");
 {
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(HostedState),nullptr),ready=CreateEventW(&sa,TRUE,FALSE,nullptr);
  auto state=(HostedState*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(HostedState));
  wchar_t self[MAX_PATH],command[MAX_PATH+100];GetModuleFileNameW(nullptr,self,MAX_PATH);swprintf_s(command,L"\"%s\" --hidden-host %llX %llX",self,(unsigned long long)(ULONG_PTR)mapping,(unsigned long long)(ULONG_PTR)ready);
  STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION process{};bool created=state && CreateProcessW(self,command,nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&process);
  bool available=created && WaitForSingleObject(ready,3000)==WAIT_OBJECT_0 && state->window;
  HWND child=available?CreateWindowExW(0,L"STATIC",L"owned-child",WS_CHILD,0,0,5,5,state->window,nullptr,GetModuleHandleW(nullptr),nullptr):nullptr;
  Check(child && vi_user_intent::Matches(state->window,child,pid),"foreign host root matches validated owned child target");
  Check(child && !vi_user_intent::Matches(state->window,child,pid+1),"foreign host root rejects wrong target PID");
  if(available){
   shell=state->window;
   Check(vi_user_intent::ShellIntermediary(shell,shell),"hidden shell-owned NOACTIVATE+TOOLWINDOW staging HWND is neutral");
   {
    vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(shell);alt=tab=false;++tick;d.Refresh(shell);
    Check(d.Provisional() && !d.Active(),"shell staging at Alt release remains provisional");
    ++tick;d.Refresh(a);++tick;Check(d.Refresh(a),"target selection after narrow shell staging finalizes within bounded grace");
   }
   Clear(a);
   {
    vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(shell);alt=tab=false;++tick;d.Refresh(shell);tick+=101;
    Check(!d.Refresh(a) && !d.EvaluatingAltTab(),"shell staging cannot authorize target after hundred-millisecond release deadline");
   }
   Clear(a);ShowWindow(shell,SW_SHOWNOACTIVATE);
   Check(vi_user_intent::ShellIntermediary(shell,shell),"exact visible shell-owned MultitaskingViewFrame tool window is neutral");
   {
    vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(shell);alt=tab=false;++tick;d.Refresh(shell);++tick;
    d.Refresh(a);++tick;Check(d.Refresh(a),"visible shell switcher intermediary permits final physical target selection");
   }
   Clear(a);HWND explorer=state->explorer;ShowWindow(explorer,SW_SHOWNOACTIVATE);
   Check(explorer && !vi_user_intent::ShellIntermediary(explorer,shell),"ordinary visible CabinetWClass remains a real application selection");
   {
    vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(explorer);alt=tab=false;++tick;d.Refresh(explorer);++tick;
    Check(!d.Refresh(a),"visible Explorer selection rejects later target self-activation");
   }
   WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MultitaskingViewFrame";RegisterClassW(&wc);
   HWND wrong=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"wrong-owner",WS_POPUP,-28000,600,10,10,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(wrong,SW_SHOWNOACTIVATE);
   Check(wrong && !vi_user_intent::ShellIntermediary(wrong,shell),"exact switcher class with wrong PID cannot claim shell evidence");if(wrong)DestroyWindow(wrong);
   Clear(a);ShowWindow(shell,SW_HIDE);shell=nullptr;
  }
  if(child)DestroyWindow(child);if(state)InterlockedExchange(&state->stop,1);
  if(created){if(WaitForSingleObject(process.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,4);CloseHandle(process.hThread);CloseHandle(process.hProcess);}
  if(state)UnmapViewOfFile(state);if(mapping)CloseHandle(mapping);if(ready)CloseHandle(ready);
 }
 {
  vi_user_intent::Detector d;Setup(d,a);SelectMouse(d,a);Check(d.Active(),"target-bound physical click accepted");
  d.SetTarget(b,pid);Check(!d.Active() && !d.Provisional(),"retarget immediately revokes active and provisional state");
  Check(!d.Refresh(b),"another HWND cannot inherit earlier target selection without input");
  SelectMouse(d,b);Check(d.Active(),"new target can acquire its own physical selection");
  d.SetTarget(b,pid+1);Check(!d.Refresh(b),"PID retarget revokes prior selection");
 }
 Clear(a);Clear(b);
 {
  vi_user_intent::Detector d;d.Configure({Key,Cursor,Hit,Foreground,Tick,Last,Shell});d.SetTarget(a,pid);d.Refresh(outside);hit=a;mouse=true;physicalForeground=outside;++tick;
  Check(!d.Refresh(a) && !GetPropW(a,vi_user_intent::kProof),"original foreground resample rejects stale target snapshot during commit");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);hit=a;mouse=true;++tick;
  Check(d.Refresh(a),"launch watcher physical button edge at actual target hit survives foreground transition");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(a);
  alt=tab=false;++tick;d.Refresh(a);loseOnAltSample=&d;++tick;
  Check(!d.Refresh(a) && !GetPropW(a,vi_user_intent::kProof),"loss interleaved before stale Alt-release publication cannot resurrect intent");
  ++tick;Check(!d.Refresh(a) && !d.Provisional(),"loss epoch resets owner FSM and rejects later target self-activation");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);hit=outside;mouse=true;++tick;
  Check(!d.Refresh(a),"mouse edge over another app cannot authorize simultaneous target self-activation");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);SetPropW(a,vi_user_intent::kProof,(HANDLE)(ULONG_PTR)(tick-10000));SetPropW(a,vi_user_intent::kKind,(HANDLE)1);SetPropW(a,vi_user_intent::kActive,(HANDLE)(ULONG_PTR)pid);SetPropW(a,vi_user_intent::kHeartbeat,(HANDLE)(ULONG_PTR)(tick-10000));
  Check(!d.Refresh(a),"orphaned active marker and ten-second-old proof cannot authorize foreground");
 }
 Clear(a);
 {
  vi_user_intent::Detector launch;Setup(launch,a);SelectMouse(launch,a);tick+=1000;Check(launch.Refresh(a),"live launcher selection persists past original proof TTL");
  vi_user_intent::Detector dll;Setup(dll,a);Check(dll.Refresh(a),"fresh target heartbeat hands selection to late detector");
  tick+=301;vi_user_intent::Detector stale;Setup(stale,a);Check(!stale.Refresh(a),"heartbeat expires when no observer refreshes it");
  launch.Refresh(outside);Check(!launch.Active() && !GetPropW(a,vi_user_intent::kProof),"actual foreground loss clears shared authorization");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(a);
  Check(d.Provisional() && !d.Active(),"physical chord and target transition are provisional while Alt held");
  alt=tab=false;++tick;d.Refresh(a);++tick;Check(d.Refresh(a),"physical Alt release and stable exact target finalize takeover");
  d.Refresh(outside);Check(!d.Active(),"loss rearms after Alt-Tab takeover");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(a);tick+=5001;d.Refresh(a);
  Check(!d.EvaluatingAltTab() && !d.Active(),"held chord expires after five seconds");
  tick+=10;d.Refresh(a);Check(!d.EvaluatingAltTab(),"held Tab cannot renew expired selection timeout");
  tab=false;++tick;d.Refresh(a);tab=true;++tick;d.Refresh(a);Check(!d.EvaluatingAltTab(),"another Tab edge during same expired Alt hold cannot renew timeout");
  alt=tab=false;++tick;d.Refresh(a);++tick;Check(!d.Refresh(a),"release of expired chord does not grant takeover");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=true;++tick;d.Refresh(outside);tab=true;++tick;d.Refresh(a);tick+=4000;tab=false;d.Refresh(a);++tick;tab=true;d.Refresh(a);tick+=1001;d.Refresh(a);
  Check(!d.EvaluatingAltTab() && !d.Active(),"repeated Tab steps cannot extend the original five-second Alt session");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(nullptr);alt=true;++tick;d.Refresh(nullptr);tab=true;++tick;d.Refresh(outside);
  alt=tab=false;++tick;d.Refresh(outside);++tick;Check(!d.Refresh(a) && !d.Active(),"Alt-Tab selecting another app cannot authorize subsequent target self-activation");
 }
 Clear(a);
 {
  vi_user_intent::Detector d;Setup(d,a);d.Refresh(outside);alt=tab=true;++tick;d.Refresh(a);Check(d.Provisional(),"retarget regression begins with target preview");
  d.SetTarget(b,pid);Check(!d.Provisional() && !d.EvaluatingAltTab(),"retarget atomically revokes preview grace");
  ++tick;d.Refresh(b);alt=tab=false;++tick;d.Refresh(b);++tick;Check(!d.Refresh(b),"old held chord cannot authorize newly adopted target");
 }
 Clear(a);Clear(b);DestroyWindow(a);DestroyWindow(b);printf("%d PASS / %d FAIL\n",checks-failed,failed);return failed?1:0;
}
