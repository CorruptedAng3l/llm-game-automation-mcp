#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "vi_r2_spies.h"
#include "vi_launch_ack.h"
#include "vi_d3d9_fixture_geometry.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"d3d11.lib")
struct GP { WORD buttons; BYTE lt,rt; SHORT lx,ly,rx,ry; };
struct State { DWORD packet; GP gp; };
using ReadPad=DWORD(WINAPI*)(DWORD,State*);
static FILE* output=nullptr;
static bool rendering=true, reset=true;
static unsigned token=0;
static std::atomic<unsigned long long> hardwareAbsorbed{0};
static DWORD WINAPI Disconnected(DWORD,State* state){++hardwareAbsorbed;if(state)*state={};return ERROR_DEVICE_NOT_CONNECTED;}
static void WINAPI NoKeyboard(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
static bool GuardsBeforeProduct(){for(const auto& hook:capture_spy::hooks)if(!capture_spy::Reaches(hook.entry,hook.detour))return false;return !capture_spy::hooks.empty();}
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_APP+1){token=(unsigned)w;reset=true;return 1;}
 if(m==WM_APP+2){rendering=w!=0;fprintf(output,"RENDER token=%u enabled=%d tick=%llu\n",token,rendering?1:0,GetTickCount64());fflush(output);return 1;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_ack::ArmDeadline())return 111;
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count<2)return 2;
 output=_wfsopen(args[1],L"w",_SH_DENYNO);if(!output)return 3;
 bool noRender=false;std::wstring eventName;
 for(int i=2;i<count;++i)if(!wcscmp(args[i],L"--no-render"))noRender=true;else if(!wcsncmp(args[i],L"--start-event=",14))eventName=args[i]+14;else return 111;
 LocalFree(args);rendering=!noRender;
 if(!capture_spy::Install()||!capture_spy::Add("keybd_event",(void*)&NoKeyboard)||!GuardsBeforeProduct()||!vi_launch_ack::ArmNoActivation()){fprintf(output,"PREREQUISITE guard_install error=%lu\n",GetLastError());fflush(output);return 111;}
 if(eventName.empty())return 111;HANDLE start=OpenEventW(SYNCHRONIZE,FALSE,eventName.c_str());if(!start){fprintf(output,"PREREQUISITE startup_event_open error=%lu\n",GetLastError());fflush(output);return 111;}const DWORD acknowledged=WaitForSingleObject(start,5000);CloseHandle(start);if(acknowledged!=WAIT_OBJECT_0){fprintf(output,"PREREQUISITE startup_ack wait=%lu\n",acknowledged);fflush(output);return 111;}
 const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 const RECT initial{-28000,800,-27680,1000};if(width<1||height<1||(long long)x+width>LONG_MAX||(long long)y+height>LONG_MAX||!vi_d9_fixture_geometry::Outside(initial,RECT{x,y,x+width,y+height}))return 111;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vi_pad_phaseconsumer";RegisterClassW(&wc);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"vi_pad_phaseconsumer",WS_POPUP|WS_VISIBLE,-28000,800,320,200,nullptr,nullptr,instance,nullptr);
 RECT actual{};if(!window||!GetWindowRect(window,&actual)||!vi_d9_fixture_geometry::Outside(actual,RECT{x,y,x+width,y+height})||!IsWindowVisible(window))return 4;
 static const int cookie=9;if(!SetPropW(window,L"ViPadPhaseLifetime.v1",(HANDLE)&cookie))return 111;
 HMODULE module=LoadLibraryW(L"xinput1_4.dll");auto read=module?(ReadPad)GetProcAddress(module,"XInputGetState"):nullptr;if(!read)return 5;
 void* unused=nullptr;if(MH_CreateHook((void*)read,(void*)&Disconnected,&unused)!=MH_OK||!unused||MH_EnableHook((void*)read)!=MH_OK||!capture_spy::Reaches((void*)read,(void*)&Disconnected))return 111;
 IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
 if(!noRender){DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);if(FAILED(result))return 6;}
 fprintf(output,"GUARDS desktop_writers=19 hardware_xinput_delegated=0 no_activation=1 offscreen=1 cookie=%llu\nREADY pid=%lu hwnd=%llu native=%d\n",(unsigned long long)(uintptr_t)&cookie,GetCurrentProcessId(),(unsigned long long)(uintptr_t)window,noRender?0:1);fflush(output);
 State previous{};DWORD previousCode=~0UL;bool first=true;unsigned frame=0,lastAxesFrame=0,violations=0,stage=0,selections=0;ULONGLONG next=GetTickCount64();
 for(;;){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){if(swap)swap->Release();if(context)context->Release();if(device)device->Release();fprintf(output,"GUARD_EXIT lower_hooks_kept=1 native_writers_delegated=0 hardware_xinput_delegated=0 hardware_absorbed=%llu\n",hardwareAbsorbed.load());fflush(output);return 0;}TranslateMessage(&message);DispatchMessageW(&message);}
  const auto now=GetTickCount64();if(rendering&&swap&&now>=next){++frame;swap->Present(0,0);next=now+16;}
  if(reset){reset=false;first=true;previous={};previousCode=~0UL;lastAxesFrame=frame;violations=stage=selections=0;fprintf(output,"RESET token=%u tick=%llu frame=%u\n",token,now,frame);fflush(output);}
  State current{};DWORD code=read(0,&current);if(code!=ERROR_SUCCESS)current={};
  if(first||code!=previousCode||memcmp(&current.gp,&previous.gp,sizeof(GP))){
   const auto& p=previous.gp;const auto& g=current.gp;
   const bool axes=first?g.lx||g.ly||g.rx||g.ry:g.lx!=p.lx||g.ly!=p.ly||g.rx!=p.rx||g.ry!=p.ry;
   if(axes)lastAxesFrame=frame;
   const bool press=(g.buttons&~p.buttons)||(g.lt&&!p.lt)||(g.rt&&!p.rt);
   if(press&&frame&&frame==lastAxesFrame)++violations;
   const bool aim=g.lx==5000&&!g.ly&&!g.rx&&!g.ry;
   if(aim&&!g.buttons&&!g.lt&&!g.rt){if(stage==2)stage=3;else if(stage==0)stage=1;}
   if(aim&&(g.buttons==0x1000||g.rt==255)&&stage==1)stage=2;
   if(!g.lx&&!g.ly&&!g.rx&&!g.ry&&!g.buttons&&!g.lt&&!g.rt&&stage==3){stage=4;++selections;}
   fprintf(output,"OBS token=%u tick=%llu frame=%u rc=%lu b=%u lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d axis_frame=%u violations=%u stage=%u selections=%u\n",token,now,frame,code,g.buttons,g.lt,g.rt,g.lx,g.ly,g.rx,g.ry,lastAxesFrame,violations,stage,selections);fflush(output);
   previous=current;previousCode=code;first=false;
  }
  Sleep(2);
 }
}
