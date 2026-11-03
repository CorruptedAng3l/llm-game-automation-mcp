// Actual CLI selector, hidden owned windows only. No activation/system-input calls.
#define wmain vinput_cli_entry_unused
#include "vinput_cli.cpp"
#undef wmain
#include "vi_foreground_observer.h"
static int checks=0,failures=0;
static void Check(bool value,const char* name){++checks;if(!value)++failures;printf("%s %s\n",value?"PASS":"FAIL",name);}
static BOOL CALLBACK FindOwned(HWND h,LPARAM value){auto* find=(WinFind*)value;DWORD owner=0;GetWindowThreadProcessId(h,&owner);if(owner==find->pid && IsWindowVisible(h)){find->h=h;return FALSE;}return TRUE;}
struct Owned {
 PROCESS_INFORMATION process{};HWND window=nullptr;
 bool Start(const wchar_t* exe,const std::wstring& log,bool host){std::wstring command=L"\""+std::wstring(exe)+L"\" \""+log+L"\""+(host?L" --host":L"");STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
  if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
  ULONGLONG until=GetTickCount64()+3000;do{WinFind find{process.dwProcessId,nullptr};EnumWindows(FindOwned,(LPARAM)&find);window=find.h;if(window)return true;Sleep(10);}while(GetTickCount64()<until);return false;}
 ~Owned(){if(process.hProcess){TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,1000);CloseHandle(process.hProcess);CloseHandle(process.hThread);}}
};
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;HWND original=GetForegroundWindow();Owned sentinel,host;
 if(!sentinel.Start(argv[1],std::wstring(argv[2])+L".sentinel.log",false)||!host.Start(argv[1],std::wstring(argv[2])+L".host.log",true))return 3;
 WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Windows.UI.Core.CoreWindow";RegisterClassW(&cls);
 HWND child=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"owned-selector-child",WS_OVERLAPPEDWINDOW,-28000,600,400,240,nullptr,nullptr,cls.hInstance,nullptr);if(!child)return 4;ShowWindow(child,SW_SHOWNOACTIVATE);
 HWND intermediate=FindWindowExW(host.window,nullptr,L"vi_focusprobe_intermediate",nullptr);if(!intermediate)return 5;
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
 DestroyWindow(child);printf("%d PASS / %d FAIL\n",checks-failures,failures);return failures?1:0;
}
