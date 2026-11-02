// vi_overlaytarget.cpp - synthetic target that behaves like a DDraw wrapper (DDrawCompat, dgVoodoo...): the game's own
// window (client 320x240) holds only a stale yellow square on black and the picture lives in a SEPARATE top-level window of class
// "DDrawCompatPresentationWindow" (800x480) that shows the game scaled 2x, aspect-fitted and centred (x offset 80).
// Picture (in game client pixels): left half red, right half blue, a 40x40 green square at the top-left corner.
// usage: vi_overlaytarget.exe [x y]   (default -28000 600: off screen)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
static LRESULT CALLBACK GameWnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r); FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); RECT st{ 140, 100, 180, 140 }; HBRUSH y = CreateSolidBrush(RGB(230, 230, 0)); FillRect(dc, &st, y); DeleteObject(y); EndPaint(h, &ps); return 0; }   // a stale leftover frame (like a finished intro video): the game window is NOT blank
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
static void DrawPicture(HDC dc, HWND h) {
    RECT r; GetClientRect(h, &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    HBRUSH red = CreateSolidBrush(RGB(220, 20, 20)), blue = CreateSolidBrush(RGB(20, 20, 220)), green = CreateSolidBrush(RGB(20, 220, 20));
    RECT a{ 80, 0, 80 + 320, 480 }, b{ 80 + 320, 0, 80 + 640, 480 }, g{ 80, 0, 80 + 80, 80 };
    FillRect(dc, &a, red); FillRect(dc, &b, blue); FillRect(dc, &g, green);
    DeleteObject(red); DeleteObject(blue); DeleteObject(green);
}
static LRESULT CALLBACK PresWnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PRINTCLIENT) { DrawPicture((HDC)w, h); GdiFlush(); return 0; }   // GdiFlush: the caller reads the DIB from another thread   // what PrintWindow asks a plain GDI window for
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); DrawPicture(dc, h); EndPaint(h, &ps); return 0; }
    return DefWindowProcW(h, m, w, l);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    int x = -28000, y = 600;
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3) { x = _wtoi(argv[1]); y = _wtoi(argv[2]); }
    WNDCLASSW wc{}; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpfnWndProc = GameWnd; wc.lpszClassName = L"OverlayGame"; RegisterClassW(&wc);
    wc.lpfnWndProc = PresWnd; wc.lpszClassName = L"DDrawCompatPresentationWindow"; RegisterClassW(&wc);
    RECT r{ 0, 0, 320, 240 }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND game = CreateWindowExW(WS_EX_NOACTIVATE, L"OverlayGame", L"vi overlay game", WS_OVERLAPPEDWINDOW, x, y, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr);
    HWND pres = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"DDrawCompatPresentationWindow", L"", WS_POPUP, x, y + 400, 800, 480, nullptr, nullptr, hi, nullptr);
    ShowWindow(game, SW_SHOWNOACTIVATE); ShowWindow(pres, SW_SHOWNOACTIVATE);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
