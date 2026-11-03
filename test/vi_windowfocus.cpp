// Two-window focus consumer and independent foreground witness. No OS input calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <shellapi.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <share.h>
#include <atomic>
#include <mutex>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
static FILE* output;
static HWND mainWindow=nullptr,borrowWindow=nullptr;
static bool active=false,createWindowAttack=false,workerAttack=false,ansiAttack=false,replaceChain=false,chainReplaced=false;
static WNDPROC changedPrevious=nullptr;
static std::atomic<bool> workerStarted{false};
static std::mutex logMutex;
static IDirectInput8W* directInput=nullptr;
static IDirectInputDevice8W* keyboard=nullptr;
static HINSTANCE instance;
static void Log(const char* fmt,...){std::lock_guard<std::mutex> lock(logMutex);va_list args;va_start(args,fmt);fprintf(output,"%llu ",GetTickCount64());vfprintf(output,fmt,args);fprintf(output,"\n");fflush(output);va_end(args);}
static DWORD WINAPI WorkerCreateAttack(LPVOID) {
    MSG message{};PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);Sleep(300);
    GUITHREADINFO info{sizeof(info)};GetGUIThreadInfo(0,&info);
    if(info.hwndActive!=borrowWindow){Log("WORKER_REFUSED actual=%p expected=%p",info.hwndActive,borrowWindow);return 0;}
    DWORD borrowed=GetWindowThreadProcessId(borrowWindow,nullptr),me=GetCurrentThreadId();
    BOOL attached=borrowed && borrowed!=me && AttachThreadInput(me,borrowed,TRUE);
    HWND popup=ansiAttack?
        CreateWindowExA(WS_EX_TOOLWINDOW,"vi_windowfocus_popup_a","Worker activation popup",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,300,220,mainWindow,nullptr,instance,nullptr):
        CreateWindowExW(WS_EX_TOOLWINDOW,L"vi_windowfocus_popup",L"Worker activation popup",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,300,220,mainWindow,nullptr,instance,nullptr);
    if(attached)AttachThreadInput(me,borrowed,FALSE);
    GetGUIThreadInfo(0,&info);Log("WORKER_CREATED ansi=%d attached=%d popup=%p actual=%p noactivate_style=%d visible=%d",(int)ansiAttack,(int)attached,popup,info.hwndActive,popup?(int)((GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0):-1,(int)IsWindowVisible(popup));
    ULONGLONG until=GetTickCount64()+2000;
    while(GetTickCount64()<until){while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}
    if(popup)DestroyWindow(popup);return 0;
}
static void ActivationHandler(HWND window){
    GUITHREADINFO info{sizeof(info)};GetGUIThreadInfo(0,&info);
    if(!borrowWindow || !IsWindow(borrowWindow) || info.hwndActive!=borrowWindow){Log("ATTACK_REFUSED actual=%p expected_owned_sentinel=%p",info.hwndActive,borrowWindow);return;}
    if(workerAttack){if(!workerStarted.exchange(true)){HANDLE worker=CreateThread(nullptr,0,WorkerCreateAttack,nullptr,0,nullptr);if(worker)CloseHandle(worker);Log("WORKER_SCHEDULED ansi=%d",(int)ansiAttack);}return;}
    DWORD target=GetWindowThreadProcessId(borrowWindow,nullptr),me=GetCurrentThreadId();
    BOOL attached=target && target!=me && AttachThreadInput(me,target,TRUE);
    BOOL set=SetForegroundWindow(window);SwitchToThisWindow(window,TRUE);
    BOOL shown=ShowWindowAsync(window,SW_RESTORE),top=BringWindowToTop(window);
    if(createWindowAttack) {
        HWND popup=CreateWindowExW(WS_EX_TOOLWINDOW,L"vi_windowfocus_popup",L"Activation callback popup",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,300,220,window,nullptr,instance,nullptr);
        GetGUIThreadInfo(0,&info);Log("ATTACK_CREATE_WINDOW popup=%p actual=%p",popup,info.hwndActive);
    }
    if(attached)AttachThreadInput(me,target,FALSE);
    GetGUIThreadInfo(0,&info);Log("ATTACK_CALLED attached=%d set=%d shown=%d top=%d actual=%p",(int)attached,(int)set,(int)shown,(int)top,info.hwndActive);
}
static LRESULT CALLBACK ChangedChain(HWND window,UINT message,WPARAM w,LPARAM l){return CallWindowProcW(changedPrevious,window,message,w,l);}
static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l){
    const bool main=GetWindowLongPtrW(window,GWLP_USERDATA)!=0;
    if(message==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);}
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(main && message==WM_ACTIVATEAPP){active=w!=0;Log("MAIN_ACTIVATEAPP %d",(int)active);if(active){ActivationHandler(window);if(replaceChain&&!chainReplaced){changedPrevious=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)ChangedChain);chainReplaced=true;Log("CHAIN_REPLACED previous=%p",changedPrevious);}}return 0;}
    if(main && message==WM_ACTIVATE){active=LOWORD(w)!=WA_INACTIVE;Log("MAIN_ACTIVATE %d",(int)active);if(active)ActivationHandler(window);return 0;}
    if(main && message==WM_KILLFOCUS){active=false;Log("MAIN_KILLFOCUS");return 0;}
    if(message==WM_APP+20){
        borrowWindow=(HWND)w;
        mainWindow=CreateWindowExW(0,L"vi_windowfocus_main",L"Focus main",WS_OVERLAPPEDWINDOW,-28000,600,900,620,nullptr,nullptr,instance,(LPVOID)1);
        // Hidden => undiscoverable by vinput's visible-window enumeration. The consumer
        // definitely loses activation before becoming eligible for subclass adoption.
        SendMessageW(mainWindow,WM_ACTIVATEAPP,FALSE,0);SendMessageW(mainWindow,WM_ACTIVATE,WA_INACTIVE,0);
        Log("MAIN_INACTIVE_BEFORE_VISIBLE hwnd=0x%llX",(unsigned long long)(ULONG_PTR)mainWindow);
        if(directInput){directInput->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr);if(keyboard){keyboard->SetDataFormat(&c_dfDIKeyboard);keyboard->SetCooperativeLevel(mainWindow,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);DIPROPDWORD buffer{};buffer.diph.dwSize=sizeof(buffer);buffer.diph.dwHeaderSize=sizeof(buffer.diph);buffer.diph.dwHow=DIPH_DEVICE;buffer.dwData=128;keyboard->SetProperty(DIPROP_BUFFERSIZE,&buffer.diph);}}
        ShowWindow(mainWindow,SW_SHOWNOACTIVATE);Log("MAIN_VISIBLE hwnd=0x%llX",(unsigned long long)(ULONG_PTR)mainWindow);return 0;
    }
    if(message==WM_APP+22){workerStarted=false;Log("WORKER_RESET");return 0;}
    if(message==WM_APP+21){Log("CONSUMER active=%d",(int)active);return 0;}
    if(message==WM_DESTROY){if(!main)PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
static LRESULT CALLBACK PopupProc(HWND window,UINT message,WPARAM w,LPARAM l){if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return IsWindowUnicode(window)?DefWindowProcW(window,message,w,l):DefWindowProcA(window,message,w,l);}
static DWORD witnessedPid=0;static unsigned eventGains=0;
static void CALLBACK ForegroundEvent(HWINEVENTHOOK,DWORD,HWND window,LONG,LONG,DWORD,DWORD){DWORD owner=0;GetWindowThreadProcessId(window,&owner);if(owner==witnessedPid){++eventGains;Log("FOREGROUND_EVENT hwnd=%p",window);}}
static int Witness(DWORD gamePid,DWORD duration){
    witnessedPid=gamePid;MSG msg{};PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    HWINEVENTHOOK hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    Log("WITNESS_READY hook=%d",hook!=nullptr);if(!hook)return 3;
    unsigned samples=0;ULONGLONG start=GetTickCount64(),run=0,longest=0;
    do{while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);ULONGLONG now=GetTickCount64();if(owner==gamePid){if(!run)run=now;++samples;if(now-run>longest)longest=now-run;}else run=0;Sleep(2);}while(GetTickCount64()-start<duration);
    UnhookWinEvent(hook);DWORD ended=0;GetWindowThreadProcessId(GetForegroundWindow(),&ended);
    Log("WITNESS_RESULT events=%u samples=%u held_ms=%llu ended_target=%d",eventGains,samples,longest,(int)(ended==gamePid));return eventGains||samples?1:0;
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int){
    int argc=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;
    if(argc>=5 && !wcscmp(args[1],L"--witness")){output=_wfsopen(args[3],L"w",_SH_DENYNO);if(!output)return 2;int result=Witness(wcstoul(args[2],nullptr,10),wcstoul(args[4],nullptr,10));fclose(output);return result;}
    output=_wfsopen(args[1],L"w",_SH_DENYNO);if(!output)return 2;instance=inst;
    for(int i=2;i<argc;i++){if(!wcscmp(args[i],L"--create-window-attack"))createWindowAttack=true;if(!wcscmp(args[i],L"--worker-create-attack"))workerAttack=true;if(!wcscmp(args[i],L"--ansi-create-attack"))ansiAttack=true;if(!wcscmp(args[i],L"--replace-chain"))replaceChain=true;}
    WNDCLASSW cls{};cls.hInstance=inst;cls.lpfnWndProc=Proc;cls.lpszClassName=L"vi_windowfocus_splash";RegisterClassW(&cls);cls.lpszClassName=L"vi_windowfocus_main";RegisterClassW(&cls);cls.lpszClassName=L"vi_windowfocus_popup";cls.lpfnWndProc=PopupProc;RegisterClassW(&cls);
    WNDCLASSA ansiClass{};ansiClass.hInstance=inst;ansiClass.lpfnWndProc=PopupProc;ansiClass.lpszClassName="vi_windowfocus_popup_a";RegisterClassA(&ansiClass);
    HWND splash=CreateWindowExW(WS_EX_NOACTIVATE,L"vi_windowfocus_splash",L"Focus splash",WS_OVERLAPPEDWINDOW,-28000,600,480,300,nullptr,nullptr,inst,nullptr);
    ShowWindow(splash,SW_SHOWNOACTIVATE);Log("SPLASH hwnd=0x%llX",(unsigned long long)(ULONG_PTR)splash);
    DirectInput8Create(inst,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&directInput,nullptr);
    bool running=true;while(running){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(active&&keyboard){DIDEVICEOBJECTDATA data[32]{};DWORD count=32;HRESULT hr=keyboard->GetDeviceData(sizeof(data[0]),data,&count,0);if(FAILED(hr)){keyboard->Acquire();count=32;hr=keyboard->GetDeviceData(sizeof(data[0]),data,&count,0);}if(SUCCEEDED(hr))for(DWORD i=0;i<count;i++)if(data[i].dwData&0x80)Log("DI_ACTION scan=0x%02lX",data[i].dwOfs);}Sleep(5);}
    if(keyboard)keyboard->Release();if(directInput)directInput->Release();fclose(output);return 0;
}
