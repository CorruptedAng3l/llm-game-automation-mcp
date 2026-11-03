// vi_testinput.cpp - positive-control target for vinput. A D3D11 window that reads input through EVERY
// channel vinput claims to drive and logs each observation to a file:
//   WM_KEYDOWN/UP, WM_CHAR, WM_INPUT keyboard+mouse (raw input registered), GetAsyncKeyState polling,
//   GetKeyState, DirectInput8 keyboard+mouse GetDeviceState, GetCursorPos, WM_MOUSEMOVE/LBUTTON/WHEEL,
//   WM_ACTIVATEAPP, GetForegroundWindow.
// The clear colour encodes the number of key-down observations so a capture can be checked too.
// usage: vi_testinput.exe <logfile> [x y w h] [--hover-gated] [--steal-fg-after-pipe <ms>]
//   --steal-fg-after-pipe: <ms> after vinput's pipe appears, a NON-injected child (vi_testinput --fg-thief <hwnd>)
//   makes this window REALLY foreground from outside (AttachThreadInput + SetForegroundWindow), like a launcher's
//   dialog closing activates a just-launched game. Logs STEAL_FG ... with the thief's result. Touches the real
//   foreground: run it under a --lock file (verify\run_fgguardtest.ps1 does).
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dinput.h>
#include <cstdio>
#include <share.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "shell32.lib")

static FILE* g_log; static HWND g_hwnd; static int g_keydowns = 0;
static bool g_hoverGated = false;
static unsigned g_frames = 0, g_hoverFrame = 0;
static bool g_hovered = false;
static void L(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsprintf_s(b, fmt, ap); va_end(ap);
    fprintf(g_log, "%lu %s\n", GetTickCount(), b); fflush(g_log);
}
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: g_keydowns++; L("WM_KEYDOWN vk=0x%02X sc=0x%02X", (unsigned)w, (unsigned)((l >> 16) & 0xFF)); return 0;
    case WM_KEYUP: case WM_SYSKEYUP: L("WM_KEYUP vk=0x%02X", (unsigned)w); return 0;
    case WM_CHAR: L("WM_CHAR %u '%c'", (unsigned)w, (w >= 32 && w < 127) ? (char)w : '?'); return 0;
    case WM_MOUSEMOVE:
        g_hoverFrame = g_frames; g_hovered = true;
        L("WM_MOUSEMOVE %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_LBUTTONDOWN: {
        L("WM_LBUTTONDOWN %d %d", (short)LOWORD(l), (short)HIWORD(l));
        if (g_hoverGated) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(h, &p);
            bool ok = g_hovered && g_frames - g_hoverFrame >= 3 &&
                p.x == (short)LOWORD(l) && p.y == (short)HIWORD(l);
            L("CLICK_%s hover_frames=%u cursor=%ld,%ld", ok ? "ACCEPT" : "REJECT", g_frames - g_hoverFrame, p.x, p.y);
        }
        return 0;
    }
    case WM_LBUTTONUP: L("WM_LBUTTONUP %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_RBUTTONDOWN: L("WM_RBUTTONDOWN %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_MOUSEWHEEL: L("WM_MOUSEWHEEL %d", (short)HIWORD(w)); return 0;
    case WM_ACTIVATEAPP: L("WM_ACTIVATEAPP %d", (int)w); break;
    case WM_KILLFOCUS: L("WM_KILLFOCUS"); break;
    case WM_INPUT: {
        RAWINPUT ri; UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
            if (ri.header.dwType == RIM_TYPEKEYBOARD) L("RAW_KBD vk=0x%02X %s sink=%d", ri.data.keyboard.VKey, (ri.data.keyboard.Flags & RI_KEY_BREAK) ? "up" : "down", (int)w);
            else if (ri.header.dwType == RIM_TYPEMOUSE) L("RAW_MOUSE dx=%ld dy=%ld btn=0x%X sink=%d", ri.data.mouse.lLastX, ri.data.mouse.lLastY, ri.data.mouse.usButtonFlags, (int)w);
        }
        break;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// External foreground thief (never injected): exit 0 = the target really was foreground right after the call.
static int FgThief(HWND target) {
    // An out-of-process OS witness observes even a gain that the guard restores before our next snapshot.
    // Keep this state inside the helper process; nothing here reads an injected counter or invents a gain.
    static HWND watched = nullptr; static LONG foregroundEvents = 0;
    watched = target; foregroundEvents = 0;
    bool observeOnly = false; wchar_t witnessLog[MAX_PATH] = {};
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 3; argv && i < argc; ++i) {
        if (!wcscmp(argv[i], L"--observe-only")) observeOnly = true;
        else if (!wcscmp(argv[i], L"--witness-log") && i + 1 < argc) wcsncpy_s(witnessLog, argv[++i], _TRUNCATE);
    }
    if (argv) LocalFree(argv);
    MSG queue; PeekMessageW(&queue, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetLastError(ERROR_SUCCESS);
    HWINEVENTHOOK witness = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
        [](HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD, DWORD) {
            if (event == EVENT_SYSTEM_FOREGROUND && window == watched) InterlockedIncrement(&foregroundEvents);
        }, 0, 0, WINEVENT_OUTOFCONTEXT);
    DWORD hookError = witness ? ERROR_SUCCESS : GetLastError();
    HWND fg = GetForegroundWindow(); DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    BOOL attached = !observeOnly && fgThread && fgThread != me ? AttachThreadInput(me, fgThread, TRUE) : FALSE;
    BOOL set = FALSE;
    if (!observeOnly) { set = SetForegroundWindow(target); BringWindowToTop(target); }
    bool was = GetForegroundWindow() == target;
    if (attached) AttachThreadInput(me, fgThread, FALSE);
    // Out-of-context WinEvent callbacks are delivered on this thread's message queue. Detach the human's
    // queue BEFORE pumping it, and bound this independent observation window to 100 ms.
    DWORD start = GetTickCount();
    do {
        for (int i = 0; i < 256 && GetTickCount() - start < 100 && PeekMessageW(&queue, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&queue); DispatchMessageW(&queue); }
        Sleep(1);
    } while (GetTickCount() - start < 100);
    if (witness) UnhookWinEvent(witness);
    if (witnessLog[0]) {
        FILE* f = _wfsopen(witnessLog, L"w", _SH_DENYNO);
        if (f) {
            fprintf(f, "observe_only=%d hook=%d hook_error=%lu attached=%d set_return=%d direct_snapshot=%d target_foreground_events=%ld target=0x%llX\n",
                (int)observeOnly, witness != nullptr, hookError, (int)attached, (int)set, (int)was, foregroundEvents, (unsigned long long)(ULONG_PTR)target);
            fclose(f);
        }
    }
    return was || foregroundEvents > 0 ? 0 : set ? 2 : 1; // TRUE from SetForegroundWindow alone is not positive proof.
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 2 && !wcscmp(argv[1], L"--fg-thief")) return FgThief((HWND)(ULONG_PTR)wcstoull(argv[2], nullptr, 0));
    int stealAfterPipe = -1;
    g_log = _wfsopen(argc > 1 ? argv[1] : L"vi_testinput.log", L"w", _SH_DENYNO);
    int x = argc > 5 ? _wtoi(argv[2]) : 100, y = argc > 5 ? _wtoi(argv[3]) : 100, w = argc > 5 ? _wtoi(argv[4]) : 640, hgt = argc > 5 ? _wtoi(argv[5]) : 360;
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_testinput"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    for (int i = 6; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--hover-gated")) g_hoverGated = true;
        else if (!wcscmp(argv[i], L"--steal-fg-after-pipe") && i + 1 < argc) stealAfterPipe = _wtoi(argv[++i]);
    }
    wchar_t pipeName[64]; swprintf_s(pipeName, L"\\\\.\\pipe\\vinput_%lu", GetCurrentProcessId());
    DWORD pipeSeenAt = 0, lastPipeCheck = 0, thiefPid = 0; bool stolen = false; HANDLE thief = nullptr;
    // Synthetic tests never take the human's foreground, including before injection.
    g_hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput test target", WS_OVERLAPPEDWINDOW,
        x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    RAWINPUTDEVICE rd[2] = {{1, 6, 0, g_hwnd}, {1, 2, 0, g_hwnd}};
    RegisterRawInputDevices(rd, 2, sizeof(RAWINPUTDEVICE));
    DXGI_SWAP_CHAIN_DESC d = {}; d.BufferCount = 2; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow = g_hwnd; d.SampleDesc.Count = 1; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc; ID3D11Device* dev; ID3D11DeviceContext* ctx;
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx);
    ID3D11Texture2D* bb; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb); ID3D11RenderTargetView* rtv; dev->CreateRenderTargetView(bb, nullptr, &rtv); bb->Release();
    IDirectInput8W* di = nullptr; IDirectInputDevice8W* kb = nullptr; IDirectInputDevice8W* ms = nullptr;
    DirectInput8Create(hi, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr);
    di->CreateDevice(GUID_SysKeyboard, &kb, nullptr); kb->SetDataFormat(&c_dfDIKeyboard); kb->SetCooperativeLevel(g_hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    di->CreateDevice(GUID_SysMouse, &ms, nullptr); ms->SetDataFormat(&c_dfDIMouse2); ms->SetCooperativeLevel(g_hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    L("START hwnd=%p", g_hwnd);
    BYTE prevG[256] = {0}, prevD[256] = {0}; POINT prevC = {-1, -1}; HWND prevFg = (HWND)1; int prevMB = 0;
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) return 0; TranslateMessage(&msg); DispatchMessageW(&msg); }
        for (int vk = 1; vk < 256; vk++) {
            BYTE dn = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
            if (dn != prevG[vk]) { L("GAKS vk=0x%02X %s", vk, dn ? "down" : "up"); prevG[vk] = dn; }
        }
        BYTE ks[256];
        HRESULT hr = kb->GetDeviceState(256, ks);
        if (FAILED(hr)) { kb->Acquire(); hr = kb->GetDeviceState(256, ks); }
        if (SUCCEEDED(hr)) for (int i = 0; i < 256; i++) { BYTE dn = (ks[i] & 0x80) ? 1 : 0; if (dn != prevD[i]) { L("DI_KBD dik=0x%02X %s", i, dn ? "down" : "up"); prevD[i] = dn; } }
        DIMOUSESTATE2 mst; hr = ms->GetDeviceState(sizeof(mst), &mst);
        if (FAILED(hr)) { ms->Acquire(); hr = ms->GetDeviceState(sizeof(mst), &mst); }
        if (SUCCEEDED(hr)) { if (mst.lX || mst.lY || mst.lZ) L("DI_MOUSE dx=%ld dy=%ld dz=%ld", mst.lX, mst.lY, mst.lZ); int mb = mst.rgbButtons[0] ? 1 : 0; if (mb != prevMB) { L("DI_MOUSE b0=%d", mb); prevMB = mb; } }
        POINT c; GetCursorPos(&c); ScreenToClient(g_hwnd, &c);
        if (c.x != prevC.x || c.y != prevC.y) { L("CURSOR %ld %ld", c.x, c.y); prevC = c; }
        if (stealAfterPipe >= 0 && !stolen) {
            DWORD now = GetTickCount();
            if (!pipeSeenAt && now - lastPipeCheck >= 50) { lastPipeCheck = now; if (WaitNamedPipeW(pipeName, 1) || GetLastError() == ERROR_SEM_TIMEOUT) { pipeSeenAt = now; L("STEAL_FG pipe seen, stealing in %d ms", stealAfterPipe); } }
            if (pipeSeenAt && now - pipeSeenAt >= (DWORD)stealAfterPipe) {
                stolen = true;
                wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
                wchar_t cl[MAX_PATH + 64]; swprintf_s(cl, L"\"%s\" --fg-thief 0x%llX", self, (unsigned long long)(ULONG_PTR)g_hwnd);
                STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi = {};
                // never block this (window) thread on the thief: its SetForegroundWindow waits for this thread
                if (CreateProcessW(self, cl, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { thief = pi.hProcess; thiefPid = pi.dwProcessId; CloseHandle(pi.hThread); L("STEAL_FG thief started pid=%lu", thiefPid); }
                else L("STEAL_FG CreateProcess failed %lu", GetLastError());
            }
        }
        if (thief && WaitForSingleObject(thief, 0) == WAIT_OBJECT_0) {
            DWORD code = 99; GetExitCodeProcess(thief, &code); CloseHandle(thief); thief = nullptr;
            L("STEAL_FG thief_pid=%lu thief_exit=%lu (0 = target was really foreground)", thiefPid, code);
        }
        HWND fg = GetForegroundWindow(); if (fg != prevFg) { L("FG %s", fg == g_hwnd ? "self" : "other"); prevFg = fg; }
        float col[4] = {(g_keydowns % 4) / 3.0f, ((g_keydowns / 4) % 4) / 3.0f, 0.25f, 1};
        ctx->OMSetRenderTargets(1, &rtv, nullptr); ctx->ClearRenderTargetView(rtv, col);
        ++g_frames;
        sc->Present(1, 0);
        Sleep(16); // bounded render rate even for an occluded offscreen target
    }
}
