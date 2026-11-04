// Presentation-window capture ported from reviewed public commit c63ab25.
// Compose with the existing no-Present fast path; do not add a native wait.
struct OverlaySearch{DWORD pid;HWND best;unsigned long long area;};
bool OverlayWindow(HWND window,DWORD process){DWORD pid=0;wchar_t name[96]{};return window&&window!=g_hwnd&&IsWindow(window)&&IsWindowVisible(window)&&GetWindowThreadProcessId(window,&pid)&&pid==process&&GetClassNameW(window,name,96)&&wcsstr(name,L"PresentationWindow");}
BOOL CALLBACK OverlayEnumProc(HWND window,LPARAM data){auto* search=(OverlaySearch*)data;if(!OverlayWindow(window,search->pid))return TRUE;RECT r{};if(!GetClientRect(window,&r)||r.right<=0||r.bottom<=0)return TRUE;auto area=(unsigned long long)r.right*r.bottom;if(area>search->area){search->best=window;search->area=area;}return TRUE;}
bool PixelsUniform(const std::vector<BYTE>& pixels){if(pixels.size()<8)return true;for(size_t i=4;i+3<pixels.size();i+=4)if(memcmp(pixels.data()+i,pixels.data(),3))return false;return true;}
std::atomic<bool> g_capViaOverlay{false};std::atomic<HWND> g_overlayHint{nullptr};
struct CaptureBitmap{
 HWND window;HDC windowDc=nullptr,dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void* pixels=nullptr;int width,height;
 CaptureBitmap(HWND h,int w,int v):window(h),width(w),height(v){windowDc=GetDC(window);if(windowDc)dc=CreateCompatibleDC(windowDc);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-v;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;if(dc)bitmap=CreateDIBSection(windowDc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(bitmap)old=SelectObject(dc,bitmap);}
 ~CaptureBitmap(){if(old&&old!=HGDI_ERROR)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(windowDc)ReleaseDC(window,windowDc);}
 bool valid()const{return dc&&bitmap&&pixels&&old&&old!=HGDI_ERROR;}
 bool print(UINT flags){if(!valid())return false;memset(pixels,0,(size_t)width*height*4);return PrintWindow(window,dc,flags)!=FALSE;}
 std::vector<BYTE> read()const{GdiFlush();std::vector<BYTE> result((BYTE*)pixels,(BYTE*)pixels+(size_t)width*height*4);for(size_t i=3;i<result.size();i+=4)result[i]=255;return result;}
};
bool CaptureViaPrintWindow(std::string& err){g_capPix.clear();g_capW=g_capH=0;g_capViaOverlay=false;HWND game=g_hwnd;if(!game){err="no window";return false;}RECT r{};if(!GetClientRect(game,&r)||r.right<=0||r.bottom<=0){err="empty client rect";return false;}int width=r.right,height=r.bottom;
 if((unsigned long long)width*height>SIZE_MAX/4||!CaptureDimensionsAllowed(width,height)){err=g_capErr.empty()?"PrintWindow dimensions overflow":g_capErr;return false;}
 CaptureDiagBegin(game,width,height);
 CaptureBitmap destination(game,width,height);if(!destination.valid()){err="PrintWindow bitmap allocation failed";return false;}bool ok=destination.print(PW_CLIENTONLY|PW_RENDERFULLCONTENT);if(ok)g_capPix=destination.read();else err="PrintWindow failed";
 OverlaySearch search{GetCurrentProcessId(),nullptr,0};HWND next=GetTopWindow(nullptr);for(unsigned count=0;next&&count<4096;count++){OverlayEnumProc(next,(LPARAM)&search);HWND prior=next;next=GetWindow(next,GW_HWNDNEXT);if(next==prior)break;}
 if(!search.best){HWND hint=g_overlayHint;if(OverlayWindow(hint,search.pid))search.best=hint;}
 RECT overlay{};if(search.best&&GetClientRect(search.best,&overlay)&&overlay.right>0&&overlay.bottom>0){
  if(overlay.right>16384||overlay.bottom>16384||(unsigned long long)overlay.right*overlay.bottom>64*1024*1024||!CaptureDimensionsAllowed(overlay.right,overlay.bottom)){g_capPix.clear();err="presentation-window source exceeds capture dimensions budget";return false;}
  const auto diagnosticSource=CaptureDiagDescribe(search.best);
  CaptureBitmap source(search.best,overlay.right,overlay.bottom);if(!source.valid()){g_capPix.clear();err="presentation-window bitmap allocation failed";return false;}bool printed=source.print(PW_CLIENTONLY);if(printed&&PixelsUniform(source.read()))printed=source.print(PW_CLIENTONLY|PW_RENDERFULLCONTENT);
  if(printed&&OverlayWindow(search.best,search.pid)){
   double scale=(std::min)((double)overlay.right/width,(double)overlay.bottom/height);int cropW=(int)(width*scale+0.5),cropH=(int)(height*scale+0.5),x=(overlay.right-cropW)/2,y=(overlay.bottom-cropH)/2;
   SetStretchBltMode(destination.dc,HALFTONE);SetBrushOrgEx(destination.dc,0,0,nullptr);
   if(cropW>0&&cropH>0&&StretchBlt(destination.dc,0,0,width,height,source.dc,x,y,cropW,cropH,SRCCOPY)){
    auto shot=destination.read();if(!PixelsUniform(shot)||!ok||PixelsUniform(g_capPix)){g_capPix=std::move(shot);g_capViaOverlay=true;ok=true;err.clear();CaptureDiagSource(diagnosticSource,x,y,cropW,cropH);}
   }
  }
 }
 if(g_hwnd!=game||!IsWindow(game)){g_capPix.clear();err="window changed during PrintWindow capture";return false;}if(ok){g_capW=width;g_capH=height;CaptureDiagPixels(g_capPix);}return ok;
}
