// Virtual DXGI fullscreen: retain native windowed presentation and owned window placement.
// Included after PFN_SetFS/o_SetFullscreenState, before capture state. No display-mode API.
using PFN_GetFS = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL*, IDXGIOutput**);
PFN_GetFS o_GetFullscreenState = nullptr;
std::atomic<int> g_fullscreenPolicy{0}; // nofullscreen=true: 0 borderless emulation, 1 explicit refusal
std::atomic<long> g_fsRequests{0}, g_fsEmulated{0}, g_fsRefused{0}, g_fsFailed{0}, g_fsPassthrough{0}, g_fsStates{0}, g_fsOutputs{0};
std::atomic<HRESULT> g_fsLastResult{S_OK};
std::atomic<HWND> g_fsLastWindow{nullptr};
std::atomic<bool> g_fsBlocked{false};
// Setters never block a reentrant window callback on this lock. Getters only read
// ref-counted private data; no lock is held while waiting for another thread.
std::mutex g_fsChangeMx;
const wchar_t* kFullscreenOwnerProp=L"vinput.fullscreen.owner.v1";
const GUID kFullscreenData = {0x7b529b31,0xc40f,0x4bf3,{0x9d,0x6b,0xf4,0x81,0x30,0x22,0x71,0x94}};
struct VirtualFullscreen final : IUnknown {
    std::atomic<ULONG> refs{1}; std::atomic<bool> active{false};
    HWND window; DWORD pid; LONG_PTR originalStyle, appliedStyle; IDXGIOutput* output;
    VirtualFullscreen(HWND w,LONG_PTR original,LONG_PTR applied,IDXGIOutput* o):window(w),pid(g_pid),originalStyle(original),appliedStyle(applied),output(o){if(output){output->AddRef();g_fsOutputs++;}g_fsStates++;}
    ~VirtualFullscreen(){if(output){output->Release();g_fsOutputs--;}g_fsStates--;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** result) override {if(!result)return E_POINTER;*result=nullptr;if(id==IID_IUnknown){*result=static_cast<IUnknown*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG n=--refs;if(!n)delete this;return n;}
};
VirtualFullscreen* FullscreenData(IDXGISwapChain* sc){
    VirtualFullscreen* data=nullptr;UINT size=sizeof(data);
    if(FAILED(sc->GetPrivateData(kFullscreenData,&size,&data))||size!=sizeof(data))return nullptr;
    return data; // GetPrivateData AddRefs interfaces stored by SetPrivateDataInterface.
}
bool FullscreenOwnWindow(HWND window){DWORD pid=0;return window&&IsWindow(window)&&GetWindowThreadProcessId(window,&pid)&&pid==g_pid;}
HRESULT FullscreenStyle(HWND window,LONG_PTR style){
    SetLastError(0);LONG_PTR previous=SetWindowLongPtrW(window,GWL_STYLE,style);DWORD error=GetLastError();
    if(!previous&&error)return HRESULT_FROM_WIN32(error);
    if(!(o_SetWindowPos?o_SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED):SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)))return HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_GEN_FAILURE);
    return S_OK;
}
HRESULT FullscreenRestore(IDXGISwapChain* sc,VirtualFullscreen* data){
    data->active=false;HRESULT cleared=sc->SetPrivateDataInterface(kFullscreenData,nullptr);if(FAILED(cleared))return cleared;
    // Never overwrite a newer application style or a destroyed/reused foreign HWND.
    if(FullscreenOwnWindow(data->window)&&GetPropW(data->window,kFullscreenOwnerProp)==data){RemovePropW(data->window,kFullscreenOwnerProp);LONG_PTR current=GetWindowLongPtrW(data->window,GWL_STYLE),mask=WS_CAPTION|WS_THICKFRAME;if((current&mask)==(data->appliedStyle&mask)){LONG_PTR restored=(current&~mask)|(data->originalStyle&mask);if(restored!=current)return FullscreenStyle(data->window,restored);}}
    return S_OK;
}
HRESULT FullscreenFailure(HWND window,HRESULT result,bool refused){
    g_fsLastWindow=window;g_fsLastResult=result;g_fsBlocked=true;
    long n=refused?++g_fsRefused:++g_fsFailed;
    // a game that retries every frame would flood the log (and the 200-line ring): log the first and every 256th
    if(n==1||n%256==0)Log("DXGI fullscreen request %s (%ld so far, hr=0x%08lX); set the game to windowed or borderless in its own settings",refused?"refused":"failed",n,(unsigned long)result);
    return result;
}
bool FullscreenBlockedBeforePresent(){
    HWND window=g_fsLastWindow.load();
    return g_fsBlocked&&g_fsRefused.load()+g_fsFailed.load()>=3&&C.present_ours==0&&window&&g_hwnd&&(window==g_hwnd||IsChild(g_hwnd,window)||IsChild(window,g_hwnd));
}
HRESULT STDMETHODCALLTYPE h_GetFullscreenState(IDXGISwapChain* sc,BOOL* fullscreen,IDXGIOutput** output){
    VirtualFullscreen* data=FullscreenData(sc);
    if(data){
        if(!g_cfg.nofullscreen){std::unique_lock<std::mutex> change(g_fsChangeMx,std::try_to_lock);if(change.owns_lock())FullscreenRestore(sc,data);}
        else if(data->active&&FullscreenOwnWindow(data->window)&&GetPropW(data->window,kFullscreenOwnerProp)==data){if(fullscreen)*fullscreen=TRUE;if(output){*output=data->output;if(*output)(*output)->AddRef();}data->Release();return S_OK;}
        data->Release();
    }
    return o_GetFullscreenState(sc,fullscreen,output);
}
HRESULT STDMETHODCALLTYPE h_SetFullscreenState(IDXGISwapChain* sc,BOOL fullscreen,IDXGIOutput* output){
    if(!g_cfg.nofullscreen){VirtualFullscreen* pending=FullscreenData(sc);if(!pending){g_fsPassthrough++;return o_SetFullscreenState(sc,fullscreen,output);}pending->Release();}
    std::unique_lock<std::mutex> change(g_fsChangeMx,std::try_to_lock);
    if(!change.owns_lock())return DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
    VirtualFullscreen* old=FullscreenData(sc);
    if(!fullscreen||!g_cfg.nofullscreen){
        HRESULT cleanup=old?FullscreenRestore(sc,old):S_OK;if(old)old->Release();
        if(FAILED(cleanup))return cleanup;
        g_fsPassthrough++;return o_SetFullscreenState(sc,fullscreen,output);
    }
    DXGI_SWAP_CHAIN_DESC description{};HRESULT result=sc->GetDesc(&description);
    HWND window=SUCCEEDED(result)?description.OutputWindow:nullptr;
    if(!FullscreenOwnWindow(window)){if(old)old->Release();return FullscreenFailure(window,DXGI_ERROR_INVALID_CALL,false);}
    g_fsRequests++;g_fsLastWindow=window;
    if(g_fullscreenPolicy==1){if(old)old->Release();return FullscreenFailure(window,DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,true);}
    BOOL native=FALSE;result=o_GetFullscreenState?o_GetFullscreenState(sc,&native,nullptr):E_NOINTERFACE;
    if(FAILED(result)||native){if(old)old->Release();return FullscreenFailure(window,FAILED(result)?result:DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,false);}
    IDXGIOutput* selected=output;if(selected)selected->AddRef();else result=sc->GetContainingOutput(&selected);
    if(!selected||FAILED(result)){if(old)old->Release();return FullscreenFailure(window,FAILED(result)?result:DXGI_ERROR_NOT_FOUND,false);}
    LONG_PTR original=old?old->originalStyle:GetWindowLongPtrW(window,GWL_STYLE);
    LONG_PTR applied=GetWindowLongPtrW(window,GWL_STYLE)&~(LONG_PTR)(WS_CAPTION|WS_THICKFRAME);
    auto data=new VirtualFullscreen(window,original,applied,selected);selected->Release();
    result=sc->SetPrivateDataInterface(kFullscreenData,data);
    if(SUCCEEDED(result)){if(!SetPropW(window,kFullscreenOwnerProp,data))result=HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_GEN_FAILURE);else result=FullscreenStyle(window,applied);if(SUCCEEDED(result))data->active=true;else{sc->SetPrivateDataInterface(kFullscreenData,nullptr);if(FullscreenOwnWindow(window)&&GetPropW(window,kFullscreenOwnerProp)==data){RemovePropW(window,kFullscreenOwnerProp);if(GetWindowLongPtrW(window,GWL_STYLE)==applied)FullscreenStyle(window,original);}}}
    data->Release();if(old)old->Release();
    g_fsLastResult=result;
    if(FAILED(result))return FullscreenFailure(window,result,false);
    g_fsBlocked=false;g_fsEmulated++;return S_OK;
}
