// Render-sampled drag control: messages alone do not count. Only positions read through
// GetCursorPos while the virtual left button is down at a rendered frame are observations.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <cstdio>
#include <share.h>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"user32.lib")
static FILE* g_log;
static bool g_pause = false, g_pauseOnDown = false;
static void Log(const char* kind, int x, int y, unsigned frame) {
    fprintf(g_log,"%lu %s %d %d frame=%u\n",GetTickCount(),kind,x,y,frame); fflush(g_log);
}
static LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (m == WM_APP + 1) { g_pause = w != 0; return 0; }
    if (m == WM_LBUTTONDOWN) { Log("DOWN",(short)LOWORD(l),(short)HIWORD(l),0); if(g_pauseOnDown)g_pause=true; return 0; }
    if (m == WM_LBUTTONUP) { Log("UP",(short)LOWORD(l),(short)HIWORD(l),0); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    int argc=0; wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv || argc<2)return 2;
    g_log=_wfsopen(argv[1],L"w",_SH_DENYNO); if(!g_log)return 3;
    for(int i=2;i<argc;i++)if(!wcscmp(argv[i],L"--pause-on-down"))g_pauseOnDown=true;
    LocalFree(argv);
    WNDCLASSW c{};c.lpfnWndProc=WindowProc;c.hInstance=instance;c.lpszClassName=L"vi_dragtarget";
    RegisterClassW(&c);
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"vinput render sampled drag",WS_OVERLAPPEDWINDOW,-28000,600,640,360,nullptr,nullptr,instance,nullptr);
    if(!window)return 4;ShowWindow(window,SW_SHOWNOACTIVATE);
    DXGI_SWAP_CHAIN_DESC d{}; d.BufferCount=2;d.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.OutputWindow=window;d.SampleDesc.Count=1;d.Windowed=TRUE;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* swap=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&swap,&device,nullptr,&context);
    if(FAILED(hr))return 5;
    ID3D11Texture2D* back=nullptr;ID3D11RenderTargetView* view=nullptr;swap->GetBuffer(0,IID_PPV_ARGS(&back));
    if(!back)return 6;device->CreateRenderTargetView(back,nullptr,&view);back->Release();if(!view)return 7;
    unsigned frame=0;DWORD last=0; bool running=true;
    while(running) {
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){running=false;break;}TranslateMessage(&m);DispatchMessageW(&m);}
        DWORD now=GetTickCount();
        if(!g_pause && now-last>=80) {
            last=now;++frame;
            POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);
            if(GetAsyncKeyState(VK_LBUTTON)&0x8000)Log("FRAME_SAMPLE",point.x,point.y,frame);
            const float color[4]={0.12f,0.2f,0.3f,1};context->ClearRenderTargetView(view,color);swap->Present(0,0);
        }
        Sleep(2);
    }
    view->Release();context->Release();device->Release();swap->Release();fclose(g_log);return 0;
}
