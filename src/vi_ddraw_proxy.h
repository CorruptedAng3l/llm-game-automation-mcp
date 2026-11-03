// DirectDraw policy and COM proxies. Native vtables and native reference counts
// are untouched. Weak registries cannot keep released application objects alive.
struct DdGroup;
struct DdMode{HWND window=nullptr;DWORD width=0,height=0,bpp=32,refresh=60;unsigned long long epoch=0;bool active=false;};
struct DdSurface;
struct DdDevice{std::mutex mutex;DdMode mode;std::weak_ptr<DdSurface> primary;std::weak_ptr<DdGroup> group;};
struct DdSurface{
 std::shared_ptr<DdDevice> device;std::weak_ptr<DdSurface> front;std::weak_ptr<DdGroup> group;IDirectDrawSurface7* native=nullptr;IDirectDrawSurface7* scratch=nullptr;
 std::vector<std::shared_ptr<DdSurface>> children;std::recursive_mutex presentMutex;DWORD caps=0;unsigned long long epoch=0;bool primary=false,virtualized=false;
 ~DdSurface(){children.clear();if(scratch)scratch->Release();if(native)native->Release();}
};
std::recursive_mutex g_ddRegistryMx;std::map<void*,void*> g_ddNative;std::map<void*,std::weak_ptr<DdGroup>> g_ddIdentities;
std::mutex g_ddModeMx,g_ddLastMx;std::weak_ptr<DdDevice> g_ddLastDevice;
std::atomic<bool> g_ddHooked{false},g_ddBlocked{false};std::atomic<HMODULE> g_ddModule{nullptr},g_ddRetained{nullptr};
std::atomic<long> g_ddCoop{0},g_ddModes{0},g_ddEmulated{0},g_ddRefused{0},g_ddFailed{0},g_ddPresented{0},g_ddDeferredPalette{0},g_ddRecordCount{0},g_ddVersionMask{0};std::atomic<HRESULT> g_ddLastResult{DD_OK};
HRESULT DdQuery(const std::shared_ptr<DdGroup>&,REFIID,void**);
struct DdGroup final : IUnknown,std::enable_shared_from_this<DdGroup>{
 IUnknown* native;std::recursive_mutex mutex;std::map<int,IUnknown*> cache;std::shared_ptr<DdDevice> device;std::shared_ptr<DdSurface> surface,rootKeep;ULONG unknownRefs=0;std::shared_ptr<DdGroup> unknownKeep;
 explicit DdGroup(IUnknown* n):native(n){++g_ddRecordCount;}
 ~DdGroup(){std::lock_guard<std::recursive_mutex> lock(g_ddRegistryMx);g_ddIdentities.erase(this);surface.reset();rootKeep.reset();native->Release();--g_ddRecordCount;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{return DdQuery(shared_from_this(),id,out);}
 ULONG STDMETHODCALLTYPE AddRef() override{std::lock_guard<std::recursive_mutex> lock(mutex);if(!unknownRefs)unknownKeep=shared_from_this();return ++unknownRefs;}
 ULONG STDMETHODCALLTYPE Release() override{std::shared_ptr<DdGroup> keep;ULONG count;{std::lock_guard<std::recursive_mutex> lock(mutex);count=--unknownRefs;if(!count)keep=std::move(unknownKeep);}return count;}
};
struct DdProxy{
 std::shared_ptr<DdGroup> group;int kind;ULONG refs=1;
 DdProxy(std::shared_ptr<DdGroup> g,int k):group(std::move(g)),kind(k){}
 ULONG RefAdd(){std::lock_guard<std::recursive_mutex> lock(group->mutex);return ++refs;}
 ULONG RefDrop(IUnknown* self){std::lock_guard<std::recursive_mutex> registry(g_ddRegistryMx);std::lock_guard<std::recursive_mutex> lock(group->mutex);ULONG left=--refs;if(!left){group->cache.erase(kind);g_ddNative.erase(self);}return left;}
};
int DdKind(REFIID id){const GUID* ids[]={&IID_IDirectDraw,&IID_IDirectDraw2,&IID_IDirectDraw4,&IID_IDirectDraw7,&IID_IDirectDrawSurface,&IID_IDirectDrawSurface2,&IID_IDirectDrawSurface3,&IID_IDirectDrawSurface4,&IID_IDirectDrawSurface7};for(int i=0;i<9;i++)if(id==*ids[i])return i+1;return 0;}
template<class T>T* DdUnwrap(T* p){std::lock_guard<std::recursive_mutex> lock(g_ddRegistryMx);auto it=g_ddNative.find(p);return it==g_ddNative.end()?p:(T*)it->second;}
HRESULT DdWrapOwned(IUnknown*,REFIID,void**,std::shared_ptr<DdDevice>,std::shared_ptr<DdSurface> = {},std::shared_ptr<DdGroup> = {});
template<class T>const GUID& DdId();
template<>const GUID& DdId<IDirectDrawSurface>(){return IID_IDirectDrawSurface;}
template<>const GUID& DdId<IDirectDrawSurface2>(){return IID_IDirectDrawSurface2;}
template<>const GUID& DdId<IDirectDrawSurface3>(){return IID_IDirectDrawSurface3;}
template<>const GUID& DdId<IDirectDrawSurface4>(){return IID_IDirectDrawSurface4;}
template<>const GUID& DdId<IDirectDrawSurface7>(){return IID_IDirectDrawSurface7;}
DdMode DdSnapshot(const std::shared_ptr<DdDevice>& device){if(!device)return {};std::lock_guard<std::mutex> lock(device->mutex);return device->mode;}
HRESULT DdFailure(HRESULT hr,bool refused=false){g_ddLastResult=hr;if(refused){++g_ddRefused;g_ddBlocked=true;}else ++g_ddFailed;return hr;}
bool DdLost(const std::shared_ptr<DdSurface>& s){if(!s||!s->virtualized)return false;auto mode=DdSnapshot(s->device);return !mode.active||mode.epoch!=s->epoch;}
DDPIXELFORMAT DdFormat(DWORD bpp){DDPIXELFORMAT p{};p.dwSize=sizeof(p);p.dwFlags=DDPF_RGB;p.dwRGBBitCount=bpp;if(bpp==8)p.dwFlags|=DDPF_PALETTEINDEXED8;else if(bpp==15){p.dwRGBBitCount=16;p.dwRBitMask=0x7c00;p.dwGBitMask=0x3e0;p.dwBBitMask=0x1f;}else if(bpp==16){p.dwRBitMask=0xf800;p.dwGBitMask=0x7e0;p.dwBBitMask=0x1f;}else{p.dwRBitMask=0xff0000;p.dwGBitMask=0xff00;p.dwBBitMask=0xff;}return p;}
HRESULT DdWindow(HWND window,DWORD width,DWORD height){
 if(!FullscreenOwnWindow(window)||GetAncestor(window,GA_ROOT)!=window)return DDERR_INVALIDPARAMS;
 LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE),applied=(style&~(LONG_PTR)WS_POPUP)|WS_OVERLAPPEDWINDOW;
 SetLastError(0);LONG_PTR prior=SetWindowLongPtrW(window,GWL_STYLE,applied);DWORD error=GetLastError();if(!prior&&error)return HRESULT_FROM_WIN32(error);
 DWORD ex=(DWORD)GetWindowLongPtrW(window,GWL_EXSTYLE);RECT r{0,0,(LONG)width,(LONG)height};if(!AdjustWindowRectEx(&r,(DWORD)applied,GetMenu(window)!=nullptr,ex&~WS_EX_TOPMOST))return HRESULT_FROM_WIN32(GetLastError());
 UINT flags=SWP_NOMOVE|SWP_NOACTIVATE|SWP_FRAMECHANGED;if(!(ex&WS_EX_TOPMOST))flags|=SWP_NOZORDER;
 if(!(o_SetWindowPos?o_SetWindowPos(window,HWND_NOTOPMOST,0,0,r.right-r.left,r.bottom-r.top,flags):SetWindowPos(window,HWND_NOTOPMOST,0,0,r.right-r.left,r.bottom-r.top,flags)))return HRESULT_FROM_WIN32(GetLastError());
 RECT actual{};if(!GetClientRect(window,&actual)||(DWORD)actual.right!=width||(DWORD)actual.bottom!=height||(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST))return DDERR_GENERIC;return DD_OK;
}
template<class P>HRESULT DdCoop(P& p,HWND window,DWORD flags){bool exclusive=(flags&(DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN))!=0;
 if(g_cfg.nofullscreen&&exclusive){++g_ddCoop;if(g_fullscreenPolicy==1)return DdFailure(DDERR_NOEXCLUSIVEMODE,true);if(!FullscreenOwnWindow(window))return DdFailure(DDERR_INVALIDPARAMS);
  DWORD legal=DDSCL_NORMAL|(flags&(DDSCL_MULTITHREADED|DDSCL_FPUSETUP|DDSCL_FPUPRESERVE));HRESULT hr=p.real->SetCooperativeLevel(window,legal);if(FAILED(hr))return DdFailure(hr);
  {std::lock_guard<std::mutex> lock(p.group->device->mutex);p.group->device->mode.window=window;}++g_ddEmulated;g_ddBlocked=false;g_ddLastResult=DD_OK;return hr;
 }HRESULT hr=p.real->SetCooperativeLevel(window,flags);if(SUCCEEDED(hr)&&FullscreenOwnWindow(window)){std::lock_guard<std::mutex> lock(p.group->device->mutex);p.group->device->mode.window=window;}return hr;
}
template<class P,class F>HRESULT DdSetMode(P& p,DWORD width,DWORD height,DWORD bpp,DWORD refresh,F native){if(!g_cfg.nofullscreen)return native();++g_ddModes;if(g_fullscreenPolicy==1)return DdFailure(DDERR_NOEXCLUSIVEMODE,true);
 if(!width||!height||width>8192||height>8192||(unsigned long long)width*height>16*1024*1024||(bpp!=8&&bpp!=15&&bpp!=16&&bpp!=24&&bpp!=32))return DdFailure(DDERR_INVALIDMODE);
 std::unique_lock<std::mutex> transaction(g_ddModeMx,std::try_to_lock);if(!transaction.owns_lock())return DDERR_WASSTILLDRAWING;auto before=DdSnapshot(p.group->device);HWND window=before.window?before.window:g_hwnd;
 HRESULT hr=DdWindow(window,width,height);if(FAILED(hr))return DdFailure(hr);{std::lock_guard<std::mutex> lock(p.group->device->mutex);p.group->device->mode={window,width,height,bpp,refresh?refresh:60,before.epoch+1,true};}
 {std::lock_guard<std::mutex> lock(g_ddLastMx);g_ddLastDevice=p.group->device;}++g_ddEmulated;g_ddBlocked=false;g_ddLastResult=DD_OK;return DD_OK;
}
template<class P,class D>HRESULT DdGetMode(P& p,D* desc){auto mode=DdSnapshot(p.group->device);if(!mode.active)return p.real->GetDisplayMode(desc);if(!desc||desc->dwSize!=sizeof(D))return DDERR_INVALIDPARAMS;
 *desc={};desc->dwSize=sizeof(D);desc->dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE|DDSD_PITCH;desc->dwWidth=mode.width;desc->dwHeight=mode.height;desc->lPitch=(LONG)(((unsigned long long)mode.width*(mode.bpp==15?16:mode.bpp)+31)/32*4);desc->dwRefreshRate=mode.refresh;desc->ddpfPixelFormat=DdFormat(mode.bpp);return DD_OK;
}
template<class P>HRESULT DdRestoreMode(P& p){auto mode=DdSnapshot(p.group->device);if(!mode.active)return p.real->RestoreDisplayMode();std::lock_guard<std::mutex> lock(p.group->device->mutex);p.group->device->mode.active=false;++p.group->device->mode.epoch;return DD_OK;}
HRESULT DdOffscreen(IDirectDraw7* draw,const DdMode& mode,IDirectDrawSurface7** out){DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;d.dwWidth=mode.width;d.dwHeight=mode.height;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;d.ddpfPixelFormat=DdFormat(mode.bpp);return draw->CreateSurface(&d,out,nullptr);}
template<class P,class D,class S>HRESULT DdCreateSurface(P& p,D* desc,S** out,IUnknown* outer){if(!desc||!out||desc->dwSize!=sizeof(D))return DDERR_INVALIDPARAMS;*out=nullptr;auto mode=DdSnapshot(p.group->device);DWORD caps=desc->ddsCaps.dwCaps;
 if(!(caps&DDSCAPS_PRIMARYSURFACE)||!mode.active){S* native=nullptr;HRESULT hr=p.real->CreateSurface(desc,&native,outer);if(FAILED(hr)||!native)return hr;auto s=std::make_shared<DdSurface>();s->device=p.group->device;native->QueryInterface(IID_IDirectDrawSurface7,(void**)&s->native);return DdWrapOwned(native,DdId<S>(),(void**)out,p.group->device,s);}
 if(outer||(caps&(DDSCAPS_3DDEVICE|DDSCAPS_TEXTURE|DDSCAPS_ZBUFFER)))return DdFailure(DDERR_UNSUPPORTED);DWORD count=(desc->dwFlags&DDSD_BACKBUFFERCOUNT)?desc->dwBackBufferCount:0;if(count>8||((caps&DDSCAPS_FLIP)&&!count))return DDERR_INVALIDPARAMS;
 {std::lock_guard<std::mutex> lock(p.group->device->mutex);auto prior=p.group->device->primary.lock();if(prior&&prior->epoch==mode.epoch)return DDERR_PRIMARYSURFACEALREADYEXISTS;}
 IDirectDraw7* draw=nullptr;HRESULT hr=p.real->QueryInterface(IID_IDirectDraw7,(void**)&draw);if(FAILED(hr))return hr;auto s=std::make_shared<DdSurface>();s->device=p.group->device;s->primary=s->virtualized=true;s->epoch=mode.epoch;s->caps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FRONTBUFFER|DDSCAPS_SYSTEMMEMORY|(caps&(DDSCAPS_FLIP|DDSCAPS_COMPLEX|DDSCAPS_VISIBLE));
 hr=DdOffscreen(draw,mode,&s->native);for(DWORD i=0;SUCCEEDED(hr)&&i<count;i++){auto b=std::make_shared<DdSurface>();b->device=s->device;b->front=s;b->virtualized=true;b->epoch=mode.epoch;b->caps=DDSCAPS_BACKBUFFER|DDSCAPS_FLIP|DDSCAPS_COMPLEX|DDSCAPS_SYSTEMMEMORY;hr=DdOffscreen(draw,mode,&b->native);if(SUCCEEDED(hr))s->children.push_back(b);}if(SUCCEEDED(hr)&&count)hr=DdOffscreen(draw,mode,&s->scratch);draw->Release();if(FAILED(hr))return DdFailure(hr);
 S* native=nullptr;hr=s->native->QueryInterface(DdId<S>(),(void**)&native);if(FAILED(hr))return hr;hr=DdWrapOwned(native,DdId<S>(),(void**)out,p.group->device,s);if(SUCCEEDED(hr)){std::lock_guard<std::mutex> lock(s->device->mutex);s->device->primary=s;}return hr;
}
HRESULT DdPresent(const std::shared_ptr<DdSurface>& s){if(!s||!s->primary)return DD_OK;if(DdLost(s))return DDERR_SURFACELOST;auto mode=DdSnapshot(s->device);RECT client{};if(!FullscreenOwnWindow(mode.window)||!GetClientRect(mode.window,&client)||(DWORD)client.right!=mode.width||(DWORD)client.bottom!=mode.height)return DdFailure(DDERR_WRONGMODE);
 std::unique_lock<std::recursive_mutex> lock(s->presentMutex,std::try_to_lock);if(!lock.owns_lock())return DDERR_WASSTILLDRAWING;HDC source=nullptr;HRESULT hr=s->native->GetDC(&source);if(hr==DDERR_NOPALETTEATTACHED){++g_ddDeferredPalette;g_ddLastResult=hr;return DD_OK;}if(FAILED(hr)||!source)return DdFailure(FAILED(hr)?hr:DDERR_GENERIC);
 HDC destination=GetDC(mode.window);bool shown=destination&&BitBlt(destination,0,0,(int)mode.width,(int)mode.height,source,0,0,SRCCOPY);if(destination)ReleaseDC(mode.window,destination);
 if(shown){++g_ddPresented;if(mode.window==g_hwnd){C.present++;OnFrame(API_DDRAW);g_scApi=API_DDRAW;g_scW=mode.width;g_scH=mode.height;if(BeginCaptureClaim()){
   if(CaptureDimensionsAllowed(mode.width,mode.height)){HDC memory=CreateCompatibleDC(source);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=(LONG)mode.width;info.bmiHeader.biHeight=-(LONG)mode.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;HBITMAP bitmap=memory?CreateDIBSection(memory,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;HGDIOBJ old=bitmap?SelectObject(memory,bitmap):nullptr;
    if(bitmap&&pixels&&old&&BitBlt(memory,0,0,(int)mode.width,(int)mode.height,source,0,0,SRCCOPY)){GdiFlush();g_capPix.assign((BYTE*)pixels,(BYTE*)pixels+(size_t)mode.width*mode.height*4);for(size_t i=3;i<g_capPix.size();i+=4)g_capPix[i]=255;g_capW=mode.width;g_capH=mode.height;g_capFmt=DXGI_FORMAT_B8G8R8A8_UNORM;}else g_capErr="ddraw: front-surface readback failed";
    if(old)SelectObject(memory,old);if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);
   }EndCapture(API_DDRAW);
  }}}
 HRESULT released=s->native->ReleaseDC(source);if(!shown)return DdFailure(DDERR_GENERIC);return FAILED(released)?DdFailure(released):DD_OK;
}
template<class P,class F>HRESULT DdSurfaceWrite(P& p,F native){if(DdLost(p.group->surface))return DDERR_SURFACELOST;HRESULT hr=native();return SUCCEEDED(hr)?DdPresent(p.group->surface):hr;}
template<class P,class S>HRESULT DdFlip(P& p,S* target,DWORD flags){auto s=p.group->surface;if(!s||!s->virtualized)return p.real->Flip(DdUnwrap(target),flags);if(!s->primary||s->children.empty()||!s->scratch)return DDERR_NOTFLIPPABLE;if(DdLost(s))return DDERR_SURFACELOST;
 if(flags&~(DDFLIP_WAIT|DDFLIP_DONOTWAIT))return DDERR_UNSUPPORTED;std::unique_lock<std::recursive_mutex> lock(s->presentMutex,std::try_to_lock);if(!lock.owns_lock())return DDERR_WASSTILLDRAWING;
 auto copy=[](IDirectDrawSurface7* dst,IDirectDrawSurface7* src){return dst->Blt(nullptr,src,nullptr,DDBLT_WAIT,nullptr);};HRESULT hr=copy(s->scratch,s->native);
 if(target){IDirectDrawSurface7* id=nullptr;HRESULT q=DdUnwrap(target)->QueryInterface(IID_IDirectDrawSurface7,(void**)&id);std::shared_ptr<DdSurface> selected;for(auto b:s->children)if(b->native==id)selected=b;if(id)id->Release();if(FAILED(q)||!selected)return DDERR_INVALIDOBJECT;if(SUCCEEDED(hr))hr=copy(s->native,selected->native);if(SUCCEEDED(hr))hr=copy(selected->native,s->scratch);}
 else{auto prior=s->native;for(auto b:s->children){if(SUCCEEDED(hr))hr=copy(prior,b->native);prior=b->native;}if(SUCCEEDED(hr))hr=copy(prior,s->scratch);}return SUCCEEDED(hr)?DdPresent(s):DdFailure(hr);
}
template<class S>HRESULT DdReturnSurface(const std::shared_ptr<DdSurface>& s,S** out){if(!out)return DDERR_INVALIDPARAMS;*out=nullptr;if(!s||!s->native)return DDERR_NOTFOUND;S* native=nullptr;HRESULT hr=s->native->QueryInterface(DdId<S>(),(void**)&native);return FAILED(hr)?hr:DdWrapOwned(native,DdId<S>(),(void**)out,s->device,s);}
template<class P,class C,class S>HRESULT DdAttached(P& p,C* caps,S** out){auto s=p.group->surface;if(!s||!s->virtualized){S* native=nullptr;HRESULT hr=p.real->GetAttachedSurface(caps,&native);return FAILED(hr)?hr:DdWrapOwned(native,DdId<S>(),(void**)out,p.group->device);}
 if(!caps||!out)return DDERR_INVALIDPARAMS;*out=nullptr;if(DdLost(s))return DDERR_SURFACELOST;auto front=s->primary?s:s->front.lock();if(!front)return DDERR_NOTFOUND;if((caps->dwCaps&DDSCAPS_FRONTBUFFER)&&!s->primary)return DdReturnSurface(front,out);
 if(caps->dwCaps&DDSCAPS_BACKBUFFER){size_t i=0;if(!s->primary){for(;i<front->children.size();i++)if(front->children[i]==s){++i;break;}}if(i<front->children.size())return DdReturnSurface(front->children[i],out);}return DDERR_NOTFOUND;
}
template<class P,class D>HRESULT DdDescription(P& p,D* desc){HRESULT hr=p.real->GetSurfaceDesc(desc);auto s=p.group->surface;if(SUCCEEDED(hr)&&desc&&s&&s->virtualized){desc->ddsCaps.dwCaps=s->caps;if(!s->children.empty()){desc->dwFlags|=DDSD_BACKBUFFERCOUNT;desc->dwBackBufferCount=(DWORD)s->children.size();}}return hr;}
template<class P,class D>HRESULT DdLock(P& p,RECT* region,D* desc,DWORD flags,HANDLE event){auto s=p.group->surface;if(DdLost(s))return DDERR_SURFACELOST;HRESULT hr=p.real->Lock(region,desc,flags,event);if(SUCCEEDED(hr)&&desc&&s&&s->virtualized){desc->ddsCaps.dwCaps=s->caps;if(!s->children.empty()){desc->dwFlags|=DDSD_BACKBUFFERCOUNT;desc->dwBackBufferCount=(DWORD)s->children.size();}}return hr;}
template<class P,class C>HRESULT DdCaps(P& p,C* caps){HRESULT hr=p.real->GetCaps(caps);auto s=p.group->surface;if(SUCCEEDED(hr)&&caps&&s&&s->virtualized)caps->dwCaps=s->caps;return hr;}
template<class P>HRESULT DdParent(P& p,void** out){void* native=nullptr;HRESULT hr=p.real->GetDDInterface(&native);return FAILED(hr)?hr:DdWrapOwned((IUnknown*)native,IID_IUnknown,out,p.group->device);}
template<class P,class S>HRESULT DdGdiSurface(P& p,S** out){if(!DdSnapshot(p.group->device).active){S* native=nullptr;HRESULT hr=p.real->GetGDISurface(&native);return FAILED(hr)?hr:DdWrapOwned(native,DdId<S>(),(void**)out,p.group->device);}
 std::shared_ptr<DdSurface> s;{std::lock_guard<std::mutex> lock(p.group->device->mutex);s=p.group->device->primary.lock();}return DdReturnSurface(s,out);
}
template<class P>HRESULT DdPalette(P& p,IDirectDrawPalette* palette){HRESULT hr=p.real->SetPalette(palette);auto s=p.group->surface;if(FAILED(hr)||!s||!s->primary)return hr;for(auto b:s->children){hr=b->native->SetPalette(palette);if(FAILED(hr))return hr;}if(s->scratch)hr=s->scratch->SetPalette(palette);return SUCCEEDED(hr)?DdPresent(s):hr;}
#include "vi_ddraw_forward.inl"
HRESULT DdWrapOwned(IUnknown* native,REFIID id,void** out,std::shared_ptr<DdDevice> device,std::shared_ptr<DdSurface> surface,std::shared_ptr<DdGroup> group){if(!out){if(native)native->Release();return E_POINTER;}*out=nullptr;if(!native)return E_NOINTERFACE;int kind=DdKind(id);if(!kind&&id!=IID_IUnknown){native->Release();return E_NOINTERFACE;}
 std::lock_guard<std::recursive_mutex> registry(g_ddRegistryMx);if(g_ddNative.count(native)||g_ddIdentities.count(native)){*out=native;return S_OK;}if(!device)device=std::make_shared<DdDevice>();if(kind>4&&!surface){surface=std::make_shared<DdSurface>();surface->device=device;native->QueryInterface(IID_IDirectDrawSurface7,(void**)&surface->native);}
 if(!group)group=surface?surface->group.lock():device->group.lock();if(!group){native->AddRef();group=std::make_shared<DdGroup>(native);group->device=device;group->surface=surface;g_ddIdentities[group.get()]=group;if(surface){group->rootKeep=surface->front.lock();surface->group=group;}else device->group=group;}
 std::lock_guard<std::recursive_mutex> lock(group->mutex);if(!kind){native->Release();group->AddRef();*out=(IUnknown*)group.get();return S_OK;}auto cached=group->cache.find(kind);if(cached!=group->cache.end()){cached->second->AddRef();native->Release();*out=cached->second;return S_OK;}
 IUnknown* proxy=DdNewProxy(kind,native,group);if(!proxy){native->Release();return E_OUTOFMEMORY;}group->cache[kind]=proxy;g_ddNative[proxy]=native;*out=proxy;if(kind<=4)g_ddVersionMask.fetch_or(1<<(kind-1));return S_OK;
}
HRESULT DdQuery(const std::shared_ptr<DdGroup>& group,REFIID id,void** out){if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown){group->AddRef();*out=(IUnknown*)group.get();return S_OK;}if(!DdKind(id))return E_NOINTERFACE;IUnknown* native=nullptr;HRESULT hr=group->native->QueryInterface(id,(void**)&native);return FAILED(hr)?hr:DdWrapOwned(native,id,out,group->device,group->surface,group);}
using DdCreate=HRESULT(WINAPI*)(GUID*,IDirectDraw**,IUnknown*);using DdCreateEx=HRESULT(WINAPI*)(GUID*,void**,REFIID,IUnknown*);DdCreate o_DdCreate=nullptr;DdCreateEx o_DdCreateEx=nullptr;
void DdRetain(){if(g_ddRetained)return;HMODULE module=g_ddModule,owned=nullptr;if(module&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)module,&owned)){HMODULE empty=nullptr;if(!g_ddRetained.compare_exchange_strong(empty,owned))FreeLibrary(owned);}}
thread_local unsigned t_ddFactory=0;struct DdFactoryScope{DdFactoryScope(){++t_ddFactory;}~DdFactoryScope(){--t_ddFactory;}};
HRESULT WINAPI h_DdCreate(GUID* guid,IDirectDraw** out,IUnknown* outer){if(t_ddFactory)return o_DdCreate(guid,out,outer);DdFactoryScope scope;DdRetain();if(!g_cfg.nofullscreen)return o_DdCreate(guid,out,outer);if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;IDirectDraw* native=nullptr;HRESULT hr=o_DdCreate(guid,&native,outer);return FAILED(hr)?hr:DdWrapOwned(native,IID_IDirectDraw,(void**)out,{});}
HRESULT WINAPI h_DdCreateEx(GUID* guid,void** out,REFIID id,IUnknown* outer){if(t_ddFactory)return o_DdCreateEx(guid,out,id,outer);DdFactoryScope scope;DdRetain();if(!g_cfg.nofullscreen)return o_DdCreateEx(guid,out,id,outer);if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;IUnknown* native=nullptr;HRESULT hr=o_DdCreateEx(guid,(void**)&native,id,outer);Log("ddraw factory ex native_hr=0x%08lX kind=%d native=%p",(unsigned long)hr,DdKind(id),native);return FAILED(hr)?hr:DdWrapOwned(native,id,out,{});}
using DdCoCreate=HRESULT(WINAPI*)(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*);DdCoCreate o_DdCoCreate=nullptr;std::atomic<bool> g_ddComHooked{false};
HRESULT WINAPI h_DdCoCreate(REFCLSID cls,LPUNKNOWN outer,DWORD context,REFIID id,LPVOID* out){if(t_ddFactory||!(cls==CLSID_DirectDraw||cls==CLSID_DirectDraw7))return o_DdCoCreate(cls,outer,context,id,out);DdFactoryScope scope;if(g_cfg.nofullscreen&&outer){if(out)*out=nullptr;return CLASS_E_NOAGGREGATION;}HRESULT hr=o_DdCoCreate(cls,outer,context,id,out);if(SUCCEEDED(hr)&&out&&*out){DdRetain();if(g_cfg.nofullscreen)return DdWrapOwned((IUnknown*)*out,id,out,{});}return hr;}
void TryHookDDraw(HMODULE module=nullptr){HMODULE ole=GetModuleHandleW(L"ole32.dll");if(ole&&!g_ddComHooked.exchange(true))HookAddr("CoCreateInstance(ddraw)",(void*)GetProcAddress(ole,"CoCreateInstance"),(void*)&h_DdCoCreate,(void**)&o_DdCoCreate);if(g_ddHooked)return;if(!module)module=GetModuleHandleW(L"ddraw.dll");if(!module||g_ddHooked.exchange(true))return;g_ddModule=module;HookAddr("DirectDrawCreate",(void*)GetProcAddress(module,"DirectDrawCreate"),(void*)&h_DdCreate,(void**)&o_DdCreate);HookAddr("DirectDrawCreateEx",(void*)GetProcAddress(module,"DirectDrawCreateEx"),(void*)&h_DdCreateEx,(void**)&o_DdCreateEx);}
bool DdGeometryMismatch(){std::shared_ptr<DdDevice> d;{std::lock_guard<std::mutex> lock(g_ddLastMx);d=g_ddLastDevice.lock();}if(!d)return false;auto m=DdSnapshot(d);if(!m.active||m.window!=g_hwnd)return false;RECT r{};return !GetClientRect(m.window,&r)||(DWORD)r.right!=m.width||(DWORD)r.bottom!=m.height;}
bool DdBlockedBeforePresent(){return g_cfg.nofullscreen&&g_ddBlocked&&g_ddRefused>=2&&C.present_ours==0;}
