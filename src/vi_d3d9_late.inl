// Called from Init/monitor, never from the loader notification callback. A tiny
// hidden windowed device discovers shared implementations for late attachment.
std::atomic<bool> g_d9LateAttempted{false};
#define VI_D9_LATE_GRACE 1
std::atomic<ULONGLONG> g_d9LateArmedAt{0};
std::atomic<HRESULT> g_d9LateResult[2]{{E_PENDING},{E_PENDING}};
LRESULT CALLBACK D9ProbeWindow(HWND h,UINT message,WPARAM w,LPARAM l){return message==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,message,w,l);}
void TryD9LateHooks(){
    if(!g_d9Hooked||!g_d9FactoryHooksReady.load(std::memory_order_acquire)||g_d9PresentHooked||g_fullscreenPolicy.load()==1||g_d9Refused.load()>0||(g_d9ProbeAdmissions.load()&1u)||g_d9LateAttempted)return;
    const auto now=GetTickCount64();ULONGLONG unarmed=0;
    if(g_d9LateArmedAt.compare_exchange_strong(unarmed,now?now:1))return;
    if(now<unarmed||now-unarmed<2000||g_d9LateAttempted.exchange(true))return;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=D9ProbeWindow;cls.lpszClassName=L"vinput.d3d9.hidden.probe";
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    if(!window)return;
    const bool prior=g_d9Internal;g_d9Internal=true;
    for(unsigned api=0;api<2;++api){
        if(g_d9Refused.load()>0)break; // a native policy refusal is not permission for diagnostic creates
        IDirect3D9* factory=nullptr;IDirect3D9Ex* extended=nullptr;IDirect3DDevice9* device=nullptr;
        HRESULT result=E_NOINTERFACE;
        if(api==0){if(o_D9Factory){D9ProbeNativeAdmission admitted;if(admitted){if(D9Retain(0))factory=o_D9Factory(D3D_SDK_VERSION);else result=HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);}}}
        else if(o_D9FactoryEx){D9ProbeNativeAdmission admitted;if(admitted){if(!D9Retain(1))result=HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);else if(SUCCEEDED(o_D9FactoryEx(D3D_SDK_VERSION,&extended)))factory=extended;}}
        if(factory&&D9Interface(factory)&&g_d9Refused.load()==0){
            D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=1;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
            if(api==0){const auto native=D9Original<D9Create>(factory,16,D9_CREATE);D9ProbeNativeAdmission admitted;if(native&&admitted)result=native(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);}
            else{IDirect3DDevice9Ex* ex=nullptr;const auto native=D9Original<D9CreateEx>(extended,20,D9_CREATEEX);D9ProbeNativeAdmission admitted;if(native&&admitted)result=native(extended,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,nullptr,&ex);device=ex;}
            if(SUCCEEDED(result)&&device)D9Device(device);
        }
        g_d9LateResult[api]=result;if(device)device->Release();if(factory)factory->Release();
    }
    g_d9Internal=prior;DestroyWindow(window);
}
