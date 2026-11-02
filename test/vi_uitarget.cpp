// vi_uitarget.cpp - synthetic game-menu target for vinput UI navigation tests.
// A D3D11 (flip-model) window whose pixels are drawn with GDI into a DIB and uploaded each frame, so vinput's
// native swapchain capture sees real text and buttons. Every click and mouse/char event is logged with coordinates.
//   usage: vi_uitarget.exe <logfile> x y [--load-ms N] [--icon-scale F] [--button-scale F]
// Client area is fixed at 800x450. Screens:
//   LOAD   "Loading ..." for --load-ms, then MAIN
//   MAIN   buttons PLAY, OPTIONS, Load   Game, BATTLE (left), BATTLE (right), QUIT, plus a texture icon (click = ICON)
//   SELECT "Select a game type" with SOLO and BACK   (PLAY opens it)
//   GAME   "Game started"                           (SOLO opens it)
// Log lines: "<tick> CLICK <x> <y> <button name|none>", "SCREEN <name>", "MOVE <x> <y> btn=<0|1>", "UP <x> <y>",
//            "WHEEL <delta> <x> <y>", "CHAR U+XXXX".
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <share.h>
#include <string>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

enum Screen { LOAD, MAIN, SELECT, GAME };
struct Button { const wchar_t* label; RECT r; Screen screen; };
static const int kW = 800, kH = 450;
static FILE* g_log; static HWND g_hwnd; static Screen g_screen = LOAD;
static DWORD g_loadMs = 0, g_start = 0;
static double g_iconScale = 1.0, g_buttonScale = 1.0;
static std::vector<Button> g_buttons;

static void Log(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsprintf_s(b, fmt, ap); va_end(ap);
    fprintf(g_log, "%lu %s\n", GetTickCount(), b); fflush(g_log);
}
static const char* ScreenName(Screen s) { return s == LOAD ? "LOAD" : s == MAIN ? "MAIN" : s == SELECT ? "SELECT" : "GAME"; }
static void SetScreen(Screen s) { if (s != g_screen) { g_screen = s; Log("SCREEN %s", ScreenName(s)); } }

static void BuildButtons() {
    g_buttons.clear();
    auto S = [](int v) { return static_cast<int>(v * g_buttonScale); };
    auto add = [&](const wchar_t* t, int x, int y, int w, int h, Screen s) { g_buttons.push_back({t, {S(x), S(y), S(x + w), S(y + h)}, s}); };
    add(L"PLAY", 60, 110, 200, 60, MAIN);
    add(L"OPTIONS", 60, 190, 200, 60, MAIN);
    add(L"BATTLE", 60, 340, 200, 60, MAIN);
    add(L"BATTLE", 540, 340, 200, 60, MAIN);
    add(L"Load   Game", 300, 110, 200, 60, MAIN);
    add(L"QUIT", 300, 190, 200, 60, MAIN);
    add(L"SOLO", 300, 150, 200, 60, SELECT);
    add(L"BACK", 300, 240, 200, 60, SELECT);
}
static RECT IconRect() { const int s = static_cast<int>(64 * g_iconScale); return {600, 110, 600 + s, 110 + s}; }

// Deterministic 64x64 icon with plenty of structure (rings, diagonals, colour blocks).
static void DrawIcon(HDC dc, const RECT& r) {
    HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, 64, 64); HGDIOBJ old = SelectObject(mem, bmp);
    // 8x8 grid of pseudo-random colour cells: not self-similar, so a wrong-scale copy cannot correlate well.
    unsigned state = 0xC0FFEEu;
    for (int cy = 0; cy < 8; ++cy) for (int cx = 0; cx < 8; ++cx) {
        state = state * 1664525u + 1013904223u;
        HBRUSH b = CreateSolidBrush(RGB(40 + ((state >> 8) & 0xBF), 40 + ((state >> 16) & 0xBF), 40 + ((state >> 24) & 0xBF)));
        RECT q{cx * 8, cy * 8, cx * 8 + 8, cy * 8 + 8}; FillRect(mem, &q, b); DeleteObject(b);
    }
    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255)); HGDIOBJ op = SelectObject(mem, pen);
    MoveToEx(mem, 2, 2, nullptr); LineTo(mem, 62, 40); MoveToEx(mem, 62, 2, nullptr); LineTo(mem, 14, 62);
    SelectObject(mem, op); DeleteObject(pen);
    HBRUSH red = CreateSolidBrush(RGB(220, 20, 20)); RECT dot{40, 40, 54, 52}; FillRect(mem, &dot, red); DeleteObject(red);
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, 64, 64, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
}

static void Text(HDC dc, const wchar_t* t, RECT r, int px, COLORREF col, UINT fmt) {
    HFONT f = CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, col);
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_NOPREFIX); SelectObject(dc, old); DeleteObject(f);
}
static void Paint(HDC dc) {
    HBRUSH bg = CreateSolidBrush(RGB(18, 28, 56)); RECT all{0, 0, kW, kH}; FillRect(dc, &all, bg); DeleteObject(bg);
    if (g_screen == LOAD) { Text(dc, L"Loading game assets", {0, 150, kW, 250}, 40, RGB(255, 255, 255), DT_CENTER | DT_VCENTER); return; }
    if (g_screen == GAME) { Text(dc, L"Game started", {0, 150, kW, 250}, 48, RGB(120, 255, 120), DT_CENTER | DT_VCENTER); return; }
    Text(dc, g_screen == MAIN ? L"MAIN MENU" : L"Select a game type", {0, 20, kW, 80}, 40, RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
    for (const Button& b : g_buttons) {
        if (b.screen != g_screen) continue;
        HBRUSH fill = CreateSolidBrush(RGB(40, 140, 60)); RECT r = b.r; FillRect(dc, &r, fill); DeleteObject(fill);
        Text(dc, b.label, r, static_cast<int>(30 * g_buttonScale), RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
    }
    if (g_screen == MAIN) { RECT ir = IconRect(); DrawIcon(dc, ir); }
}

static const Button* Hit(int x, int y) {
    for (const Button& b : g_buttons) if (b.screen == g_screen && x >= b.r.left && x < b.r.right && y >= b.r.top && y < b.r.bottom) return &b;
    return nullptr;
}
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_LBUTTONDOWN: {
        const int x = (short)LOWORD(l), y = (short)HIWORD(l);
        const Button* b = Hit(x, y);
        std::string name = "none";
        if (b) { name.clear(); for (const wchar_t* p = b->label; *p; ++p) name += (*p == L' ') ? '_' : static_cast<char>(*p); }
        if (!b && g_screen == MAIN) { RECT ir = IconRect(); if (x >= ir.left && x < ir.right && y >= ir.top && y < ir.bottom) name = "ICON"; }
        Log("CLICK %d %d %s", x, y, name.c_str());
        if (b && !wcscmp(b->label, L"PLAY")) SetScreen(SELECT);
        else if (b && !wcscmp(b->label, L"SOLO")) SetScreen(GAME);
        else if (b && !wcscmp(b->label, L"BACK")) SetScreen(MAIN);
        return 0;
    }
    case WM_LBUTTONUP: Log("UP %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_MOUSEMOVE: Log("MOVE %d %d btn=%d", (short)LOWORD(l), (short)HIWORD(l), (w & MK_LBUTTON) ? 1 : 0); return 0;
    case WM_MOUSEWHEEL: { POINT p{(short)LOWORD(l), (short)HIWORD(l)}; ScreenToClient(h, &p); Log("WHEEL %d %ld %ld", (short)HIWORD(w), p.x, p.y); return 0; }
    case WM_CHAR: Log("CHAR U+%04X", (unsigned)w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_log = _wfsopen(argc > 1 ? argv[1] : L"vi_uitarget.log", L"w", _SH_DENYNO);
    int x = argc > 3 ? _wtoi(argv[2]) : 100, y = argc > 3 ? _wtoi(argv[3]) : 100;
    for (int i = 4; i + 1 < argc; ++i) {
        if (!wcscmp(argv[i], L"--load-ms")) g_loadMs = (DWORD)_wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"--icon-scale")) g_iconScale = _wtof(argv[++i]);
        else if (!wcscmp(argv[i], L"--button-scale")) g_buttonScale = _wtof(argv[++i]);
    }
    BuildButtons();
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_uitarget"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rc{0, 0, kW, kH}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);
    g_hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput ui target", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
        x, y, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    DXGI_SWAP_CHAIN_DESC d = {}; d.BufferCount = 2; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow = g_hwnd; d.SampleDesc.Count = 1; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc; ID3D11Device* dev; ID3D11DeviceContext* ctx;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx))) { Log("D3D11 FAILED"); return 1; }
    ID3D11Texture2D* bb; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    RECT cr; GetClientRect(g_hwnd, &cr); Log("START client=%ldx%ld", cr.right, cr.bottom);
    HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), kW, -kH, 1, 32, BI_RGB};
    void* bits = nullptr; HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); SelectObject(mem, dib);
    std::vector<unsigned char> rgba(static_cast<size_t>(kW) * kH * 4u);
    g_start = GetTickCount(); Log("SCREEN LOAD");
    if (!g_loadMs) SetScreen(MAIN);
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) return 0; TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (g_screen == LOAD && GetTickCount() - g_start >= g_loadMs) SetScreen(MAIN);
        Paint(mem); GdiFlush();
        const unsigned char* src = static_cast<const unsigned char*>(bits);
        for (size_t i = 0; i < static_cast<size_t>(kW) * kH; ++i) { rgba[i * 4] = src[i * 4 + 2]; rgba[i * 4 + 1] = src[i * 4 + 1]; rgba[i * 4 + 2] = src[i * 4]; rgba[i * 4 + 3] = 255; }
        ctx->UpdateSubresource(bb, 0, nullptr, rgba.data(), kW * 4, 0);
        sc->Present(1, 0);
        Sleep(16);
    }
}
