// Background-only legacy DirectInput ABI/focus control. Never activates or uses exclusive input.
#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <share.h>
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* logFile=nullptr;
static void Log(const char* format,...){va_list args;va_start(args,format);fprintf(logFile,"%llu ",GetTickCount64());vfprintf(logFile,format,args);fprintf(logFile,"\n");fflush(logFile);va_end(args);}
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
static bool Zero(const void* p,size_t n){auto bytes=(const unsigned char*)p;for(size_t i=0;i<n;i++)if(bytes[i])return false;return true;}
template<class Device> int Run(Device* keyboard,Device* mouse,HWND window,bool buffered,DWORD stride){
 HRESULT kdf=keyboard->SetDataFormat(&c_dfDIKeyboard),mdf=mouse->SetDataFormat(&c_dfDIMouse);
 HRESULT kc=keyboard->SetCooperativeLevel(window,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE),mc=mouse->SetCooperativeLevel(window,DISCL_FOREGROUND|DISCL_NONEXCLUSIVE);
 DIPROPDWORD p={{sizeof(p),sizeof(DIPROPHEADER),0,DIPH_DEVICE},64};HRESULT kp=keyboard->SetProperty(DIPROP_BUFFERSIZE,&p.diph),mp=mouse->SetProperty(DIPROP_BUFFERSIZE,&p.diph);
 Log("SETUP kdf=%08X mdf=%08X kc=%08X mc=%08X kp=%08X mp=%08X",(unsigned)kdf,(unsigned)mdf,(unsigned)kc,(unsigned)mc,(unsigned)kp,(unsigned)mp);
 if(FAILED(kdf)||FAILED(mdf)||FAILED(kc)||FAILED(mc)||FAILED(kp)||FAILED(mp))return 5;
 void** kv=*(void***)keyboard;void** mv=*(void***)mouse;
 Log("VTABLE k_acq=%p k_state=%p k_data=%p m_acq=%p m_state=%p m_data=%p",kv[7],kv[9],kv[10],mv[7],mv[9],mv[10]);
 Log("READY forced_unacquired_state=1 buffered=%d stride=%lu real_foreground_before_inject=%d",(int)buffered,stride,(int)(GetForegroundWindow()==window));
 BYTE previous[256]={};BYTE button=0;ULONGLONG next=0,deadline=GetTickCount64()+90000;
 while(GetTickCount64()<deadline){
  MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)return 0;TranslateMessage(&m);DispatchMessageW(&m);}
  keyboard->Unacquire();mouse->Unacquire();
  HRESULT ka=keyboard->Acquire(),ma=mouse->Acquire();
  // Force the native state/data failure even if fake Win32 focus lets native Acquire succeed.
  // Some native legacy implementations still expose physical state here. The uninjected runner witnesses/retries contaminated windows; injected isolation is tested separately.
  keyboard->Unacquire();mouse->Unacquire();BYTE keys[256];DIMOUSESTATE state;memset(keys,0xA5,sizeof(keys));memset(&state,0xA5,sizeof(state));
  HRESULT ks=keyboard->GetDeviceState(sizeof(keys),keys),ms=mouse->GetDeviceState(sizeof(state),&state);
  if(SUCCEEDED(ks)&&!buffered){for(int i=0;i<256;i++)if((keys[i]&0x80)!=(previous[i]&0x80))Log("KEY dik=0x%02X %s",i,(keys[i]&0x80)?"down":"up");memcpy(previous,keys,sizeof(keys));}
  if(SUCCEEDED(ms)&&!buffered){BYTE b=state.rgbButtons[0]&0x80;if(b!=button)Log("MOUSE b0 %s",b?"down":"up");button=b;if(state.lX||state.lY||state.lZ)Log("DELTA x=%ld y=%ld z=%ld",state.lX,state.lY,state.lZ);}
  HRESULT kd=DI_OK,md=DI_OK;bool guards=true,tail=true;DWORD kn=0,mn=0;
  if(buffered){for(int device=0;device<2;device++){
   std::vector<BYTE> bytes((size_t)stride*32+32,0xA5);DWORD count=32;
   HRESULT result=(device?mouse:keyboard)->GetDeviceData(stride,(DIDEVICEOBJECTDATA*)bytes.data(),&count,0);
   for(size_t i=(size_t)stride*32;i<bytes.size();i++)if(bytes[i]!=0xA5)guards=false;
   if(SUCCEEDED(result)&&count<=32){for(DWORD i=0;i<count;i++){DWORD fields[4]{};memcpy(fields,bytes.data()+(size_t)i*stride,sizeof(fields));for(DWORD j=16;j<stride;j++)if(bytes[(size_t)i*stride+j])tail=false;if(!device)Log("KEY dik=0x%02X %s",fields[0],(fields[1]&0x80)?"down":"up");else if(fields[0]==DIMOFS_BUTTON0)Log("MOUSE b0 %s",(fields[1]&0x80)?"down":"up");else Log("BUFFER_MOUSE offset=%lu data=%ld",fields[0],(LONG)fields[1]);}}
   else if(count>32)guards=false;
   if(device){md=result;mn=count;}else{kd=result;kn=count;}
  }}
  ULONGLONG now=GetTickCount64();if(now>=next){next=now+100;Log("STATUS k_acq=%08X m_acq=%08X k_state=%08X m_state=%08X zero_k=%d zero_m=%d k_data=%08X m_data=%08X guard=%d tail=%d kn=%lu mn=%lu",(unsigned)ka,(unsigned)ma,(unsigned)ks,(unsigned)ms,(int)(SUCCEEDED(ks)&&Zero(keys,sizeof(keys))),(int)(SUCCEEDED(ms)&&Zero(&state,sizeof(state))),(unsigned)kd,(unsigned)md,(int)guards,(int)tail,kn,mn);}
  if(!guards||!tail){Log("FAIL ABI canary/tail");return 6;}Sleep(2);
 }
 return 0;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(count<3)return 2;
 logFile=_wfsopen(args[1],L"w",_SH_DENYNO);if(!logFile)return 3;
 bool modern=args[2][0]==L'8';
 bool wide=wcsstr(args[2],L"w")!=nullptr,extended=wcsstr(args[2],L"ex")!=nullptr,buffered=count>3&&wcscmp(args[3],L"buffered")==0;
 DWORD stride=count>4?(DWORD)_wtoi(args[4]):16;if(stride!=16&&stride!=20&&stride!=24)return 4;
 WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=instance;wc.lpszClassName=L"vinput_legacy_focus";RegisterClassW(&wc);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Legacy DI background control",WS_OVERLAPPEDWINDOW,-28000,600,500,300,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);
 HMODULE library=LoadLibraryW(modern?L"dinput8.dll":L"dinput.dll");if(!library)return 7;int result=8;
 if(modern){using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);auto create=(Create)GetProcAddress(library,"DirectInput8Create");if(!create)return 9;
  if(wide){IDirectInput8W* di=nullptr;IDirectInputDevice8W *k=nullptr,*m=nullptr;if(SUCCEEDED(create(instance,0x0800,IID_IDirectInput8W,(void**)&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();}
  else{IDirectInput8A* di=nullptr;IDirectInputDevice8A *k=nullptr,*m=nullptr;if(SUCCEEDED(create(instance,0x0800,IID_IDirectInput8A,(void**)&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();}
 }else if(extended){using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);auto create=(Create)GetProcAddress(library,"DirectInputCreateEx");if(!create)return 9;
  if(wide){IDirectInput7W* di=nullptr;IDirectInputDeviceW *k=nullptr,*m=nullptr;if(SUCCEEDED(create(instance,0x0700,IID_IDirectInput7W,(void**)&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();}
  else{IDirectInput7A* di=nullptr;IDirectInputDeviceA *k=nullptr,*m=nullptr;if(SUCCEEDED(create(instance,0x0700,IID_IDirectInput7A,(void**)&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();}
 }else if(wide){using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,LPDIRECTINPUTW*,LPUNKNOWN);auto create=(Create)GetProcAddress(library,"DirectInputCreateW");IDirectInputW* di=nullptr;IDirectInputDeviceW *k=nullptr,*m=nullptr;if(create&&SUCCEEDED(create(instance,0x0300,&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();
 }else{using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,LPDIRECTINPUTA*,LPUNKNOWN);auto create=(Create)GetProcAddress(library,"DirectInputCreateA");IDirectInputA* di=nullptr;IDirectInputDeviceA *k=nullptr,*m=nullptr;if(create&&SUCCEEDED(create(instance,0x0300,&di,nullptr))&&di&&SUCCEEDED(di->CreateDevice(GUID_SysKeyboard,&k,nullptr))&&SUCCEEDED(di->CreateDevice(GUID_SysMouse,&m,nullptr))){result=Run(k,m,window,buffered,stride);k->Release();m->Release();}if(di)di->Release();}
 Log("EXIT %d",result);DestroyWindow(window);FreeLibrary(library);LocalFree(args);fclose(logFile);return result;
}
