// Background-only host-style raw decoder; no physical input or foreground API calls.
// Link a separate ordinary MinHook instance into this EXE (not vinput's renamed instance).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <share.h>
#include "MinHook.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
using RawRead=UINT (WINAPI*)(HRAWINPUT,UINT,LPVOID,PUINT,UINT);
static RawRead originalRaw=nullptr;
static FILE* logfile=nullptr;
static HWND mainWindow=nullptr;
static WNDPROC beneathOverlay=nullptr;
static bool decoderInstalled=false,below=false,pumpA=false;
static unsigned accepted=0,received=0;
constexpr ULONG_PTR kHostMarker=0x49340001;
constexpr ULONG_PTR kHostPrefixMarker=0x56490ACE;
static std::wstring pressureResume;
static thread_local bool readingHostPrefix=false;
static void Log(const char* format,...) {
    va_list args;va_start(args,format);fprintf(logfile,"%llu ",GetTickCount64());
    vfprintf(logfile,format,args);fprintf(logfile,"\n");fflush(logfile);va_end(args);
}
static UINT WINAPI HostRaw(HRAWINPUT handle,UINT command,LPVOID data,PUINT bytes,UINT header) {
    if((ULONG_PTR)handle!=kHostMarker&&((ULONG_PTR)handle!=kHostPrefixMarker||!readingHostPrefix))return originalRaw(handle,command,data,bytes,header);
    if(!bytes || header!=sizeof(RAWINPUTHEADER) || (command!=RID_INPUT&&command!=RID_HEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;
    }
    RAWINPUT input{};input.header.dwType=RIM_TYPEMOUSE;input.header.dwSize=(DWORD)sizeof(input);
    input.header.wParam=RIM_INPUTSINK;input.data.mouse.lLastX=(ULONG_PTR)handle==kHostPrefixMarker?91:37;input.data.mouse.lLastY=(ULONG_PTR)handle==kHostPrefixMarker?-23:-19;
    const UINT need=(UINT)(command==RID_HEADER?sizeof(RAWINPUTHEADER):sizeof(input));
    if(!data){*bytes=need;Log("DECODER_QUERY marker=0x%llX bytes=%u",(unsigned long long)(ULONG_PTR)handle,need);return 0;}
    if(*bytes<need){*bytes=need;SetLastError(ERROR_INSUFFICIENT_BUFFER);return (UINT)-1;}
    memcpy(data,&input,need);*bytes=need;Log("DECODER_DATA marker=0x%llX bytes=%u",(unsigned long long)(ULONG_PTR)handle,need);return need;
}
static bool InstallDecoder() {
    if(decoderInstalled)return true;
    auto api=GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetRawInputData");
    MH_STATUS init=MH_Initialize();
    MH_STATUS created=(init==MH_OK||init==MH_ERROR_ALREADY_INITIALIZED)?MH_CreateHook((void*)api,(void*)&HostRaw,(void**)&originalRaw):init;
    MH_STATUS enabled=created==MH_OK?MH_EnableHook((void*)api):created;
    decoderInstalled=enabled==MH_OK;
    Log("DECODER_INSTALL order=%s result=%d",below?"below":"above",(int)enabled);return decoderInstalled;
}
static LRESULT ReadRaw(WPARAM w,LPARAM l) {
    ++received;Log("RAW_RECEIVED count=%u marker=0x%llX w=%llu",received,(unsigned long long)(ULONG_PTR)l,(unsigned long long)w);
    const bool previousPrefix=readingHostPrefix;readingHostPrefix=(ULONG_PTR)l==kHostPrefixMarker&&w==RIM_INPUTSINK;
    UINT bytes=0;SetLastError(0);UINT query=GetRawInputData((HRAWINPUT)l,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER));
    DWORD queryError=GetLastError();RAWINPUT input{};UINT capacity=(UINT)sizeof(input);
    SetLastError(0);UINT result=GetRawInputData((HRAWINPUT)l,RID_INPUT,&input,&capacity,sizeof(RAWINPUTHEADER));
    DWORD dataError=GetLastError();
    if((ULONG_PTR)l==kHostPrefixMarker && query==0 && result==sizeof(input) && input.data.mouse.lLastX==91 && input.data.mouse.lLastY==-23 && w==RIM_INPUTSINK) {
        Log("HOST_PREFIX_ACCEPT original_marker=1 original_wparam=1 dx=91 dy=-23");
    } else if(((ULONG_PTR)l&~(ULONG_PTR)0xFFF)==0x56490000 && query==0 && result!=(UINT)-1 && input.header.dwType==RIM_TYPEMOUSE) {
        Log("VINPUT_ACCEPT dx=%ld dy=%ld",input.data.mouse.lLastX,input.data.mouse.lLastY);
    } else if(query==0 && bytes==sizeof(input) && result==sizeof(input) && input.header.dwType==RIM_TYPEMOUSE &&
       input.data.mouse.lLastX==37 && input.data.mouse.lLastY==-19 && (ULONG_PTR)l==kHostMarker && w==RIM_INPUTSINK) {
        ++accepted;Log("RAW_ACCEPT count=%u original_marker=1 original_wparam=1 dx=37 dy=-19",accepted);
    } else Log("RAW_REFUSED query=%u query_error=%lu bytes=%u result=%u data_error=%lu",query,queryError,bytes,result,dataError);
    readingHostPrefix=previousPrefix;return 0;
}
static LRESULT CALLBACK Overlay(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_INPUT)return ReadRaw(w,l); // host consumes above vinput's subclass
    return CallWindowProcW(beneathOverlay,window,message,w,l);
}
static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_APP+1) {
        if(!below&&!InstallDecoder()){Log("ARM_FAILED");return 0;}
        beneathOverlay=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)Overlay);
        Log("ARMED decoder=%s overlay=outer",below?"below":"above");return 0;
    }
    if(message==WM_APP+2) {
        SetLastError(0);BOOL posted=w?PostMessageA(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker):
            PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker);
        Log("SELF_POST api=%s result=%d error=%lu",w?"A":"W",(int)posted,GetLastError());return 0;
    }
    if(message==WM_APP+3) {
        unsigned successes=0,failures=0;DWORD error=0;
        for(unsigned n=0;n<1025;++n){BOOL ok=PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker);if(ok)++successes;else{++failures;error=GetLastError();}}
        Log("CAPACITY successes=%u failures=%u error=%lu",successes,failures,error);return 0;
    }
    if(message==WM_APP+4) {
        unsigned filled=0;while(filled<20000&&PostMessageW(window,WM_NULL,0,0))++filled;
        SetLastError(0);BOOL ok=PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker);
        Log("QUEUE_FULL filled=%u raw_result=%d error=%lu",filled,(int)ok,GetLastError());return 0;
    }
    if(message==WM_APP+5) {
        BOOL ok=PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker);
        Log("EXPIRY_POST result=%d",(int)ok);Sleep(10200);Log("EXPIRY_RESUME");return 0;
    }
    if(message==WM_APP+6) {
        UINT bytes=0;SetLastError(0);UINT result=GetRawInputData((HRAWINPUT)kHostMarker,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER));
        Log("OUTSIDE_DISPATCH_READ result=%u error=%lu bytes=%u",result,GetLastError(),bytes);return 0;
    }
    if(message==WM_APP+7) {
        BOOL ok=PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostMarker);
        RemovePropW(window,L"vinput.self_raw.window_generation.v1");
        Log("GENERATION_INVALIDATED post_result=%d",(int)ok);return 0;
    }
    if(message==WM_APP+20) {
        DeleteFileW(pressureResume.c_str());Log("PRESSURE_PAUSED");
        const ULONGLONG until=GetTickCount64()+12000;
        while(GetTickCount64()<until&&GetFileAttributesW(pressureResume.c_str())==INVALID_FILE_ATTRIBUTES)Sleep(5);
        Log("PRESSURE_RESUMED");return 0;
    }
    if(message==WM_APP+21) {
        BOOL ok=PostMessageW(window,WM_INPUT,RIM_INPUTSINK,(LPARAM)kHostPrefixMarker);
        Log("SELF_PREFIX_POST result=%d",(int)ok);return 0;
    }
    if(message==WM_INPUT)return ReadRaw(w,l);
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(count<4)return 2;logfile=_wfsopen(args[1],L"w",_SH_DENYNO);if(!logfile)return 2;
    pressureResume=std::wstring(args[1])+L".pressure.resume";
    below=std::wstring(args[2])==L"below";pumpA=std::wstring(args[3])==L"a";
    if(below&&!InstallDecoder())return 3;
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vi_selfraw";
    RegisterClassW(&wc);mainWindow=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Self raw fixture",WS_OVERLAPPEDWINDOW,
        -28000,600,460,260,nullptr,nullptr,instance,nullptr);if(!mainWindow)return 4;
    ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
    RAWINPUTDEVICE mouse{1,2,0,mainWindow};BOOL registered=RegisterRawInputDevices(&mouse,1,sizeof(mouse));
    Log("REGISTER_MOUSE result=%d",(int)registered);
    Log("READY hwnd=0x%llX decoder=%s pump=%s",(unsigned long long)(ULONG_PTR)mainWindow,below?"below":"above",pumpA?"A":"W");
    const ULONGLONG deadline=GetTickCount64()+120000;
    while(GetTickCount64()<deadline) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT)goto done;
            if(message.message>=0xC000)Log("ENVELOPE message=%u ticket=0x%llX proof=0x%llX",message.message,
                (unsigned long long)message.wParam,(unsigned long long)(ULONG_PTR)message.lParam);
            TranslateMessage(&message);if(pumpA)DispatchMessageA(&message);else DispatchMessageW(&message);
        }
        Sleep(2);
    }
done:
    DestroyWindow(mainWindow);LocalFree(args);fclose(logfile);return 0;
}
