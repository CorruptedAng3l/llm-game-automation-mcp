// Independent consumer-readiness oracle. Reads keys and receives every INSERT, but only
// acts on INSERT after WM_APP+1 explicitly arms the consumer. No OS input/foreground calls.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* logFile;
static bool ready=false,menu=false;
static void Log(const char* name,int value=0){fprintf(logFile,"%llu %s value=%d ready=%d menu=%d\n",GetTickCount64(),name,value,(int)ready,(int)menu);fflush(logFile);}
static void Paint(HWND h,HDC dc) {
    RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));
    HFONT font=CreateFontW(-32,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));
    const wchar_t* text=menu?L"OVERLAY PANEL":ready?L"MENU CLOSED":L"OVERLAY STARTING";
    TextOutW(dc,30,70,text,(int)wcslen(text));SelectObject(dc,old);DeleteObject(font);
}
static void Redraw(HWND h) {
    // A layered GDI backing bitmap remains paintable while entirely offscreen;
    // ordinary screen-clipped GDI painting can leave an empty DWM capture there.
    RECT r{};GetClientRect(h,&r);HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=r.right;bi.bmiHeader.biHeight=-r.bottom;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);HGDIOBJ old=SelectObject(dc,bitmap);
    Paint(h,dc);for(size_t n=3;n<(size_t)r.right*r.bottom*4;n+=4)((BYTE*)bits)[n]=255;
    POINT destination{-28000,600},source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(h,nullptr,&destination,&size,dc,&source,0,&blend,ULW_ALPHA);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_APP+1){ready=w!=0;Log("READY",(int)w);Redraw(h);return 0;}
    if(m==WM_KEYDOWN && w==VK_INSERT){Log("INSERT_DOWN");if(ready){menu=!menu;Log("MENU_TOGGLED",menu);Redraw(h);}else Log("CONSUMER_NOT_READY");return 0;}
    if(m==WM_KEYUP && w==VK_INSERT){Log("INSERT_UP");return 0;}
    if(m==WM_PRINT || m==WM_PRINTCLIENT){Paint(h,(HDC)w);Log("PRINT");return 0;}
    if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);Paint(h,dc);EndPaint(h,&p);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<2)return 2;
    logFile=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!logFile)return 3;
    WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_keyready";RegisterClassW(&c);
    HWND h=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_LAYERED,c.lpszClassName,L"Key consumer readiness",WS_POPUP,-28000,600,640,300,nullptr,nullptr,instance,nullptr);
    if(!h)return 4;ShowWindow(h,SW_SHOWNOACTIVATE);Redraw(h);Log("START");
    for(;;){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){fclose(logFile);return 0;}TranslateMessage(&m);DispatchMessageW(&m);}
        if(GetAsyncKeyState(VK_INSERT)&0x8000)Log("GAKS_DOWN");
        BYTE keys[256]{};if(GetKeyboardState(keys) && (keys[VK_INSERT]&0x80))Log("GKBS_DOWN");Sleep(5);
    }
}
