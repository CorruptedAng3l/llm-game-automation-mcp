// Generated forwarding boilerplate from the installed Windows SDK; policy is handwritten.
struct DdWrap1 final : IDirectDraw, DdProxy { using Native=IDirectDraw; Native* real; DdWrap1(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,1),real(n){} ~DdWrap1(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override {return real->Compact();}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD p0, LPDIRECTDRAWCLIPPER * p1, IUnknown  * p2) override {return real->CreateClipper(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD p0, LPPALETTEENTRY p1, LPDIRECTDRAWPALETTE * p2, IUnknown  * p3) override {return real->CreatePalette(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC p0, LPDIRECTDRAWSURFACE  * p1, IUnknown  * p2) override {return DdCreateSurface(*this,p0,p1,p2);}
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE  *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD p0, LPDDSURFACEDESC p1, LPVOID p2, LPDDENUMMODESCALLBACK p3) override {return real->EnumDisplayModes(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override {return real->FlipToGDISurface();}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS p0, LPDDCAPS p1) override {return real->GetCaps(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC p0) override {return DdGetMode(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD p0, LPDWORD p1) override {return real->GetFourCCCodes(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE  * p0) override {return DdGdiSurface(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD p0) override {return real->GetMonitorFrequency(p0);}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD p0) override {return real->GetScanLine(p0);}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL p0) override {return real->GetVerticalBlankStatus(p0);}
 HRESULT STDMETHODCALLTYPE Initialize(GUID  * p0) override {return real->Initialize(p0);}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override {return DdRestoreMode(*this);}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND p0, DWORD p1) override {return DdCoop(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD p0, DWORD p1, DWORD p2) override {return DdSetMode(*this,p0,p1,p2,0,[&]{return real->SetDisplayMode(p0, p1, p2);});}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD p0, HANDLE p1) override {return real->WaitForVerticalBlank(p0, p1);}
};
struct DdWrap2 final : IDirectDraw2, DdProxy { using Native=IDirectDraw2; Native* real; DdWrap2(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,2),real(n){} ~DdWrap2(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override {return real->Compact();}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD p0, LPDIRECTDRAWCLIPPER * p1, IUnknown  * p2) override {return real->CreateClipper(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD p0, LPPALETTEENTRY p1, LPDIRECTDRAWPALETTE * p2, IUnknown  * p3) override {return real->CreatePalette(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC p0, LPDIRECTDRAWSURFACE  * p1, IUnknown  * p2) override {return DdCreateSurface(*this,p0,p1,p2);}
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE  *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD p0, LPDDSURFACEDESC p1, LPVOID p2, LPDDENUMMODESCALLBACK p3) override {return real->EnumDisplayModes(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override {return real->FlipToGDISurface();}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS p0, LPDDCAPS p1) override {return real->GetCaps(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC p0) override {return DdGetMode(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD p0, LPDWORD p1) override {return real->GetFourCCCodes(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE  * p0) override {return DdGdiSurface(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD p0) override {return real->GetMonitorFrequency(p0);}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD p0) override {return real->GetScanLine(p0);}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL p0) override {return real->GetVerticalBlankStatus(p0);}
 HRESULT STDMETHODCALLTYPE Initialize(GUID  * p0) override {return real->Initialize(p0);}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override {return DdRestoreMode(*this);}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND p0, DWORD p1) override {return DdCoop(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD p0, DWORD p1, DWORD p2, DWORD p3, DWORD p4) override {return DdSetMode(*this,p0,p1,p2,p3,[&]{return real->SetDisplayMode(p0, p1, p2, p3, p4);});}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD p0, HANDLE p1) override {return real->WaitForVerticalBlank(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS p0, LPDWORD p1, LPDWORD p2) override {return real->GetAvailableVidMem(p0, p1, p2);}
};
struct DdWrap3 final : IDirectDraw4, DdProxy { using Native=IDirectDraw4; Native* real; DdWrap3(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,3),real(n){} ~DdWrap3(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override {return real->Compact();}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD p0, LPDIRECTDRAWCLIPPER * p1, IUnknown  * p2) override {return real->CreateClipper(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD p0, LPPALETTEENTRY p1, LPDIRECTDRAWPALETTE * p2, IUnknown  * p3) override {return real->CreatePalette(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2 p0, LPDIRECTDRAWSURFACE4  * p1, IUnknown  * p2) override {return DdCreateSurface(*this,p0,p1,p2);}
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE4, LPDIRECTDRAWSURFACE4  *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD p0, LPDDSURFACEDESC2 p1, LPVOID p2, LPDDENUMMODESCALLBACK2 p3) override {return real->EnumDisplayModes(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override {return real->FlipToGDISurface();}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS p0, LPDDCAPS p1) override {return real->GetCaps(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2 p0) override {return DdGetMode(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD p0, LPDWORD p1) override {return real->GetFourCCCodes(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE4  * p0) override {return DdGdiSurface(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD p0) override {return real->GetMonitorFrequency(p0);}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD p0) override {return real->GetScanLine(p0);}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL p0) override {return real->GetVerticalBlankStatus(p0);}
 HRESULT STDMETHODCALLTYPE Initialize(GUID  * p0) override {return real->Initialize(p0);}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override {return DdRestoreMode(*this);}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND p0, DWORD p1) override {return DdCoop(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD p0, DWORD p1, DWORD p2, DWORD p3, DWORD p4) override {return DdSetMode(*this,p0,p1,p2,p3,[&]{return real->SetDisplayMode(p0, p1, p2, p3, p4);});}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD p0, HANDLE p1) override {return real->WaitForVerticalBlank(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2 p0, LPDWORD p1, LPDWORD p2) override {return real->GetAvailableVidMem(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE4 *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override {return real->RestoreAllSurfaces();}
 HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {return real->TestCooperativeLevel();}
 HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER p0, DWORD p1) override {return real->GetDeviceIdentifier(p0, p1);}
};
struct DdWrap4 final : IDirectDraw7, DdProxy { using Native=IDirectDraw7; Native* real; DdWrap4(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,4),real(n){} ~DdWrap4(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Compact() override {return real->Compact();}
 HRESULT STDMETHODCALLTYPE CreateClipper(DWORD p0, LPDIRECTDRAWCLIPPER * p1, IUnknown  * p2) override {return real->CreateClipper(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE CreatePalette(DWORD p0, LPPALETTEENTRY p1, LPDIRECTDRAWPALETTE * p2, IUnknown  * p3) override {return real->CreatePalette(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2 p0, LPDIRECTDRAWSURFACE7  * p1, IUnknown  * p2) override {return DdCreateSurface(*this,p0,p1,p2);}
 HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7  *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD p0, LPDDSURFACEDESC2 p1, LPVOID p2, LPDDENUMMODESCALLBACK2 p3) override {return real->EnumDisplayModes(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE FlipToGDISurface() override {return real->FlipToGDISurface();}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS p0, LPDDCAPS p1) override {return real->GetCaps(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2 p0) override {return DdGetMode(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD p0, LPDWORD p1) override {return real->GetFourCCCodes(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE7  * p0) override {return DdGdiSurface(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD p0) override {return real->GetMonitorFrequency(p0);}
 HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD p0) override {return real->GetScanLine(p0);}
 HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL p0) override {return real->GetVerticalBlankStatus(p0);}
 HRESULT STDMETHODCALLTYPE Initialize(GUID  * p0) override {return real->Initialize(p0);}
 HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override {return DdRestoreMode(*this);}
 HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND p0, DWORD p1) override {return DdCoop(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD p0, DWORD p1, DWORD p2, DWORD p3, DWORD p4) override {return DdSetMode(*this,p0,p1,p2,p3,[&]{return real->SetDisplayMode(p0, p1, p2, p3, p4);});}
 HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD p0, HANDLE p1) override {return real->WaitForVerticalBlank(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2 p0, LPDWORD p1, LPDWORD p2) override {return real->GetAvailableVidMem(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE7 *) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override {return real->RestoreAllSurfaces();}
 HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {return real->TestCooperativeLevel();}
 HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 p0, DWORD p1) override {return real->GetDeviceIdentifier(p0, p1);}
 HRESULT STDMETHODCALLTYPE StartModeTest(LPSIZE p0, DWORD p1, DWORD p2) override {return g_cfg.nofullscreen?DDERR_UNSUPPORTED:real->StartModeTest(p0,p1,p2);}
 HRESULT STDMETHODCALLTYPE EvaluateMode(DWORD p0, DWORD * p1) override {return real->EvaluateMode(p0, p1);}
};
struct DdWrap5 final : IDirectDrawSurface, DdProxy { using Native=IDirectDrawSurface; Native* real; DdWrap5(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,5),real(n){} ~DdWrap5(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT p0) override {return real->AddOverlayDirtyRect(p0);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT p0, LPDIRECTDRAWSURFACE p1, LPRECT p2, DWORD p3, LPDDBLTFX p4) override {return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD p0, DWORD p1, LPDIRECTDRAWSURFACE p2, LPRECT p3, DWORD p4) override {return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE p0, DWORD p1) override {return DdFlip(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS p0, LPDIRECTDRAWSURFACE  * p1) override {return DdAttached(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD p0) override {return real->GetBltStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS p0) override {return DdCaps(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER * p0) override {return real->GetClipper(p0);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->GetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC  * p0) override {if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD p0) override {return real->GetFlipStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG p0, LPLONG p1) override {return real->GetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE * p0) override {return real->GetPalette(p0);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT p0) override {return real->GetPixelFormat(p0);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC p0) override {return DdDescription(*this,p0);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE IsLost() override {return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT p0, LPDDSURFACEDESC p1, DWORD p2, HANDLE p3) override {return DdLock(*this,p0,p1,p2,p3);}
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC p0) override {return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});}
 HRESULT STDMETHODCALLTYPE Restore() override {return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER p0) override {return real->SetClipper(DdUnwrap(p0));}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->SetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG p0, LONG p1) override {return real->SetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE p0) override {return DdPalette(*this,p0);}
 HRESULT STDMETHODCALLTYPE Unlock(LPVOID p0) override {return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDOVERLAYFX) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD p0) override {return real->UpdateOverlayDisplay(p0);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE) override {return DDERR_UNSUPPORTED;}
};
struct DdWrap6 final : IDirectDrawSurface2, DdProxy { using Native=IDirectDrawSurface2; Native* real; DdWrap6(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,6),real(n){} ~DdWrap6(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT p0) override {return real->AddOverlayDirtyRect(p0);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT p0, LPDIRECTDRAWSURFACE2 p1, LPRECT p2, DWORD p3, LPDDBLTFX p4) override {return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD p0, DWORD p1, LPDIRECTDRAWSURFACE2 p2, LPRECT p3, DWORD p4) override {return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE2 p0, DWORD p1) override {return DdFlip(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS p0, LPDIRECTDRAWSURFACE2  * p1) override {return DdAttached(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD p0) override {return real->GetBltStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS p0) override {return DdCaps(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER * p0) override {return real->GetClipper(p0);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->GetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC  * p0) override {if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD p0) override {return real->GetFlipStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG p0, LPLONG p1) override {return real->GetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE * p0) override {return real->GetPalette(p0);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT p0) override {return real->GetPixelFormat(p0);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC p0) override {return DdDescription(*this,p0);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE IsLost() override {return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT p0, LPDDSURFACEDESC p1, DWORD p2, HANDLE p3) override {return DdLock(*this,p0,p1,p2,p3);}
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC p0) override {return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});}
 HRESULT STDMETHODCALLTYPE Restore() override {return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER p0) override {return real->SetClipper(DdUnwrap(p0));}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->SetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG p0, LONG p1) override {return real->SetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE p0) override {return DdPalette(*this,p0);}
 HRESULT STDMETHODCALLTYPE Unlock(LPVOID p0) override {return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE2, LPRECT, DWORD, LPDDOVERLAYFX) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD p0) override {return real->UpdateOverlayDisplay(p0);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID  * p0) override {return DdParent(*this,p0);}
 HRESULT STDMETHODCALLTYPE PageLock(DWORD p0) override {return real->PageLock(p0);}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD p0) override {return real->PageUnlock(p0);}
};
struct DdWrap7 final : IDirectDrawSurface3, DdProxy { using Native=IDirectDrawSurface3; Native* real; DdWrap7(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,7),real(n){} ~DdWrap7(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE3) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT p0) override {return real->AddOverlayDirtyRect(p0);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT p0, LPDIRECTDRAWSURFACE3 p1, LPRECT p2, DWORD p3, LPDDBLTFX p4) override {return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD p0, DWORD p1, LPDIRECTDRAWSURFACE3 p2, LPRECT p3, DWORD p4) override {return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE3) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE3 p0, DWORD p1) override {return DdFlip(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS p0, LPDIRECTDRAWSURFACE3  * p1) override {return DdAttached(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD p0) override {return real->GetBltStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS p0) override {return DdCaps(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER * p0) override {return real->GetClipper(p0);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->GetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC  * p0) override {if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD p0) override {return real->GetFlipStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG p0, LPLONG p1) override {return real->GetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE * p0) override {return real->GetPalette(p0);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT p0) override {return real->GetPixelFormat(p0);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC p0) override {return DdDescription(*this,p0);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE IsLost() override {return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT p0, LPDDSURFACEDESC p1, DWORD p2, HANDLE p3) override {return DdLock(*this,p0,p1,p2,p3);}
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC p0) override {return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});}
 HRESULT STDMETHODCALLTYPE Restore() override {return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER p0) override {return real->SetClipper(DdUnwrap(p0));}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->SetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG p0, LONG p1) override {return real->SetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE p0) override {return DdPalette(*this,p0);}
 HRESULT STDMETHODCALLTYPE Unlock(LPVOID p0) override {return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE3, LPRECT, DWORD, LPDDOVERLAYFX) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD p0) override {return real->UpdateOverlayDisplay(p0);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE3) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID  * p0) override {return DdParent(*this,p0);}
 HRESULT STDMETHODCALLTYPE PageLock(DWORD p0) override {return real->PageLock(p0);}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD p0) override {return real->PageUnlock(p0);}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC, DWORD) override {return DDERR_UNSUPPORTED;}
};
struct DdWrap8 final : IDirectDrawSurface4, DdProxy { using Native=IDirectDrawSurface4; Native* real; DdWrap8(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,8),real(n){} ~DdWrap8(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE4) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT p0) override {return real->AddOverlayDirtyRect(p0);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT p0, LPDIRECTDRAWSURFACE4 p1, LPRECT p2, DWORD p3, LPDDBLTFX p4) override {return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD p0, DWORD p1, LPDIRECTDRAWSURFACE4 p2, LPRECT p3, DWORD p4) override {return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE4) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE4 p0, DWORD p1) override {return DdFlip(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 p0, LPDIRECTDRAWSURFACE4  * p1) override {return DdAttached(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD p0) override {return real->GetBltStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 p0) override {return DdCaps(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER * p0) override {return real->GetClipper(p0);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->GetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC  * p0) override {if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD p0) override {return real->GetFlipStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG p0, LPLONG p1) override {return real->GetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE * p0) override {return real->GetPalette(p0);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT p0) override {return real->GetPixelFormat(p0);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 p0) override {return DdDescription(*this,p0);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE IsLost() override {return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT p0, LPDDSURFACEDESC2 p1, DWORD p2, HANDLE p3) override {return DdLock(*this,p0,p1,p2,p3);}
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC p0) override {return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});}
 HRESULT STDMETHODCALLTYPE Restore() override {return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER p0) override {return real->SetClipper(DdUnwrap(p0));}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->SetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG p0, LONG p1) override {return real->SetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE p0) override {return DdPalette(*this,p0);}
 HRESULT STDMETHODCALLTYPE Unlock(LPRECT p0) override {return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE4, LPRECT, DWORD, LPDDOVERLAYFX) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD p0) override {return real->UpdateOverlayDisplay(p0);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE4) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID  * p0) override {return DdParent(*this,p0);}
 HRESULT STDMETHODCALLTYPE PageLock(DWORD p0) override {return real->PageLock(p0);}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD p0) override {return real->PageUnlock(p0);}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID p0, LPVOID p1, DWORD p2, DWORD p3) override {return real->SetPrivateData(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID p0, LPVOID p1, LPDWORD p2) override {return real->GetPrivateData(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID p0) override {return real->FreePrivateData(p0);}
 HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD p0) override {return real->GetUniquenessValue(p0);}
 HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override {return real->ChangeUniquenessValue();}
};
struct DdWrap9 final : IDirectDrawSurface7, DdProxy { using Native=IDirectDrawSurface7; Native* real; DdWrap9(Native* n,std::shared_ptr<DdGroup> g):DdProxy(g,9),real(n){} ~DdWrap9(){real->Release();}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID p0, LPVOID * p1) override {return DdQuery(group,p0,p1);}
 ULONG STDMETHODCALLTYPE AddRef() override {return RefAdd();}
 ULONG STDMETHODCALLTYPE Release() override {auto keep=group;ULONG n=RefDrop((IUnknown*)this);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT p0) override {return real->AddOverlayDirtyRect(p0);}
 HRESULT STDMETHODCALLTYPE Blt(LPRECT p0, LPDIRECTDRAWSURFACE7 p1, LPRECT p2, DWORD p3, LPDDBLTFX p4) override {return DdSurfaceWrite(*this,[&]{return real->Blt(p0,DdUnwrap(p1),p2,p3,p4);});}
 HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE BltFast(DWORD p0, DWORD p1, LPDIRECTDRAWSURFACE7 p2, LPRECT p3, DWORD p4) override {return DdSurfaceWrite(*this,[&]{return real->BltFast(p0,p1,DdUnwrap(p2),p3,p4);});}
 HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7 p0, DWORD p1) override {return DdFlip(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 p0, LPDIRECTDRAWSURFACE7  * p1) override {return DdAttached(*this,p0,p1);}
 HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD p0) override {return real->GetBltStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 p0) override {return DdCaps(*this,p0);}
 HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER * p0) override {return real->GetClipper(p0);}
 HRESULT STDMETHODCALLTYPE GetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->GetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetDC(HDC  * p0) override {if(DdLost(group->surface))return DDERR_SURFACELOST;return real->GetDC(p0);}
 HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD p0) override {return real->GetFlipStatus(p0);}
 HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG p0, LPLONG p1) override {return real->GetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE * p0) override {return real->GetPalette(p0);}
 HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT p0) override {return real->GetPixelFormat(p0);}
 HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 p0) override {return DdDescription(*this,p0);}
 HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE IsLost() override {return DdLost(group->surface)?DDERR_SURFACELOST:real->IsLost();}
 HRESULT STDMETHODCALLTYPE Lock(LPRECT p0, LPDDSURFACEDESC2 p1, DWORD p2, HANDLE p3) override {return DdLock(*this,p0,p1,p2,p3);}
 HRESULT STDMETHODCALLTYPE ReleaseDC(HDC p0) override {return DdSurfaceWrite(*this,[&]{return real->ReleaseDC(p0);});}
 HRESULT STDMETHODCALLTYPE Restore() override {return DdLost(group->surface)?DDERR_WRONGMODE:real->Restore();}
 HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER p0) override {return real->SetClipper(DdUnwrap(p0));}
 HRESULT STDMETHODCALLTYPE SetColorKey(DWORD p0, LPDDCOLORKEY p1) override {return real->SetColorKey(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG p0, LONG p1) override {return real->SetOverlayPosition(p0, p1);}
 HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE p0) override {return DdPalette(*this,p0);}
 HRESULT STDMETHODCALLTYPE Unlock(LPRECT p0) override {return DdSurfaceWrite(*this,[&]{return real->Unlock(p0);});}
 HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD p0) override {return real->UpdateOverlayDisplay(p0);}
 HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID  * p0) override {return DdParent(*this,p0);}
 HRESULT STDMETHODCALLTYPE PageLock(DWORD p0) override {return real->PageLock(p0);}
 HRESULT STDMETHODCALLTYPE PageUnlock(DWORD p0) override {return real->PageUnlock(p0);}
 HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2, DWORD) override {return DDERR_UNSUPPORTED;}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID p0, LPVOID p1, DWORD p2, DWORD p3) override {return real->SetPrivateData(p0, p1, p2, p3);}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID p0, LPVOID p1, LPDWORD p2) override {return real->GetPrivateData(p0, p1, p2);}
 HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID p0) override {return real->FreePrivateData(p0);}
 HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD p0) override {return real->GetUniquenessValue(p0);}
 HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override {return real->ChangeUniquenessValue();}
 HRESULT STDMETHODCALLTYPE SetPriority(DWORD p0) override {return real->SetPriority(p0);}
 HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD p0) override {return real->GetPriority(p0);}
 HRESULT STDMETHODCALLTYPE SetLOD(DWORD p0) override {return real->SetLOD(p0);}
 HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD p0) override {return real->GetLOD(p0);}
};
IUnknown* DdNewProxy(int kind,IUnknown* native,const std::shared_ptr<DdGroup>& group){switch(kind){
case 1:return new DdWrap1((IDirectDraw*)native,group);
case 2:return new DdWrap2((IDirectDraw2*)native,group);
case 3:return new DdWrap3((IDirectDraw4*)native,group);
case 4:return new DdWrap4((IDirectDraw7*)native,group);
case 5:return new DdWrap5((IDirectDrawSurface*)native,group);
case 6:return new DdWrap6((IDirectDrawSurface2*)native,group);
case 7:return new DdWrap7((IDirectDrawSurface3*)native,group);
case 8:return new DdWrap8((IDirectDrawSurface4*)native,group);
case 9:return new DdWrap9((IDirectDrawSurface7*)native,group);
default:return nullptr;}}
