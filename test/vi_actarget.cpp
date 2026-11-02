// vi_actarget.cpp - synthetic target for the anti-cheat refusal tests (verify\run_actest.ps1).
// A plain window that can load DLLs in order at start-up and one more DLL when it receives the key 'L':
//   vi_actarget.exe <logfile> [--load <dll>]... [--late <dll>]
// The test loads a harmless dummy DLL named like an anti-cheat module (test\vi_acdummy.cpp) to prove vinput refuses.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

static FILE* g_log = nullptr;
static std::wstring g_late;
static void L(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
        L("WM_KEYDOWN vk=0x%02X", (unsigned)w);
        if (w == 'L' && !g_late.empty()) {
            HMODULE mod = LoadLibraryW(g_late.c_str());
            L("LATE_LOAD %s err=%lu", mod ? "ok" : "failed", mod ? 0ul : GetLastError());
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) g_log = _wfsopen(argv[1], L"w", _SH_DENYNO);   // readable by the test while open
    L("START pid=%lu", GetCurrentProcessId());
    for (int i = 2; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--load" && i + 1 < argc) {
            HMODULE mod = LoadLibraryW(argv[++i]);
            L("LOAD %s err=%lu", mod ? "ok" : "failed", mod ? 0ul : GetLastError());
        } else if (a == L"--late" && i + 1 < argc) g_late = argv[++i];
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_actarget";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput anti-cheat refusal target", WS_OVERLAPPEDWINDOW,
                                -28000, 600, 320, 200, nullptr, nullptr, hi, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    L("WINDOW ready");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (g_log) fclose(g_log);
    return 0;
}
