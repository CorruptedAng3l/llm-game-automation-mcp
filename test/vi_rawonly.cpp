// Raw-only owned receiver. No physical raw handle is decoded and no desktop input is sent.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
typedef unsigned __int64 QWORD;
static FILE* file=nullptr;static HWND mainWindow=nullptr,alternateWindow=nullptr;static bool hooked=false;static std::wstring fixtureMode;
static void Log(const char* format,...){va_list args;va_start(args,format);fprintf(file,"%llu ",GetTickCount64());vfprintf(file,format,args);fprintf(file,"\n");fflush(file);va_end(args);}
static bool Magic(HRAWINPUT input){return ((ULONG_PTR)input&~(ULONG_PTR)0xFFF)==0x56490000;}
static void Describe(const RAWINPUT& input,const char* source,HWND window){if(input.header.dwType==RIM_TYPEMOUSE)Log("RAW_MOUSE source=%s receiver=%s dx=%ld dy=%ld buttons=%u wheel=%d",source,window==alternateWindow?"alternate":"main",input.data.mouse.lLastX,input.data.mouse.lLastY,input.data.mouse.usButtonFlags,(SHORT)input.data.mouse.usButtonData);}
static void Invalid(){UINT bytes=0;SetLastError(0);UINT result=GetRawInputData((HRAWINPUT)(ULONG_PTR)0x12345,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER));Log("INVALID_HANDLE result=%u error=%lu",result,GetLastError());}
static void Registration(HWND target,DWORD flags){RAWINPUTDEVICE device{1,2,flags,target};SetLastError(0);BOOL result=RegisterRawInputDevices(&device,1,sizeof(device));DWORD error=GetLastError();Log("REGISTER result=%d error=%lu flags=%lu target=0x%llX",(int)result,error,flags,(unsigned long long)(ULONG_PTR)target);UINT count=0;GetRegisteredRawInputDevices(nullptr,&count,sizeof(device));std::vector<RAWINPUTDEVICE> list(count);UINT written=count?GetRegisteredRawInputDevices(list.data(),&count,sizeof(device)):0;bool mouse=false;HWND actual=nullptr;if(written!=(UINT)-1)for(UINT i=0;i<written;i++)if(list[i].usUsagePage==1&&list[i].usUsage==2){mouse=true;actual=list[i].hwndTarget;}Log("LIVE_REG mouse=%d target=0x%llX",(int)mouse,(unsigned long long)(ULONG_PTR)actual);}
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+10&&!hooked){hooked=true;if(fixtureMode==L"late")Registration(mainWindow,0);if(fixtureMode==L"remove")Registration(nullptr,RIDEV_REMOVE);if(fixtureMode==L"failed")Registration(mainWindow,RIDEV_REMOVE);if(fixtureMode==L"buffer"){UINT bytes=0;SetLastError(0);UINT result=GetRawInputBuffer(nullptr,&bytes,sizeof(RAWINPUTHEADER));Log("BUFFER_PROBE result=%u error=%lu bytes=%u",result,GetLastError(),bytes);}Log("INJECTED_READY");return 0;}
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_MOUSEMOVE){Log("LEGACY_MOVE ignored=1");return 0;}
 if(m==WM_INPUT){
  if(!Magic((HRAWINPUT)l)||!hooked){Log("NON_SYNTHETIC_RAW_NOT_READ");return DefWindowProcW(h,m,w,l);}
  UINT bytes=0;UINT sized=GetRawInputData((HRAWINPUT)l,RID_INPUT,nullptr,&bytes,sizeof(RAWINPUTHEADER));if(sized!=0||bytes>4096||bytes<sizeof(RAWINPUTHEADER)){Log("RAW_ERROR size result=%u bytes=%u",sized,bytes);return 0;}
  std::vector<BYTE> storage(bytes);UINT result=GetRawInputData((HRAWINPUT)l,RID_INPUT,storage.data(),&bytes,sizeof(RAWINPUTHEADER));if(result==(UINT)-1)Log("RAW_ERROR data error=%lu",GetLastError());else Describe(*(RAWINPUT*)storage.data(),"post",h);return 0;
 }
 if(m==WM_APP+1){alignas(16) BYTE storage[8192+32];memset(storage,0xA5,sizeof(storage));UINT bytes=8192;SetLastError(0);UINT count=GetRawInputBuffer((RAWINPUT*)storage,&bytes,sizeof(RAWINPUTHEADER));DWORD error=GetLastError();bool canary=true;for(size_t i=8192;i<sizeof(storage);i++)if(storage[i]!=0xA5)canary=false;Log("BUFFER_DRAIN count=%u error=%lu returned_bytes=%u canary=%d caller_capacity=8192",count,error,bytes,(int)canary);if(count!=(UINT)-1){BYTE* at=storage;for(UINT i=0;i<count&&at+sizeof(RAWINPUTHEADER)<=storage+8192;i++){RAWINPUT* input=(RAWINPUT*)at;if(input->header.dwSize<sizeof(RAWINPUTHEADER)||at+input->header.dwSize>storage+8192)break;Describe(*input,"buffer",h);at=(BYTE*)NEXTRAWINPUTBLOCK(input);}}return 0;}
 if(m==WM_APP+3){UINT bytes=0;SetLastError(0);UINT count=GetRawInputBuffer(nullptr,&bytes,0);Log("BUFFER_BAD_HEADER count=%u error=%lu",count,GetLastError());return 0;}
 if(m==WM_APP+4){alignas(16) BYTE storage[80];memset(storage,0xA5,sizeof(storage));UINT bytes=sizeof(RAWINPUTHEADER);SetLastError(0);UINT count=GetRawInputBuffer((RAWINPUT*)storage,&bytes,sizeof(RAWINPUTHEADER));DWORD error=GetLastError();bool intact=true;for(BYTE value:storage)if(value!=0xA5)intact=false;Log("BUFFER_SMALL count=%u error=%lu needed=%u untouched=%d",count,error,bytes,(int)intact);return 0;}
 if(m==WM_APP+2){Invalid();return 0;}
 if(m==WM_DESTROY){if(h==mainWindow)PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(count<3)return 2;file=_wfsopen(args[1],L"w",_SH_DENYNO);if(!file)return 2;std::wstring mode=args[2];fixtureMode=mode;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vinput_raw_only";RegisterClassW(&wc);mainWindow=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Raw-only fixture",WS_OVERLAPPEDWINDOW,-28000,600,500,300,nullptr,nullptr,instance,nullptr);alternateWindow=CreateWindowExW(0,wc.lpszClassName,L"Raw sink",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
 if(mode==L"pre"||mode==L"remove"||mode==L"buffer"||mode==L"alternate")Registration(mode==L"alternate"?alternateWindow:mainWindow,0);
 Invalid();Log("READY main=0x%llX alternate=0x%llX",(unsigned long long)(ULONG_PTR)mainWindow,(unsigned long long)(ULONG_PTR)alternateWindow);
 ULONGLONG deadline=GetTickCount64()+90000;
 while(GetTickCount64()<deadline){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)goto done;TranslateMessage(&message);DispatchMessageW(&message);}
  Sleep(2);
 }
done:
 Registration(nullptr,RIDEV_REMOVE);DestroyWindow(alternateWindow);DestroyWindow(mainWindow);LocalFree(args);fclose(file);return 0;}
