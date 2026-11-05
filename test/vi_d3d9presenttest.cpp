// Actual D3D9/Ex rendering with a lower DLL guard refusing all exclusive calls.
// Only a 64x64 owned NOACTIVATE window is visible; all desktop writers absorb.
#include <vinput_core.cpp>
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++passed:++failed;}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
static BOOL WINAPI SamePlacement(HWND window,HWND,int,int,int width,int height,UINT flags){RECT r{};return window==g_hwnd&&(flags&SWP_NOACTIVATE)&&GetClientRect(window,&r)&&r.right==width&&r.bottom==height;}
static D3DPRESENT_PARAMETERS Params(HWND window,bool windowed){D3DPRESENT_PARAMETERS p{};p.Windowed=windowed;p.hDeviceWindow=window;p.BackBufferWidth=p.BackBufferHeight=64;p.BackBufferCount=1;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;p.FullScreen_RefreshRateInHz=windowed?0:60;return p;}
static bool Painted(IDirect3DDevice9* device,IDirect3DSwapChain9* chain){
    IDirect3DSurface9* back=nullptr;const auto got=chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back);
    const auto hr=SUCCEEDED(got)?device->ColorFill(back,nullptr,0xff1276b4):got;
    if(back)back->Release();return SUCCEEDED(hr);
}
static void PresentChecks(IDirect3DDevice9* device,IDirect3DDevice9Ex* extended,IDirect3DSwapChain9* chain,unsigned kind){
    Check(Painted(device,chain),"native target backbuffer accepts known-color draw");
    const auto before=C.present_ours.load();g_capReq=1;ResetEvent(g_capEvent);
    HRESULT hr=kind==0?device->Present(nullptr,nullptr,nullptr,nullptr):kind==1?chain->Present(nullptr,nullptr,nullptr,nullptr,0):extended->PresentEx(nullptr,nullptr,nullptr,nullptr,0);
    printf("native present kind=%u hr=0x%08lX\n",kind,(unsigned long)hr);
    Check(hr==D3D_OK,"actual windowed driver Present succeeds");
    Check(C.present_ours==before+1&&g_frameApi==9,"one successful owned Present counted, including nested paths");
    Check(g_capReq==0&&g_capW==64&&g_capH==64&&g_capPix.size()==64*64*4&&g_capPix[0]==0xb4&&g_capPix[1]==0x76&&g_capPix[2]==0x12&&g_capPix[3]==255,"native D3D9 capture returns exact drawn BGRA pixels without PrintWindow");
    g_capReq=0;
}
int main(int argc,char** argv){
    if(argc<3)return 2;const bool ex=strstr(argv[1],"ex")!=nullptr,late=strstr(argv[1],"late")!=nullptr;
    if(!strcmp(argv[2],"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify()||!vi_launch_ack::ArmNoActivation())return 111;
    g_pid=GetCurrentProcessId();g_t0=GetTickCount();g_capEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.lpszClassName=L"vi.d3d9.present.owned";
    if(!RegisterClassW(&cls))return 111;
    RECT work{};if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0))return 111;
    g_hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"D3D9 test",WS_POPUP|WS_VISIBLE,work.right-64,work.bottom-64,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    if(!g_hwnd)return 111;o_SetWindowPos=SamePlacement;
    wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);std::wstring folder=own;folder.resize(folder.find_last_of(L"\\/"));
    HMODULE guarded=LoadLibraryW((folder+L"\\d3d9.dll").c_str());if(!guarded)return 111;
    const auto create=(D9Factory)GetProcAddress(guarded,"Direct3DCreate9");const auto createEx=(D9FactoryEx)GetProcAddress(guarded,"Direct3DCreate9Ex");
    using Stats=void(WINAPI*)(LONG*);const auto stats=(Stats)GetProcAddress(guarded,"ViGuardStats");if(!stats)return 111;
    if(!late)TryHookD9(guarded);
    IDirect3D9* factory=nullptr;IDirect3D9Ex* factoryEx=nullptr;
    if(ex){const auto hr=createEx(D3D_SDK_VERSION,&factoryEx);if(SUCCEEDED(hr))factory=factoryEx;}else factory=create(D3D_SDK_VERSION);
    Check(factory!=nullptr,"actual guarded D3D9 factory created");if(!factory)return 2;
    auto pp=Params(g_hwnd,late);IDirect3DDevice9* device=nullptr;IDirect3DDevice9Ex* extended=nullptr;
    if(ex&&SUCCEEDED(factory->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,pp.BackBufferFormat,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr)))pp.MultiSampleType=D3DMULTISAMPLE_2_SAMPLES;
    D3DDISPLAYMODEEX mode{};mode.Size=sizeof(mode);mode.Width=mode.Height=64;mode.RefreshRate=60;mode.Format=D3DFMT_X8R8G8B8;mode.ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
    const auto hr=ex?factoryEx->CreateDeviceEx(0,D3DDEVTYPE_HAL,g_hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,late?nullptr:&mode,&extended):factory->CreateDevice(0,D3DDEVTYPE_HAL,g_hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);
    if(ex)device=extended;printf("native create api=%s late=%d hr=0x%08lX\n",ex?"ex":"9",late,(unsigned long)hr);
    Check(SUCCEEDED(hr)&&device,"fullscreen request reaches a presentable guarded windowed device");
    if(device){
        if(late){TryHookD9(guarded);
#ifdef VI_D3D9_CAPTURE
            const auto graceDeadline=GetTickCount64()+3500;
            do{TryD9LateHooks();if(!g_d9LateAttempted)Sleep(10);}while(!g_d9LateAttempted&&GetTickCount64()<graceDeadline);
#endif
        }
        IDirect3DSwapChain9* chain=nullptr;Check(SUCCEEDED(device->GetSwapChain(0,&chain))&&chain,"native swapchain is available");
        if(chain){D3DPRESENT_PARAMETERS actual{};chain->GetPresentParameters(&actual);Check(actual.Windowed&&actual.FullScreen_RefreshRateInHz==0,"actual driver parameters remain windowed with zero fullscreen refresh");
            PresentChecks(device,extended,chain,0);PresentChecks(device,extended,chain,1);if(ex)PresentChecks(device,extended,chain,2);chain->Release();}
        pp=Params(g_hwnd,false);const auto reset=ex?extended->ResetEx(&pp,&mode):device->Reset(&pp);
        Check(SUCCEEDED(reset),"fullscreen Reset converts to a windowed native Reset");
        chain=nullptr;const auto gotChain=device->GetSwapChain(0,&chain);Check(SUCCEEDED(gotChain)&&chain,"swapchain remains available after reset attempt");
        if(chain){PresentChecks(device,extended,chain,0);
            const HWND own=g_hwnd;g_hwnd=GetDesktopWindow();const auto frames=C.present_ours.load();g_capReq=1;
            const auto outside=chain->Present(nullptr,nullptr,nullptr,nullptr,0);
            Check(outside==D3D_OK&&C.present_ours==frames&&g_capReq==1,"a chain outside the selected window is neither counted nor captured");g_capReq=0;g_hwnd=own;
#ifdef VI_D3D9_CAPTURE
            const auto beforeLost=C.present_ours.load();g_capReq=1;
            const auto lost=D9PresentFrame(device,chain,nullptr,[]{return D3DERR_DEVICELOST;});
            Check(lost==D3DERR_DEVICELOST&&C.present_ours==beforeLost&&g_capReq==0&&g_capPix.empty()&&g_capW==0&&g_d9Lost>0,"controlled lost return preserves HRESULT, clears capture, and never invents a frame");
#else
            Check(false,"controlled lost return preserves HRESULT, clears capture, and never invents a frame");
#endif
            chain->Release();}
        pp=Params(g_hwnd,false);IDirect3DSwapChain9* additional=nullptr;const auto added=device->CreateAdditionalSwapChain(&pp,&additional);
        Check(SUCCEEDED(added)&&additional,"additional fullscreen request creates a windowed chain");
        if(!additional){pp=Params(g_hwnd,true);device->CreateAdditionalSwapChain(&pp,&additional);printf("windowed additional-chain control after rejected fullscreen request\n");}
        if(additional){PresentChecks(device,extended,additional,1);additional->Release();}
        device->Release();
    }
    factory->Release();LONG counters[3]{};stats(counters);
    Check(counters[0]==0&&counters[1]>0&&counters[2]==0,"lower native guard saw only windowed requests and no fullscreen mode");
    Check(vi_launch_terminal::Verify(),"terminal desktop writer guard retained throughout rendering");
    DestroyWindow(g_hwnd);g_hwnd=nullptr;CloseHandle(g_capEvent);g_capEvent=nullptr;
    printf("D3D9 Present: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
