// Focus/error-dialog/relaunch controls. Runs offscreen and nonactivating except explicitly requested
// --steal-* controls (the harness holds the shared foreground lock). Never sends OS mouse/keyboard input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* g_log;
static bool g_interactive=false;
static bool g_sourceProbe=false;
static void Note(const char* what, HWND h=nullptr) { fprintf(g_log,"%llu %s hwnd=0x%llX\n",GetTickCount64(),what,(unsigned long long)(ULONG_PTR)h); fflush(g_log); }
static HWND RealForeground() { return GetForegroundWindow(); }
static HWND g_eventTarget=nullptr;
static bool g_eventSeen=false;
static void CALLBACK ForegroundEvent(HWINEVENTHOOK,DWORD event,HWND h,LONG,LONG,DWORD,DWORD) {
    if(event==EVENT_SYSTEM_FOREGROUND && h==g_eventTarget)g_eventSeen=true;
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_MOUSEACTIVATE || m==WM_LBUTTONDOWN || m==WM_LBUTTONUP) {
        INPUT_MESSAGE_SOURCE source{};BOOL got=GetCurrentInputMessageSource(&source);
        fprintf(g_log,"%llu MOUSE_RECEIPT message=0x%X interactive=%d ok=%d device=%d origin=%d\n",GetTickCount64(),m,(int)g_interactive,(int)got,(int)source.deviceType,(int)source.originId);fflush(g_log);
    }
    if(m==WM_INPUT && g_sourceProbe) {
        RAWINPUT input{};UINT size=sizeof(input);
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=(UINT)-1 && input.header.dwType==RIM_TYPEKEYBOARD && input.data.keyboard.VKey==VK_MENU) {
            fprintf(g_log,"%llu ALT_RAW flags=%u device_nonnull=%d foreground_self=%d\n",GetTickCount64(),input.data.keyboard.Flags,input.header.hDevice!=nullptr,(int)(GetForegroundWindow()==h));fflush(g_log);
        }
    }
    if((m==WM_KEYUP || m==WM_SYSKEYUP || m==WM_KEYDOWN || m==WM_SYSKEYDOWN) && w==VK_MENU) {
        INPUT_MESSAGE_SOURCE source{};BOOL got=GetCurrentInputMessageSource(&source);
        fprintf(g_log,"%llu ALT_SOURCE message=0x%X ok=%d device=%d origin=%d foreground_self=%d\n",GetTickCount64(),m,(int)got,(int)source.deviceType,(int)source.originId,(int)(GetForegroundWindow()==h));fflush(g_log);
    }
    if(m==WM_MOUSEACTIVATE && !g_interactive)return MA_NOACTIVATE;
    if(m==WM_APP+1) { SwitchToThisWindow(h,TRUE); Note("SWITCH_CALLED",h); return 0; }
    if(m==WM_APP+2) { ShowWindowAsync(h,SW_RESTORE); Note("SHOWASYNC_CALLED",h); return 0; }
    if(m==WM_APP+3) { SetForegroundWindow(h); Note("SETFG_CALLED",h); return 0; }
    if(m==WM_APP+4) { AllowSetForegroundWindow(ASFW_ANY); Note("ALLOWFG_CALLED",h); return 0; }
    if(m==WM_APP+5) {
        HWND parent=(HWND)w;
        SetWindowLongPtrW(h,GWL_STYLE,(GetWindowLongPtrW(h,GWL_STYLE)&~(LONG_PTR)WS_OVERLAPPEDWINDOW)|WS_CHILD|WS_VISIBLE);
        SetParent(h,parent);
        SetWindowPos(h,nullptr,10,10,400,220,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
        Note(GetParent(h)==parent ? "HOSTED" : "HOST_FAILED",h);
        Note("HOSTED_ROOT",GetAncestor(h,GA_ROOT));return 0;
    }
    if(m==WM_PAINT) { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT r;GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)(COLOR_WINDOW+1));TextOutW(dc,20,20,L"Focus probe",11);EndPaint(h,&ps);return 0; }
    if(m==WM_DESTROY) { PostQuitMessage(0);return 0; }
    return DefWindowProcW(h,m,w,l);
}
static bool Steal(HWND target,bool observeOnly=false) {
    MSG queued{};PeekMessageW(&queued,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    g_eventTarget=target;g_eventSeen=false;SetLastError(0);
    HWINEVENTHOOK eventHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    DWORD hookError=eventHook?0:GetLastError();
    HWND before=RealForeground(); DWORD tid=GetWindowThreadProcessId(before,nullptr), me=GetCurrentThreadId();
    bool attached=false,switchAttempted=false;DWORD attachError=0,setError=0,topError=0;BOOL set=FALSE,top=FALSE;
    HWND primary=before;
    bool snapshotSeen=before==target;
    if(!observeOnly) {
        SetLastError(0);attached=tid && tid!=me && AttachThreadInput(me,tid,TRUE);attachError=GetLastError();
        SetLastError(0);set=SetForegroundWindow(target);setError=GetLastError();
        if(RealForeground()==target)snapshotSeen=true;
        SetLastError(0);top=BringWindowToTop(target);topError=GetLastError();
        primary=RealForeground();switchAttempted=primary!=target;
        if(primary==target)snapshotSeen=true;
        if(switchAttempted)SwitchToThisWindow(target,TRUE);
    }
    HWND immediate=RealForeground();if(immediate==target)snapshotSeen=true;
    // Detach the human's queue before pumping our out-of-context event hook.
    // Queued exact-HWND foreground events survive a faster watchdog hand-back.
    if(attached)AttachThreadInput(me,tid,FALSE);
    ULONGLONG until=GetTickCount64()+100;
    do {
        while(PeekMessageW(&queued,nullptr,0,0,PM_REMOVE)){TranslateMessage(&queued);DispatchMessageW(&queued);}
        if(RealForeground()==target)snapshotSeen=true;Sleep(2);
    } while(GetTickCount64()<until);
    if(eventHook)UnhookWinEvent(eventHook);
    bool activated=snapshotSeen||g_eventSeen;
    if(g_log){fprintf(g_log,"%llu HELPER queue=1 observe_only=%d event_hook=%d hook_error=%lu attached=%d attach_error=%lu set=%d set_error=%lu top=%d top_error=%lu switch_attempted=%d before=0x%llX target=0x%llX primary_actual=0x%llX immediate_actual=0x%llX actual=0x%llX snapshot_seen=%d event_seen=%d activated=%d\n",GetTickCount64(),(int)observeOnly,eventHook!=nullptr,hookError,(int)attached,attachError,(int)set,setError,(int)top,topError,(int)switchAttempted,(unsigned long long)(ULONG_PTR)before,(unsigned long long)(ULONG_PTR)target,(unsigned long long)(ULONG_PTR)primary,(unsigned long long)(ULONG_PTR)immediate,(unsigned long long)(ULONG_PTR)RealForeground(),(int)snapshotSeen,(int)g_eventSeen,(int)activated);fflush(g_log);}
    return activated;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int) {
    int argc=0; wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>2 && (!wcscmp(argv[1],L"--steal") || !wcscmp(argv[1],L"--observe-only"))) {
        if(argc>3)g_log=_wfsopen(argv[3],L"w",_SH_DENYNO);
        bool activated=Steal((HWND)(ULONG_PTR)wcstoull(argv[2],nullptr,0),!wcscmp(argv[1],L"--observe-only"));if(g_log)fclose(g_log);return activated ? 0 : 1;
    }
    if(argc<2)return 2;
    g_log=_wfsopen(argv[1],L"w",_SH_DENYNO); if(!g_log)return 3;
    bool dialog=false, startupSteal=false, host=false, core=false; int delayedSteal=-1, delayWindow=0,x=-28000,y=600;
    for(int i=2;i<argc;++i){
        if(!wcscmp(argv[i],L"--dialog"))dialog=true;
        else if(!wcscmp(argv[i],L"--host"))host=true;
        else if(!wcscmp(argv[i],L"--core-window"))core=true;
        else if(!wcscmp(argv[i],L"--steal-at-window"))startupSteal=true;
        else if(!wcscmp(argv[i],L"--steal-after") && i+1<argc)delayedSteal=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--window-delay") && i+1<argc)delayWindow=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--interactive") || !wcscmp(argv[i],L"--normal-sentinel"))g_interactive=true;
        else if(!wcscmp(argv[i],L"--input-source-probe"))g_sourceProbe=true;
        else if(!wcscmp(argv[i],L"--x") && i+1<argc)x=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--y") && i+1<argc)y=_wtoi(argv[++i]);
    }
    Sleep(delayWindow>0 ? (DWORD)delayWindow : 0);
    WNDCLASSW wc{};wc.hInstance=inst;wc.lpfnWndProc=Proc;wc.lpszClassName=L"vi_focusprobe";RegisterClassW(&wc);
    if(host){wc.lpszClassName=L"ApplicationFrameWindow";RegisterClassW(&wc);}
    else if(core){wc.lpszClassName=L"Windows.UI.Core.CoreWindow";RegisterClassW(&wc);}
    HWND h=CreateWindowExW(g_interactive?0:WS_EX_NOACTIVATE,dialog?L"#32770":wc.lpszClassName,
        dialog?L"Fatal Error":L"Focus probe",WS_OVERLAPPEDWINDOW,x,y,480,300,nullptr,nullptr,inst,nullptr);
    if(!h)return 4;
    if(dialog)CreateWindowExW(0,L"STATIC",L"Forced GfxDevice 'Vulkan' was not built into this application",WS_CHILD|WS_VISIBLE,10,10,440,100,h,nullptr,inst,nullptr);
    ShowWindow(h,SW_SHOWNOACTIVATE);Note("WINDOW",h);
    fprintf(g_log,"%llu WINDOW_STYLE interactive=%d ex=0x%llX visible=%d\n",GetTickCount64(),(int)g_interactive,(unsigned long long)GetWindowLongPtrW(h,GWL_EXSTYLE),(int)IsWindowVisible(h));fflush(g_log);
    if(g_sourceProbe){RAWINPUTDEVICE keyboard{1,6,0,h};BOOL registered=RegisterRawInputDevices(&keyboard,1,sizeof(keyboard));fprintf(g_log,"%llu SOURCE_PROBE raw_foreground_only=%d\n",GetTickCount64(),(int)registered);fflush(g_log);}
    if(host) {
        wc.lpszClassName=L"vi_focusprobe_intermediate";RegisterClassW(&wc);
        HWND intermediate=CreateWindowExW(0,wc.lpszClassName,L"Intermediate",WS_CHILD|WS_VISIBLE,0,0,460,260,h,nullptr,inst,nullptr);
        Note("HOST_ROOT",h);Note("HOST_INTERMEDIATE",intermediate);
    }
    ULONGLONG start=GetTickCount64(); bool stolen=false; HANDLE helper=nullptr; DWORD helperPid=0;
    for(;;) {
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){fclose(g_log);return 0;}TranslateMessage(&m);DispatchMessageW(&m);}
        if(!stolen && (startupSteal || (delayedSteal>=0 && GetTickCount64()-start>=(ULONGLONG)delayedSteal))) {
            stolen=true; wchar_t self[MAX_PATH],cl[MAX_PATH+80];GetModuleFileNameW(nullptr,self,MAX_PATH);
            swprintf_s(cl,L"\"%s\" --steal 0x%llX",self,(unsigned long long)(ULONG_PTR)h);
            STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE; PROCESS_INFORMATION pi{};
            if(CreateProcessW(self,cl,nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){helper=pi.hProcess;helperPid=pi.dwProcessId;CloseHandle(pi.hThread);Note("STEAL_STARTED",h);}
        }
        if(helper && WaitForSingleObject(helper,0)==WAIT_OBJECT_0){CloseHandle(helper);helper=nullptr;fprintf(g_log,"%llu STEAL_FINISHED pid=%lu\n",GetTickCount64(),helperPid);fflush(g_log);}
        Sleep(5);
    }
}
