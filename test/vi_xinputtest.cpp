// vi_xinputtest.cpp - synthetic XInput reader for verify\run_xinputtest.ps1.
// Polls pad 0 every ~2 ms through XInputGetState (and, with --ex, XInputGetStateEx = ordinal 100) of the DLL named by
// --dll (default xinput1_4.dll), and logs every change of the result code or of the gamepad fields:
//   STATE src=<std|ex> rc=<code> pkt=<n> b=0x<hex> lt=<n> rt=<n> lx=<n> ly=<n> rx=<n> ry=<n>
// It owns a small window (vinput wants a main window) placed where the script says, never activated.
//   vi_xinputtest.exe <log> x y w h [--dll name] [--ex]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <share.h>
#include <shellapi.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

struct XGP { WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XST { DWORD pkt; XGP gp; };
using PFN_XGS = DWORD(WINAPI*)(DWORD, XST*);

static FILE* g_log = nullptr;
static void Log(const char* f, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

struct Src { const char* name; PFN_XGS fn; DWORD rc; XST st; bool first; };

static void Poll(Src& s) {
    if (!s.fn) return;
    XST st = {};
    DWORD rc = s.fn(0, &st);
    if (rc != ERROR_SUCCESS) memset(&st, 0, sizeof(st));
    bool changed = s.first || rc != s.rc || memcmp(&st.gp, &s.st.gp, sizeof(st.gp)) != 0;
    if (!changed) return;
    s.first = false; s.rc = rc; s.st = st;
    Log("STATE src=%s rc=%lu pkt=%lu b=0x%04X lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d", s.name, rc, st.pkt, st.gp.b,
        st.gp.lt, st.gp.rt, st.gp.lx, st.gp.ly, st.gp.rx, st.gp.ry);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(GRAY_BRUSH)); EndPaint(h, &ps); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 6) return 2;
    g_log = _wfsopen(argv[1], L"w", _SH_DENYNO);   // the test script reads it while we run
    int x = _wtoi(argv[2]), y = _wtoi(argv[3]), w = _wtoi(argv[4]), hgt = _wtoi(argv[5]);
    const wchar_t* dll = L"xinput1_4.dll"; bool ex = false;
    for (int i = 6; i < argc; i++) {
        if (!wcscmp(argv[i], L"--dll") && i + 1 < argc) dll = argv[++i];
        else if (!wcscmp(argv[i], L"--ex")) ex = true;
    }
    HMODULE h = LoadLibraryW(dll);
    if (!h) { Log("FAIL LoadLibrary %ls err=%lu", dll, GetLastError()); return 3; }
    Src std_ = {"std", (PFN_XGS)GetProcAddress(h, "XInputGetState"), 0, {}, true};
    Src exs = {"ex", ex ? (PFN_XGS)GetProcAddress(h, MAKEINTRESOURCEA(100)) : nullptr, 0, {}, true};
    if (!std_.fn || (ex && !exs.fn)) { Log("FAIL GetProcAddress std=%p ex=%p", (void*)std_.fn, (void*)exs.fn); return 3; }
    Log("READY dll=%ls ex=%d pid=%lu", dll, ex ? 1 : 0, GetCurrentProcessId());

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"vi_xinputtest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, L"vi_xinputtest", WS_OVERLAPPEDWINDOW, x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    ShowWindow(hw, SW_SHOWNOACTIVATE);
    timeBeginPeriod(1);
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) { timeEndPeriod(1); return 0; } TranslateMessage(&m); DispatchMessageW(&m); }
        Poll(std_); Poll(exs);
        Sleep(2);
    }
}
