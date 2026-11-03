// A slow DirectInput-only consumer; posted button messages are deliberately ignored.
// Hover establishes the next 300ms poll deadline so a 60ms press deterministically misses.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
static FILE* output;static bool polling=true,buffered=false,keyboardOnly=false,wmOnly=false,shortState=false;static unsigned remaining=0xffffffff,clicks=0,oneCapacity=1;static ULONGLONG nextPoll=0;
static void Log(const char* format,...){fprintf(output,"%llu ",GetTickCount64());va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_MOUSEMOVE){nextPoll=GetTickCount64()+300;Log("HOVER next_poll=%llu",nextPoll);return 0;}
 if(m==WM_LBUTTONDOWN){if(wmOnly){++clicks;Log("WM_ACCEPT clicks=%u",clicks);}else Log("WM_IGNORED");return 0;}
 if(m==WM_APP+1){polling=w!=0;remaining=0xffffffff;nextPoll=GetTickCount64()+300;Log("POLLING %d",(int)polling);return 0;}
 if(m==WM_APP+2){polling=true;remaining=1;oneCapacity=w>=1&&w<=16?(unsigned)w:1;nextPoll=GetTickCount64()+300;Log("ONE_POLL_ARMED");return 0;}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,(HBRUSH)(COLOR_WINDOW+1));EndPaint(h,&ps);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 (void)l;return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(!output)return 3;
 for(int i=2;i<argc;i++){if(!wcscmp(argv[i],L"--stopped"))polling=false;if(!wcscmp(argv[i],L"--buffered"))buffered=true;if(!wcscmp(argv[i],L"--keyboard-only"))keyboardOnly=true;if(!wcscmp(argv[i],L"--wm-only"))wmOnly=true;if(!wcscmp(argv[i],L"--short-state"))shortState=true;}LocalFree(argv);
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"vi_dipollclick";RegisterClassW(&cls);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Slow DI click observer",WS_OVERLAPPEDWINDOW,-28000,600,480,300,nullptr,nullptr,instance,nullptr);if(!window)return 4;ShowWindow(window,SW_SHOWNOACTIVATE);
 IDirectInput8W* di=nullptr;IDirectInputDevice8W* device=nullptr;
 if(!wmOnly){if(FAILED(DirectInput8Create(instance,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&di,nullptr))||FAILED(di->CreateDevice(keyboardOnly?GUID_SysKeyboard:GUID_SysMouse,&device,nullptr)))return 5;
  if(FAILED(device->SetDataFormat(keyboardOnly?&c_dfDIKeyboard:&c_dfDIMouse2)))return 6;device->SetCooperativeLevel(window,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);
  if(buffered){DIPROPDWORD property{};property.diph.dwSize=sizeof(property);property.diph.dwHeaderSize=sizeof(property.diph);property.diph.dwHow=DIPH_DEVICE;property.dwData=128;device->SetProperty(DIPROP_BUFFERSIZE,&property.diph);}device->Acquire();}
 Log("READY pid=%lu hwnd=0x%llX buffered=%d keyboard=%d wm_only=%d",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)window,(int)buffered,(int)keyboardOnly,(int)wmOnly);
 nextPoll=GetTickCount64()+300;bool running=true,wasDown=false;
 while(running){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
  if(device&&polling&&remaining&&GetTickCount64()>=nextPoll){nextPoll=GetTickCount64()+300;if(remaining!=0xffffffff)--remaining;
   if(buffered){DIDEVICEOBJECTDATA data[16]{};DWORD count=remaining==0?oneCapacity:16;HRESULT hr=device->GetDeviceData(sizeof(data[0]),data,&count,0);Log("BUFFER_POLL hr=0x%08lX count=%lu",(unsigned long)hr,count);
    if(SUCCEEDED(hr))for(DWORD i=0;i<count;i++)if(data[i].dwOfs>=DIMOFS_BUTTON0&&data[i].dwOfs<=DIMOFS_BUTTON4){Log("BUFFER_BUTTON button=%lu down=%d sequence=%lu",data[i].dwOfs-DIMOFS_BUTTON0,(data[i].dwData&0x80)!=0,data[i].dwSequence);if(data[i].dwOfs==DIMOFS_BUTTON0&&(data[i].dwData&0x80)){++clicks;Log("CONSUMED_DOWN clicks=%u",clicks);}}}
   else if(keyboardOnly){BYTE keys[256]{};HRESULT hr=device->GetDeviceState(sizeof(keys),keys);Log("KEYBOARD_POLL hr=0x%08lX",(unsigned long)hr);}
   else{DIMOUSESTATE2 state{};HRESULT hr=device->GetDeviceState(shortState?sizeof(DIMOUSESTATE):sizeof(state),&state);const bool down=SUCCEEDED(hr)&&(state.rgbButtons[0]&0x80)!=0;Log("STATE_POLL hr=0x%08lX left=%d",(unsigned long)hr,(int)down);if(down&&!wasDown){++clicks;Log("CONSUMED_DOWN clicks=%u",clicks);}wasDown=down;}
  }Sleep(1);
 }
 if(device)device->Release();if(di)di->Release();fclose(output);return 0;
}
