// Owned meters for returned RawInput and DirectInput deltas, with explicit drains.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <shellapi.h>
#include <cstdio>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
typedef unsigned __int64 QWORD;
static FILE* output;static IDirectInputDevice8W* mouse;
static long long rawX=0,rawY=0,stateX=0,stateY=0,bufferX=0,bufferY=0;
static bool Magic(HRAWINPUT h){return ((ULONG_PTR)h&~(ULONG_PTR)0xFFF)==0x56490000;}
static void ReadRaw(const RAWINPUT& r){if(r.header.dwType==RIM_TYPEMOUSE){rawX+=r.data.mouse.lLastX;rawY+=r.data.mouse.lLastY;}}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_INPUT){if(Magic((HRAWINPUT)l)){RAWINPUT r{};UINT size=sizeof(r);if(GetRawInputData((HRAWINPUT)l,RID_INPUT,&r,&size,sizeof(RAWINPUTHEADER))!=(UINT)-1)ReadRaw(r);return 0;}return DefWindowProcW(h,m,w,l);}
 if(m==WM_APP+1){mouse->Unacquire();DIMOUSESTATE state{};HRESULT hr=mouse->GetDeviceState(sizeof(state),&state);if(SUCCEEDED(hr)){stateX+=state.lX;stateY+=state.lY;}fprintf(output,"STATE hr=%08lX dx=%ld dy=%ld total_x=%lld total_y=%lld\n",(unsigned long)hr,state.lX,state.lY,stateX,stateY);}
 else if(m==WM_APP+2){POINT point{(LONG)w,(LONG)l};ClientToScreen(h,&point);SetCursorPos(point.x,point.y);fprintf(output,"CURSOR_SET\n");}
 else if(m==WM_APP+3){for(unsigned turn=0;turn<16;turn++){alignas(16) BYTE bytes[8192]{};UINT capacity=sizeof(bytes);UINT count=GetRawInputBuffer((RAWINPUT*)bytes,&capacity,sizeof(RAWINPUTHEADER));if(count==(UINT)-1||!count)break;BYTE* at=bytes;for(UINT i=0;i<count&&at+sizeof(RAWINPUTHEADER)<=bytes+sizeof(bytes);i++){auto* r=(RAWINPUT*)at;if(r->header.dwSize<sizeof(RAWINPUTHEADER)||at+r->header.dwSize>bytes+sizeof(bytes))break;ReadRaw(*r);at=(BYTE*)NEXTRAWINPUTBLOCK(r);}}fprintf(output,"RAW_BUFFER total_x=%lld total_y=%lld\n",rawX,rawY);}
 else if(m==WM_APP+4){mouse->Unacquire();for(unsigned turn=0;turn<16;turn++){DIDEVICEOBJECTDATA entries[64]{};DWORD count=64;HRESULT hr=mouse->GetDeviceData(sizeof(entries[0]),entries,&count,0);if(FAILED(hr)||!count)break;for(DWORD i=0;i<count&&i<64;i++){if(entries[i].dwOfs==DIMOFS_X)bufferX+=(LONG)entries[i].dwData;if(entries[i].dwOfs==DIMOFS_Y)bufferY+=(LONG)entries[i].dwData;}}fprintf(output,"DI_BUFFER total_x=%lld total_y=%lld\n",bufferX,bufferY);}
 else if(m==WM_APP+5){rawX=rawY=stateX=stateY=bufferX=bufferY=0;fprintf(output,"RESET\n");}
 else if(m==WM_APP+6){fprintf(output,"RAW_POST total_x=%lld total_y=%lld\n",rawX,rawY);}
 else if(m==WM_DESTROY){PostQuitMessage(0);return 0;}else return DefWindowProcW(h,m,w,l);
 fflush(output);return 0;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_relativeaccount";RegisterClassW(&c);HWND h=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"relative meters",WS_OVERLAPPEDWINDOW,-28000,600,360,280,nullptr,nullptr,instance,nullptr);ShowWindow(h,SW_SHOWNOACTIVATE);
 IDirectInput8W* di=nullptr;if(FAILED(DirectInput8Create(instance,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&di,nullptr))||FAILED(di->CreateDevice(GUID_SysMouse,&mouse,nullptr)))return 4;
 mouse->SetDataFormat(&c_dfDIMouse);mouse->SetCooperativeLevel(h,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);DIPROPDWORD property{};property.diph.dwSize=sizeof(property);property.diph.dwHeaderSize=sizeof(property.diph);property.diph.dwHow=DIPH_DEVICE;property.dwData=128;mouse->SetProperty(DIPROP_BUFFERSIZE,&property.diph);
 RAWINPUTDEVICE registration{1,2,0,h};RegisterRawInputDevices(&registration,1,sizeof(registration));fprintf(output,"READY\n");fflush(output);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}mouse->Release();di->Release();fclose(output);return 0;
}
