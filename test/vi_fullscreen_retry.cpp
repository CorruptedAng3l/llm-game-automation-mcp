// UE-style retry-until-fullscreen-success control. No desktop input/activation/display changes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <share.h>
#include "vi_pattern.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* file=nullptr;static int action=0;static bool hookConfirmed=false;
static void Log(const char* format,...){va_list args;va_start(args,format);fprintf(file,"%llu ",GetTickCount64());vfprintf(file,format,args);fprintf(file,"\n");fflush(file);va_end(args);}
static LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+10){hookConfirmed=true;return 0;}if(m>=WM_APP+1&&m<=WM_APP+5){action=(int)(m-WM_APP);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
#define HR(expr) do{HRESULT r=(expr);if(FAILED(r)){Log("FAIL %s hr=%08X",#expr,(unsigned)r);return 3;}}while(0)
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(count<2)return 2;file=_wfsopen(args[1],L"w",_SH_DENYNO);if(!file)return 2;
 std::wstring mode=count>2?args[2]:L"visible";bool hidden=mode==L"hidden",plain=mode==L"plain";
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vinput_fullscreen_retry";RegisterClassW(&wc);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Fullscreen retry fixture",WS_OVERLAPPEDWINDOW,-28000,600,836,519,nullptr,nullptr,instance,nullptr);if(!window)return 2;if(!hidden)ShowWindow(window,SW_SHOWNOACTIVATE);
 // External harness validates installed hooks before this marker; pump while waiting
 // so status GetWindowText cannot deadlock an in-app synchronous pipe request.
 Log("HOOK_BARRIER_READY hwnd=0x%llX",(unsigned long long)(ULONG_PTR)window);ULONGLONG deadline=GetTickCount64()+20000;
 while(!hookConfirmed&&GetTickCount64()<deadline){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}
 if(!hookConfirmed){Log("FAIL no external hook confirmation; no TRUE call issued");return 4;}
 IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;IDXGIFactory* factory=nullptr;ID3D11Texture2D *back=nullptr,*source=nullptr;
 DXGI_SWAP_CHAIN_DESC description{};description.BufferCount=2;description.BufferDesc.Width=820;description.BufferDesc.Height=480;description.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.OutputWindow=window;description.SampleDesc.Count=1;description.Windowed=TRUE;description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 HR(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&description,&swap,&device,nullptr,&context));HR(swap->GetParent(IID_PPV_ARGS(&factory)));
 LONG_PTR original=GetWindowLongPtrW(window,GWL_STYLE);RECT originalRect{};GetWindowRect(window,&originalRect);bool originalVisible=IsWindowVisible(window)!=FALSE;LONG_PTR originalEx=GetWindowLongPtrW(window,GWL_EXSTYLE);
 Log("READY hwnd=0x%llX hidden=%d plain=%d",(unsigned long long)(ULONG_PTR)window,(int)hidden,(int)plain);
 bool needFullscreen=!plain,render=false;UINT frame=0,requests=0,successes=0,width=0,height=0;std::vector<BYTE> pixels;deadline=GetTickCount64()+90000;
 while(GetTickCount64()<deadline){
  MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)goto done;TranslateMessage(&message);DispatchMessageW(&message);}
  int command=action;action=0;
  if(command==1){RECT r{};GetWindowRect(window,&r);SetWindowPos(window,nullptr,0,0,r.right-r.left+40,r.bottom-r.top+30,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);BOOL fullscreen=FALSE;IDXGIOutput* output=nullptr;HRESULT hr=swap->GetFullscreenState(&fullscreen,&output);if(output)output->Release();Log("RESIZE_STATE hr=%08X fullscreen=%d output=%d",(unsigned)hr,(int)fullscreen,(int)(output!=nullptr));}
  if(command==2||command==5){HRESULT hr=swap->SetFullscreenState(FALSE,nullptr);BOOL fullscreen=TRUE;IDXGIOutput* output=nullptr;HRESULT query=swap->GetFullscreenState(&fullscreen,&output);if(output)output->Release();Log("FALSE_STATE hr=%08X query=%08X fullscreen=%d output=%d restored_frame=%d",(unsigned)hr,(unsigned)query,(int)fullscreen,(int)(output!=nullptr),(int)((GetWindowLongPtrW(window,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME))==(original&(WS_CAPTION|WS_THICKFRAME))));}
  if(command==3){if(source){source->Release();source=nullptr;}if(back){back->Release();back=nullptr;}swap->Release();swap=nullptr;HR(factory->CreateSwapChain(device,&description,&swap));BOOL fullscreen=TRUE;IDXGIOutput* output=nullptr;HRESULT query=swap->GetFullscreenState(&fullscreen,&output);if(output)output->Release();Log("RECREATED query=%08X fullscreen=%d output=%d",(unsigned)query,(int)fullscreen,(int)(output!=nullptr));width=height=0;needFullscreen=false;render=true;}
  if(command==4)needFullscreen=true;
  if(needFullscreen){
   HRESULT result=swap->SetFullscreenState(TRUE,nullptr);++requests;
   if(requests<=3||requests%60==0)Log("REQUEST count=%u hr=%08X",requests,(unsigned)result);
   if(result==S_OK){
    BOOL fullscreen=FALSE;IDXGIOutput* output=nullptr;HRESULT query=swap->GetFullscreenState(&fullscreen,&output);bool outputValid=output!=nullptr;if(output)output->Release();bool outputs=true;
    for(int i=0;i<100;i++){IDXGIOutput* ref=nullptr;HRESULT q=swap->GetFullscreenState(nullptr,&ref);if(FAILED(q)||!ref)outputs=false;if(ref)ref->Release();}
    DXGI_SWAP_CHAIN_DESC physical{};HR(swap->GetDesc(&physical));RECT after{};GetWindowRect(window,&after);
    Log("FULLSCREEN_OK sequence=%u query=%08X fullscreen=%d output=%d refs100=%d native_windowed=%d outer_same=%d visibility_same=%d safety_exstyle_same=%d borderless=%d ex_before=%llX ex_after=%llX",++successes,(unsigned)query,(int)fullscreen,(int)outputValid,(int)outputs,(int)physical.Windowed,(int)EqualRect(&originalRect,&after),(int)((IsWindowVisible(window)!=FALSE)==originalVisible),(int)((GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TOPMOST|WS_EX_NOACTIVATE))==(originalEx&(WS_EX_TOPMOST|WS_EX_NOACTIVATE))),(int)((GetWindowLongPtrW(window,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME))==0),(unsigned long long)originalEx,(unsigned long long)GetWindowLongPtrW(window,GWL_EXSTYLE));
    if(hidden){ShowWindow(window,SW_SHOWNOACTIVATE);hidden=false;}needFullscreen=false;render=true;
   }
  }
  if(render){
   RECT client{};GetClientRect(window,&client);UINT wantedWidth=(UINT)client.right,wantedHeight=(UINT)client.bottom;
   if(wantedWidth&&wantedHeight&&(wantedWidth!=width||wantedHeight!=height)){
    if(source){source->Release();source=nullptr;}if(back){back->Release();back=nullptr;}
    HR(swap->ResizeBuffers(0,wantedWidth,wantedHeight,DXGI_FORMAT_UNKNOWN,0));HR(swap->GetBuffer(0,IID_PPV_ARGS(&back)));D3D11_TEXTURE2D_DESC texture{};back->GetDesc(&texture);texture.BindFlags=0;texture.MiscFlags=0;texture.Usage=D3D11_USAGE_DEFAULT;texture.CPUAccessFlags=0;HR(device->CreateTexture2D(&texture,nullptr,&source));width=wantedWidth;height=wantedHeight;pixels.resize((size_t)width*height*4);
   }
   if(source&&back){++frame;for(UINT y=0;y<height;y++)vipat::Row(vipat::BGRA8,(int)y,(int)width,frame,pixels.data()+(size_t)y*width*4);context->UpdateSubresource(source,0,nullptr,pixels.data(),width*4,0);context->CopyResource(back,source);HR(swap->Present(0,0));if(frame%60==0)Log("FRAME %u",frame);}
  }
  Sleep(16);
 }
done:
 if(source)source->Release();if(back)back->Release();swap->SetFullscreenState(FALSE,nullptr);swap->Release();factory->Release();context->Release();device->Release();DestroyWindow(window);LocalFree(args);fclose(file);return 0;
}
