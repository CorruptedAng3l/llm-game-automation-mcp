// Owned fake vinput server. It applies each requested key to its own UI, then
// deliberately loses/refuses ACK or hangs UI/status, modeling ambiguous delivery.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <atomic>
#include <cstdio>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
static HWND windowHandle;
static FILE* logFile;
static std::wstring mode;
static HANDLE hung;
static std::atomic<bool> menu{false};
static std::atomic<int> applied{0};
static void Note(const char* event,const std::string& payload={}){fprintf(logFile,"%llu %s applied=%d menu=%d %s\n",GetTickCount64(),event,applied.load(),(int)menu,payload.c_str());fflush(logFile);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_APP+1){menu=!menu;++applied;SetWindowTextW(h,menu?L"APPLIED INSERT - PANEL OPEN":L"APPLIED INSERT - PANEL CLOSED");Note("KEY_APPLIED");InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_APP+2){Note("UI_HANG_ENTER");SetEvent(hung);Sleep(4000);Note("UI_HANG_EXIT");return 0;}
    if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));TextOutW(dc,20,40,menu?L"PANEL OPEN":L"PANEL CLOSED",menu?10:12);EndPaint(h,&p);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void Reply(HANDLE pipe,const std::string& text){DWORD n=0;std::string line=text+"\n";WriteFile(pipe,line.data(),(DWORD)line.size(),&n,nullptr);FlushFileBuffers(pipe);}
static DWORD WINAPI Server(LPVOID){
    if(mode==L"--hang-preflight")WaitForSingleObject(hung,1000);
    wchar_t name[80];swprintf_s(name,L"\\\\.\\pipe\\vinput_%lu",GetCurrentProcessId());
    for(;;){
        HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,PIPE_UNLIMITED_INSTANCES,65536,65536,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)return 2;
        Note("PIPE_READY");if(!ConnectNamedPipe(pipe,nullptr)&&GetLastError()!=ERROR_PIPE_CONNECTED){CloseHandle(pipe);continue;}
        char buffer[8192];DWORD n=0;std::string request;
        while(ReadFile(pipe,buffer,sizeof(buffer),&n,nullptr)&&n){request.append(buffer,n);if(request.find('\n')!=std::string::npos)break;}
        Note("REQUEST",request);
        if(request.find("\"cmd\":\"status\"")!=std::string::npos){
            wchar_t title[128]={};SendMessageW(windowHandle,WM_GETTEXT,128,(LPARAM)title); // deliberate real UI dependency
            RECT r{};GetClientRect(windowHandle,&r);char json[300];sprintf_s(json,"{\"ok\":true,\"hwnd\":\"0x%llX\",\"client_w\":%ld,\"client_h\":%ld,\"frames\":0}",(unsigned long long)(ULONG_PTR)windowHandle,r.right,r.bottom);Reply(pipe,json);
        }else if(request.find("\"cmd\":\"key\"")!=std::string::npos||request.find("\"cmd\":\"hold\"")!=std::string::npos){
            SendMessageW(windowHandle,WM_APP+1,0,0);
            if(mode==L"--drop-ack")Note("ACK_DROPPED");
            else if(mode==L"--server-error")Reply(pipe,"{\"ok\":false,\"error\":\"synthetic error after key application\"}");
            else {Reply(pipe,"{\"ok\":true}");if(mode==L"--hang-after-ack"){PostMessageW(windowHandle,WM_APP+2,0,0);WaitForSingleObject(hung,1000);}}
        }else Reply(pipe,"{\"ok\":false,\"error\":\"fixture implements status/key only\"}");
        DisconnectNamedPipe(pipe);CloseHandle(pipe);
    }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<3)return 2;
    logFile=_wfsopen(argv[1],L"w",_SH_DENYNO);mode=argv[2];LocalFree(argv);if(!logFile)return 3;hung=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_keytransport";RegisterClassW(&c);
    windowHandle=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"PANEL CLOSED",WS_OVERLAPPEDWINDOW,-28000,600,640,300,nullptr,nullptr,instance,nullptr);
    if(!windowHandle)return 4;ShowWindow(windowHandle,SW_SHOWNOACTIVATE);
    char windowId[40];sprintf_s(windowId,"hwnd=0x%llX",(unsigned long long)(ULONG_PTR)windowHandle);Note("WINDOW_READY",windowId);
    if(mode==L"--hang-preflight")PostMessageW(windowHandle,WM_APP+2,0,0);
    HANDLE thread=CreateThread(nullptr,0,Server,nullptr,0,nullptr);if(thread)CloseHandle(thread);
    for(;;){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)return 0;TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}
}
