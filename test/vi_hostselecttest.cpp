// Actual CLI selector, hidden owned windows only. No activation/system-input calls.
#define wmain vinput_cli_entry_unused
#include "vinput_cli.cpp"
#undef wmain
#include "vi_foreground_observer.h"
static int checks=0,failures=0;
static void Check(bool value,const char* name){++checks;if(!value)++failures;printf("%s %s\n",value?"PASS":"FAIL",name);}
static std::wstring OwnedCanonical(const wchar_t* path){
 wchar_t buffer[32768]{};DWORD n=GetFullPathNameW(path,32768,buffer,nullptr);return n&&n<32768?std::wstring(buffer,n):std::wstring();
}
struct ViHostSelectFind {DWORD pid=0;HWND h=nullptr;const wchar_t* klass=nullptr;};
static BOOL CALLBACK FindOwned(HWND h,LPARAM value){
 auto* find=(ViHostSelectFind*)value;DWORD owner=0;wchar_t klass[128]{};
 if(GetWindowThreadProcessId(h,&owner)&&owner==find->pid&&IsWindowVisible(h)&&GetClassNameW(h,klass,128)&&!wcscmp(klass,find->klass)){find->h=h;return FALSE;}return TRUE;
}
struct Owned {
 PROCESS_INFORMATION process{};HWND window=nullptr,intermediate=nullptr;DWORD windowThread=0;FILETIME creation{};bool identityKnown=false,hostMode=false;std::wstring expectedImage;
 bool IdentityCurrent(bool establish=false){
  if(!process.hProcess||WaitForSingleObject(process.hProcess,0)!=WAIT_TIMEOUT||GetProcessId(process.hProcess)!=process.dwProcessId)return false;
  FILETIME born{},exited{},kernel{},user{};if(!GetProcessTimes(process.hProcess,&born,&exited,&kernel,&user)||(!born.dwLowDateTime&&!born.dwHighDateTime))return false;
  if(identityKnown&&(born.dwLowDateTime!=creation.dwLowDateTime||born.dwHighDateTime!=creation.dwHighDateTime))return false;
  wchar_t image[32768]{};DWORD length=32768;if(!QueryFullProcessImageNameW(process.hProcess,0,image,&length)||!length||_wcsicmp(expectedImage.c_str(),image))return false;
  if(!identityKnown){if(!establish)return false;creation=born;identityKnown=true;}return true;
 }
 bool WindowCurrent(HWND h,const wchar_t* expected,HWND parent=nullptr,DWORD thread=0){
  DWORD pid=0;wchar_t klass[128]{};DWORD tid=GetWindowThreadProcessId(h,&pid);
  return h&&IsWindow(h)&&IsWindowVisible(h)&&tid&&pid==process.dwProcessId&&(!thread||tid==thread)&&(!parent||GetParent(h)==parent)&&GetClassNameW(h,klass,128)&&!wcscmp(klass,expected);
 }
 bool ReadyCurrent(){
  if(!IdentityCurrent()||!WindowCurrent(window,hostMode?L"ApplicationFrameWindow":L"vi_focusprobe",nullptr,windowThread))return false;
  return !hostMode||WindowCurrent(intermediate,L"vi_focusprobe_intermediate",window,windowThread);
 }
 bool Start(const wchar_t* exe,const std::wstring& log,bool host){
  expectedImage=OwnedCanonical(exe);hostMode=host;if(expectedImage.empty())return false;
  std::wstring command=L"\""+expectedImage+L"\" \""+log+L"\""+(host?L" --host":L"");STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
  if(!CreateProcessW(expectedImage.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
  // One original3s readiness budget covers root AND required intermediate child.
  const ULONGLONG until=GetTickCount64()+3000;
  do{
   if(WaitForSingleObject(process.hProcess,0)!=WAIT_TIMEOUT)return false;
   if(IdentityCurrent(true)){
    ViHostSelectFind find{process.dwProcessId,nullptr,host?L"ApplicationFrameWindow":L"vi_focusprobe"};EnumWindows(FindOwned,(LPARAM)&find);window=find.h;
    if(window){windowThread=GetWindowThreadProcessId(window,nullptr);intermediate=host?FindWindowExW(window,nullptr,L"vi_focusprobe_intermediate",nullptr):nullptr;if(ReadyCurrent())return true;}
   }
   Sleep(10);
  }while(GetTickCount64()<until);
  fprintf(stderr,"STARTUP_TIMEOUT host=%d pid=%lu root=%p child=%p identity_known=%d budget_ms=3000\n",(int)hostMode,process.dwProcessId,window,intermediate,(int)identityKnown);return false;
 }
 bool Stop(){
  bool stopped=true;if(process.hProcess){
   DWORD state=WaitForSingleObject(process.hProcess,0);
   if(state==WAIT_TIMEOUT){
    if(IdentityCurrent()){TerminateProcess(process.hProcess,0);stopped=WaitForSingleObject(process.hProcess,1000)==WAIT_OBJECT_0;}
    else{fprintf(stderr,"CLEANUP_REFUSED pid=%lu identity_unverified\n",process.dwProcessId);stopped=false;}
   }else stopped=state==WAIT_OBJECT_0;
   CloseHandle(process.hProcess);if(process.hThread)CloseHandle(process.hThread);process={};
  }return stopped;
 }
 ~Owned(){Stop();}
};
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;HWND original=GetForegroundWindow();Owned sentinel,host;
 if(!sentinel.Start(argv[1],std::wstring(argv[2])+L".sentinel.log",false)||!host.Start(argv[1],std::wstring(argv[2])+L".host.log",true))return 3;
 WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Windows.UI.Core.CoreWindow";RegisterClassW(&cls);
 HWND child=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"owned-selector-child",WS_OVERLAPPEDWINDOW,-28000,600,400,240,nullptr,nullptr,cls.hInstance,nullptr);if(!child)return 4;ShowWindow(child,SW_SHOWNOACTIVATE);
 HWND intermediate=host.intermediate;if(!intermediate||!host.ReadyCurrent())return 5;
 g_launchPid=GetCurrentProcessId();g_initialLaunchPid=GetCurrentProcessId();g_previousForeground=sentinel.window;
 NoteForeground(sentinel.window);NoteForeground(host.window);
 Check(PickRestoreTarget()==host.window,"future host is an outside destination before adoption");
 SetWindowLongPtrW(child,GWL_STYLE,(GetWindowLongPtrW(child,GWL_STYLE)&~(LONG_PTR)WS_OVERLAPPEDWINDOW)|WS_CHILD|WS_VISIBLE);SetParent(child,intermediate);
 Check(GetAncestor(child,GA_ROOT)==host.window && ForegroundIsTarget(host.window,GetCurrentProcessId()),"actual nested external host maps to current target");
 Check(!ForegroundIsTarget(host.window,GetCurrentProcessId()+1),"host relation cannot authorize a wrong target PID");
 Check(PickRestoreTarget()==sentinel.window,"CLI history skips own host after adoption");
 g_fgHistory.clear();g_previousForeground=host.window;
 Check(PickRestoreTarget()!=host.window,"CLI fallback rejects current own host");
 g_previousForeground=child;Check(PickRestoreTarget()!=child,"CLI fallback rejects own process window");
 g_previousForeground=sentinel.window;Check(PickRestoreTarget()==sentinel.window,"CLI preserves valid external fallback");
 const DWORD oracleOwned[]={101,102,103};
 Check(ViForegroundOutsideOwned(201,oracleOwned),"observer accepts initial external foreground PID");
 Check(ViForegroundOutsideOwned(202,oracleOwned),"observer accepts changed external foreground PID");
 Check(!ViForegroundOutsideOwned(101,oracleOwned),"observer rejects own fixture PID");
 Check(!ViForegroundOutsideOwned(102,oracleOwned) && !ViForegroundOutsideOwned(103,oracleOwned),"observer rejects both owned helper PIDs");
 Check(ViForegroundOutsideOwned(0,oracleOwned),"observer permits transient absent foreground");
 HWND observed=GetForegroundWindow();DWORD observedOwner=0;GetWindowThreadProcessId(observed,&observedOwner);
 const DWORD owned[]={GetCurrentProcessId(),sentinel.process.dwProcessId,host.process.dwProcessId};
 printf("OBSERVER before=%p after=%p owner=%lu external_changed=%d\n",original,observed,observedOwner,(int)(original!=observed));
 Check(ViForegroundOutsideOwned(observedOwner,owned),"final actual foreground sample excludes all owned fixture PIDs");
 DestroyWindow(child);bool hostStopped=host.Stop(),sentinelStopped=sentinel.Stop();Check(hostStopped&&sentinelStopped,"owned helpers accounted through verified original handles");printf("%d PASS / %d FAIL\n",checks-failures,failures);return failures?1:0;
}
