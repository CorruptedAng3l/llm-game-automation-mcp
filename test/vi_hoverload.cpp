// Controlled slow Present cadence, plus an optional short CPU/GPU-copy neighbor.
// All HWNDs are offscreen and NOACTIVATE. This never injects OS input or foreground.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <thread>
#include <atomic>
#include <share.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output=nullptr;static unsigned frame=0,hoverFrame=0;static bool hovered=false,paused=false;
static DWORD frameMs=20;static ULONGLONG nextFrame=0;
static void Log(const char* format,...){fprintf(output,"%llu ",GetTickCount64());va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM w,LPARAM l){
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_APP+1){frameMs=(DWORD)w;nextFrame=GetTickCount64()+frameMs;Log("INTERVAL %lu",frameMs);return 0;}
    if(message==WM_APP+2){paused=w!=0;nextFrame=GetTickCount64()+frameMs;Log("PAUSED %d",(int)paused);return 0;}
    if(message==WM_MOUSEMOVE){hovered=true;hoverFrame=frame;Log("HOVER frame=%u point=%d,%d",frame,(short)LOWORD(l),(short)HIWORD(l));return 0;}
    if(message==WM_LBUTTONDOWN){POINT cursor{};GetCursorPos(&cursor);ScreenToClient(h,&cursor);unsigned seen=hovered?frame-hoverFrame:0;
        const bool accepted=hovered&&seen>=3&&cursor.x==(short)LOWORD(l)&&cursor.y==(short)HIWORD(l);
        Log("DOWN accepted=%d hover_frames=%u point=%d,%d cursor=%ld,%ld",(int)accepted,seen,(short)LOWORD(l),(short)HIWORD(l),cursor.x,cursor.y);return 0;}
    if(message==WM_LBUTTONUP){Log("UP");return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<2)return 2;
    output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(!output)return 3;
    DWORD neighborMs=0;for(int i=2;i<argc;i++)if(!wcscmp(argv[i],L"--neighbor-ms")&&i+1<argc)neighborMs=(DWORD)_wtoi(argv[++i]);LocalFree(argv);
    if(neighborMs>15000)return 4;
    WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"vi_hoverload";RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Slow Present hover control",WS_OVERLAPPEDWINDOW,-28000,600,640,360,nullptr,nullptr,instance,nullptr);if(!window)return 5;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);if(FAILED(hr))return 6;
    ID3D11Texture2D* back=nullptr;ID3D11RenderTargetView* view=nullptr;swap->GetBuffer(0,IID_PPV_ARGS(&back));if(!back)return 7;device->CreateRenderTargetView(back,nullptr,&view);back->Release();if(!view)return 8;
    Log("READY pid=%lu hwnd=0x%llX neighbor_ms=%lu",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)window,neighborMs);
    std::atomic<bool> busy{neighborMs!=0};std::atomic<unsigned long long> cpuOps{0};std::vector<std::thread> workers;
    ID3D11Texture2D *copyA=nullptr,*copyB=nullptr;ID3D11Query* done=nullptr;
    if(neighborMs){
        // Two CPU workers and 2x16MiB textures: bounded, deliberately below all-core load.
        for(unsigned n=0;n<2;n++)workers.emplace_back([&,n]{unsigned long long state=n+1,count=0;while(busy){for(unsigned k=0;k<16384;k++){state=state*6364136223846793005ULL+1;count+=(state>>32)&1;}SwitchToThread();}cpuOps.fetch_add(count);});
        D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=2048;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
        std::vector<unsigned> pixels(2048*2048,0x1f263dff);D3D11_SUBRESOURCE_DATA initial{pixels.data(),2048*4,0};
        hr=device->CreateTexture2D(&texture,&initial,&copyA);if(SUCCEEDED(hr))hr=device->CreateTexture2D(&texture,nullptr,&copyB);
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};if(SUCCEEDED(hr))hr=device->CreateQuery(&query,&done);
        if(FAILED(hr)){Log("NEIGHBOR_SETUP_FAILED hr=0x%08lX",(unsigned long)hr);busy=false;for(auto& worker:workers)worker.join();return 9;}
    }
    const ULONGLONG began=GetTickCount64();unsigned completedBatches=0;bool running=true;
    if(neighborMs)Log("NEIGHBOR_BEGIN duration_ms=%lu",neighborMs);
    while(running){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
        const ULONGLONG now=GetTickCount64();if(neighborMs&&now-began>=neighborMs)break;
        if(!paused&&now>=nextFrame){nextFrame=now+frameMs;++frame;const float color[4]={0.12f,0.2f,0.3f,1};context->ClearRenderTargetView(view,color);hr=swap->Present(0,0);Log("FRAME n=%u hr=0x%08lX",frame,(unsigned long)hr);}
        if(neighborMs){
            for(unsigned copy=0;copy<4;copy++){context->CopyResource(copyB,copyA);context->CopyResource(copyA,copyB);}context->End(done);context->Flush();
            const ULONGLONG wait=GetTickCount64()+100;HRESULT ready=S_FALSE;
            while((ready=context->GetData(done,nullptr,0,0))==S_FALSE&&GetTickCount64()<wait&&GetTickCount64()-began<neighborMs)Sleep(1);
            if(ready==S_OK)++completedBatches;
        }
        Sleep(neighborMs?2:1);
    }
    busy=false;for(auto& worker:workers)worker.join();
    if(neighborMs)Log("NEIGHBOR_DONE completed_batches=%u cpu_ops=%llu elapsed_ms=%llu",completedBatches,cpuOps.load(),GetTickCount64()-began);
    if(done)done->Release();if(copyB)copyB->Release();if(copyA)copyA->Release();view->Release();context->Release();device->Release();swap->Release();fclose(output);return 0;
}
