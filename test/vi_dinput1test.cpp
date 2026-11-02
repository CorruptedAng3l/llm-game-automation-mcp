// Synthetic target for the LEGACY DirectInput path (dinput.dll, DirectInputCreateA, DirectInput 1-7), which 1990s games
// such as Fallout still use. vinput's dinput8 hooks never see these devices.
// usage: vi_dinput1test.exe <log> [--buffered] [--gated]
// --gated: behave like Fallout 1/2's GNW95 loop - a window that never activates itself (no-activate, off-screen) and
//          does not poll input at all until its window procedure has received WM_ACTIVATEAPP(TRUE) (logs ACTIVATEAPP 0|1).
// Polls the keyboard (GetDeviceState, or buffered GetDeviceData with --buffered) and the mouse every ~2 ms and logs
//   <hh:mm:ss.mmm> KEY dik=0xNN down|up       <hh:mm:ss.mmm> MOUSE b0 down|up       <hh:mm:ss.mmm> READY
#define DIRECTINPUT_VERSION 0x0300
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#pragma comment(lib, "dinput8.lib")   // only for c_dfDI* and the GUIDs; the target talks to dinput.dll
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")

static FILE* g_log;
static void Out(const char* fmt, ...) {
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}
static bool g_active = false;
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_ACTIVATEAPP) { g_active = w != 0; Out("ACTIVATEAPP %d", (int)(w != 0)); return 0; }
    return DefWindowProcA(h, m, w, l);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR cmd, int) {
    char path[MAX_PATH] = {}; bool buffered = false, gated = false;
    { char* c = cmd; while (*c == ' ') c++; bool q = *c == '"'; if (q) c++; char* e = c; while (*e && (q ? *e != '"' : *e != ' ')) e++;
      memcpy(path, c, (size_t)(e - c)); buffered = strstr(e, "--buffered") != nullptr; gated = strstr(e, "--gated") != nullptr; }
    g_log = fopen(path, "w"); if (!g_log) return 2;
    WNDCLASSA wc = {}; wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = "vi_dinput1test"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);
    HWND w = gated ? CreateWindowExA(WS_EX_NOACTIVATE, wc.lpszClassName, "vinput dinput1 test", WS_OVERLAPPEDWINDOW, -28000, 600, 404, 260, nullptr, nullptr, hi, nullptr)
                   : CreateWindowA(wc.lpszClassName, "vinput dinput1 test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 404, 260, nullptr, nullptr, hi, nullptr);
    if (gated) ShowWindow(w, SW_SHOWNOACTIVATE);
    LPDIRECTINPUTA di = nullptr; LPDIRECTINPUTDEVICEA kb = nullptr, ms = nullptr;
    using PFN_Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
    HMODULE dl = LoadLibraryA("dinput.dll");
    PFN_Create create = dl ? (PFN_Create)GetProcAddress(dl, "DirectInputCreateA") : nullptr;
    if (!create || FAILED(create(hi, 0x0300, &di, nullptr))) { Out("ERROR DirectInputCreateA"); return 3; }
    if (FAILED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr)) || FAILED(di->CreateDevice(GUID_SysMouse, &ms, nullptr))) { Out("ERROR CreateDevice"); return 4; }
    kb->SetDataFormat(&c_dfDIKeyboard); ms->SetDataFormat(&c_dfDIMouse);
    kb->SetCooperativeLevel(w, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND); ms->SetCooperativeLevel(w, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND);
    if (buffered) { DIPROPDWORD p = {{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, 32}; kb->SetProperty(DIPROP_BUFFERSIZE, &p.diph); }
    kb->Acquire(); ms->Acquire();
    Out("READY mode=%s%s", buffered ? "buffered" : "state", gated ? " gated" : "");
    BYTE prev[256] = {}, cur[256]; BYTE pb = 0;
    MSG m;
    for (;;) {
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) return 0; TranslateMessage(&m); DispatchMessageA(&m); }
        if (gated && !g_active) { Sleep(2); continue; }   // GNW95: an inactive program does not read its devices
        if (buffered) {
            DIDEVICEOBJECTDATA d[32]; DWORD n = 32;
            if (SUCCEEDED(kb->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), d, &n, 0))) for (DWORD i = 0; i < n; i++) Out("KEY dik=0x%02X %s", d[i].dwOfs, (d[i].dwData & 0x80) ? "down" : "up");
        } else if (SUCCEEDED(kb->GetDeviceState(256, cur))) {
            for (int i = 0; i < 256; i++) if ((cur[i] & 0x80) != (prev[i] & 0x80)) Out("KEY dik=0x%02X %s", i, (cur[i] & 0x80) ? "down" : "up");
            memcpy(prev, cur, 256);
        }
        DIMOUSESTATE s = {};
        if (SUCCEEDED(ms->GetDeviceState(sizeof s, &s))) { BYTE b = s.rgbButtons[0] & 0x80; if (b != pb) Out("MOUSE b0 %s", b ? "down" : "up"); pb = b; }
        Sleep(2);
    }
}
