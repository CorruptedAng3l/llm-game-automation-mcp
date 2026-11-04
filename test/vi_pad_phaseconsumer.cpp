#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"d3d11.lib")
struct GP { WORD buttons; BYTE lt,rt; SHORT lx,ly,rx,ry; };
struct State { DWORD packet; GP gp; };
using ReadPad=DWORD(WINAPI*)(DWORD,State*);
static FILE* output=nullptr;
static bool rendering=true, reset=true;
static unsigned token=0;
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+1){token=(unsigned)w;reset=true;return 1;}
 if(m==WM_APP+2){rendering=w!=0;fprintf(output,"RENDER token=%u enabled=%d tick=%llu\n",token,rendering?1:0,GetTickCount64());fflush(output);return 1;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count<2)return 2;
 output=_wfsopen(args[1],L"w",_SH_DENYNO);if(!output)return 3;
 const bool noRender=count>2&&!wcscmp(args[2],L"--no-render");rendering=!noRender;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vi_pad_phaseconsumer";RegisterClassW(&wc);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"vi_pad_phaseconsumer",WS_OVERLAPPEDWINDOW,-28000,800,320,200,nullptr,nullptr,instance,nullptr);
 if(!window)return 4;ShowWindow(window,SW_SHOWNOACTIVATE);
 HMODULE module=LoadLibraryW(L"xinput1_4.dll");auto read=module?(ReadPad)GetProcAddress(module,"XInputGetState"):nullptr;if(!read)return 5;
 IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
 if(!noRender){DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);if(FAILED(result))return 6;}
 fprintf(output,"READY pid=%lu hwnd=%llu native=%d\n",GetCurrentProcessId(),(unsigned long long)(uintptr_t)window,noRender?0:1);fflush(output);
 State previous{};DWORD previousCode=~0UL;bool first=true;unsigned frame=0,lastAxesFrame=0,violations=0,stage=0,selections=0;ULONGLONG next=GetTickCount64();
 for(;;){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){if(swap)swap->Release();if(context)context->Release();if(device)device->Release();return 0;}TranslateMessage(&message);DispatchMessageW(&message);}
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
