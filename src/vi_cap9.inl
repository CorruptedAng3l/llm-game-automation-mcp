// D3D9 readback runs on the presenting thread. No surface is retained across
// Present/Reset; multisample resolve uses StretchRect without changing bindings.
#define VI_D3D9_CAPTURE 1
std::atomic<unsigned long long> g_d9PresentAttempts{0},g_d9PresentOk{0},g_d9Lost{0},g_d9CaptureFailures{0};
std::atomic<HRESULT> g_d9PresentLast{S_OK};
std::atomic<bool> g_d9PresentHooked{false};
thread_local unsigned g_d9PresentDepth=0;
thread_local bool g_d9Internal=false;
template<class T>struct D9Borrow {T* value=nullptr;~D9Borrow(){if(value)value->Release();}};
void CaptureD9(IDirect3DDevice9* device,IDirect3DSwapChain9* chain){
    D9Borrow<IDirect3DSurface9> back,resolved,host;
    HRESULT hr=chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back.value);
    D3DSURFACE_DESC desc{};if(SUCCEEDED(hr))hr=back.value->GetDesc(&desc);
    if(FAILED(hr)){g_capErr="d3d9: backbuffer unavailable";return;}
    if(!CaptureDimensionsAllowed(desc.Width,desc.Height))return;
    const auto format=desc.Format==D3DFMT_A8R8G8B8?DXGI_FORMAT_B8G8R8A8_UNORM:desc.Format==D3DFMT_X8R8G8B8?DXGI_FORMAT_B8G8R8X8_UNORM:DXGI_FORMAT_UNKNOWN;
    if(format==DXGI_FORMAT_UNKNOWN){g_capErr="d3d9: unsupported backbuffer format "+std::to_string(desc.Format);return;}
    IDirect3DSurface9* source=back.value;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
        hr=device->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved.value,nullptr);
        if(SUCCEEDED(hr))hr=device->StretchRect(back.value,nullptr,resolved.value,nullptr,D3DTEXF_NONE);
        if(FAILED(hr)){g_capErr="d3d9: multisample resolve failed";return;}source=resolved.value;
    }
    hr=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&host.value,nullptr);
    if(SUCCEEDED(hr))hr=device->GetRenderTargetData(source,host.value);
    D3DLOCKED_RECT locked{};if(SUCCEEDED(hr))hr=host.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);
    if(FAILED(hr)){g_capErr="d3d9: readback failed HRESULT "+std::to_string((unsigned long)hr);return;}
    bool ok=locked.pBits&&locked.Pitch>0&&(unsigned)locked.Pitch>=desc.Width*4;
    if(ok){g_capPix.resize((size_t)desc.Width*desc.Height*4);for(UINT y=0;y<desc.Height&&ok;++y)ok=ConvertRow(format,(BYTE*)locked.pBits+(size_t)y*locked.Pitch,g_capPix.data()+(size_t)y*desc.Width*4,desc.Width);}
    const HRESULT unlocked=host.value->UnlockRect();
    if(ok&&SUCCEEDED(unlocked)){g_capW=desc.Width;g_capH=desc.Height;g_capFmt=format;}
    else{g_capPix.clear();g_capErr="d3d9: invalid readback pitch or unlock failure";}
}
template<class F>HRESULT D9PresentFrame(IDirect3DDevice9* device,IDirect3DSwapChain9* chain,HWND destination,F native){
    if(g_d9Internal||g_d9PresentDepth)return native();
    struct Depth{Depth(){++g_d9PresentDepth;}~Depth(){--g_d9PresentDepth;}} depth;
    D3DPRESENT_PARAMETERS params{};bool owned=chain&&SUCCEEDED(chain->GetPresentParameters(&params));
    const HWND window=destination?destination:params.hDeviceWindow;
    owned=owned&&FullscreenOwnWindow(window)&&(!g_hwnd||window==g_hwnd||GetAncestor(window,GA_ROOT)==g_hwnd);
    if(!owned)return native();
    ++g_d9PresentAttempts;++C.present;
    const bool capture=BeginCaptureClaim();if(capture)CaptureD9(device,chain);
    const HRESULT hr=native();g_d9PresentLast=hr;
    if(hr==D3D_OK){++g_d9PresentOk;OnFrame(API_D3D9);}
    else if(hr==D3DERR_DEVICELOST||hr==D3DERR_DEVICENOTRESET)++g_d9Lost;
    if(capture){if(hr!=D3D_OK){g_capPix.clear();g_capW=g_capH=0;g_capErr="d3d9: Present did not render, HRESULT "+std::to_string((unsigned long)hr);}if(!g_capErr.empty())++g_d9CaptureFailures;EndCapture(API_D3D9);}
    return hr;
}
using D9Present=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
HRESULT STDMETHODCALLTYPE h_D9Present(IDirect3DDevice9* self,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty){
    const auto original=D9Original<D9Present>(self,17,D9_PRESENT);if(!original)return D3DERR_INVALIDCALL;
    D9Borrow<IDirect3DSwapChain9> chain;self->GetSwapChain(0,&chain.value);
    return D9PresentFrame(self,chain.value,window,[&]{return original(self,source,dest,window,dirty);});
}
using D9PresentEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
HRESULT STDMETHODCALLTYPE h_D9PresentEx(IDirect3DDevice9Ex* self,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty,DWORD flags){
    const auto original=D9Original<D9PresentEx>(self,121,D9_PRESENTEX);if(!original)return D3DERR_INVALIDCALL;
    D9Borrow<IDirect3DSwapChain9> chain;self->GetSwapChain(0,&chain.value);
    return D9PresentFrame(self,chain.value,window,[&]{return original(self,source,dest,window,dirty,flags);});
}
using D9ChainPresent=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
HRESULT STDMETHODCALLTYPE h_D9ChainPresent(IDirect3DSwapChain9* self,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty,DWORD flags){
    const auto original=D9Original<D9ChainPresent>(self,3,D9_CHAINPRESENT);if(!original)return D3DERR_INVALIDCALL;
    D9Borrow<IDirect3DDevice9> device;if(FAILED(self->GetDevice(&device.value)))return original(self,source,dest,window,dirty,flags);
    return D9PresentFrame(device.value,self,window,[&]{return original(self,source,dest,window,dirty,flags);});
}
bool D9Chain(IDirect3DSwapChain9* chain){const bool ok=D9Patch(chain,3,D9_CHAINPRESENT,(void*)&h_D9ChainPresent,"IDirect3DSwapChain9::Present");if(ok)g_d9PresentHooked=true;return ok;}
void D9ObserveDevice(IDirect3DDevice9* device){
    // Optional observation must not invalidate a successfully created device.
    if(D9Patch(device,17,D9_PRESENT,(void*)&h_D9Present,"IDirect3DDevice9::Present"))g_d9PresentHooked=true;
    D9Borrow<IDirect3DSwapChain9> chain;if(SUCCEEDED(device->GetSwapChain(0,&chain.value))&&chain.value)D9Chain(chain.value);
    D9Borrow<IDirect3DDevice9Ex> ex;if(SUCCEEDED(device->QueryInterface(IID_IDirect3DDevice9Ex,(void**)&ex.value))&&ex.value)
        if(D9Patch(ex.value,121,D9_PRESENTEX,(void*)&h_D9PresentEx,"IDirect3DDevice9Ex::PresentEx"))g_d9PresentHooked=true;
}
