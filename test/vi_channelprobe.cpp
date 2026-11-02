// Synthetic vinput channel observer. This target never requests foreground or sends OS input by default.
// Usage: vi_channelprobe.exe <absolute-log-path>
// WM_APP+1: polling enabled=(wParam!=0); +2: cursor-recenter enabled; +3: post invalid WM_INPUT;
// WM_APP+4: guarded target-side SendInput probe; +5: add a chaining overlay WndProc; +6: raw-buffer polling.
// WM_APP+7: remove overlay; +8: quit. Guarded SendInput requires an actual vinput status reply proving
// redirect_sendinput=true, really_foreground=false, and a SendInput hook. Missing support => REFUSED.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <shellapi.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <share.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
typedef unsigned __int64 QWORD; // NEXTRAWINPUTBLOCK on Windows SDK with WIN32_LEAN_AND_MEAN.

namespace {
FILE* g_log = nullptr;
HWND g_window = nullptr;
bool g_poll = true, g_recenter = false, g_rawBuffer = false;
WNDPROC g_overlayPrevious = nullptr;
unsigned g_clicks = 0;
constexpr LPARAM kExternalRaw = 0x49340001;

void Log(const char* fmt, ...) {
    char line[1024];
    va_list ap; va_start(ap, fmt); vsprintf_s(line, fmt, ap); va_end(ap);
    if (g_log) { fprintf(g_log, "%llu %s\n", GetTickCount64(), line); fflush(g_log); }
}

bool StatusAllowsSendInput() {
    wchar_t path[80]; swprintf_s(path, L"\\\\.\\pipe\\vinput_%lu", GetCurrentProcessId());
    HANDLE pipe = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) { Log("SENDINPUT_REFUSED no_vinput_pipe"); return false; }
    const char request[] = "{\"cmd\":\"status\"}\n";
    DWORD written = 0;
    bool ok = WriteFile(pipe, request, sizeof(request) - 1, &written, nullptr) != FALSE;
    std::string reply;
    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (ok && GetTickCount64() < deadline && reply.size() < 65536) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
        if (!available) { Sleep(2); continue; }
        char buffer[4096]; DWORD got = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) || !got) break;
        reply.append(buffer, got);
        if (reply.find('\n') != std::string::npos) break;
    }
    CloseHandle(pipe);
    const bool enabled = reply.find("\"redirect_sendinput\":true") != std::string::npos;
    const bool background = reply.find("\"really_foreground\":false") != std::string::npos;
    const auto hookStart = reply.find("\"hooks_ok\":\"");
    const auto hookEnd = hookStart == std::string::npos ? hookStart : reply.find('"', hookStart + 12);
    const bool hooked = hookStart != std::string::npos && hookEnd != std::string::npos &&
        reply.substr(hookStart, hookEnd - hookStart).find("SendInput") != std::string::npos;
    if (!enabled || !background || !hooked) {
        Log("SENDINPUT_REFUSED enabled=%d background=%d hooked=%d", enabled, background, hooked);
        return false;
    }
    return true;
}

void GuardedSendInput() {
    if (!StatusAllowsSendInput()) return;
    INPUT input[3]{};
    input[0].type = INPUT_MOUSE; input[0].mi.dx = 19; input[0].mi.dy = -11; input[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    input[1].type = INPUT_KEYBOARD; input[1].ki.wVk = VK_F8;
    input[2].type = INPUT_KEYBOARD; input[2].ki.wVk = VK_F8; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(3, input, sizeof(INPUT));
    Log("TARGET_SENDINPUT sent=%u expected=3 dx=19 dy=-11 key=F8", sent);
}

void DescribeRaw(const RAWINPUT& raw, const char* source) {
    if (raw.header.dwType == RIM_TYPEKEYBOARD)
        Log("%s_KBD vk=0x%02X flags=0x%X", source, raw.data.keyboard.VKey, raw.data.keyboard.Flags);
    if (raw.header.dwType == RIM_TYPEMOUSE)
        Log("%s_MOUSE dx=%ld dy=%ld buttons=0x%X", source, raw.data.mouse.lLastX, raw.data.mouse.lLastY, raw.data.mouse.usButtonFlags);
}

LRESULT CALLBACK Overlay(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) {
        POINT p{}; GetCursorPos(&p); ScreenToClient(h, &p);
        Log("OVERLAY msg=0x%X lp=%d,%d cursor=%ld,%ld", m, (short)LOWORD(l), (short)HIWORD(l), p.x, p.y);
    }
    return CallWindowProcW(g_overlayPrevious, h, m, w, l);
}

LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_APP + 1: g_poll = w != 0; Log("POLLING %s", g_poll ? "on" : "off"); return 0;
    case WM_APP + 2: g_recenter = w != 0; Log("RECENTER %s", g_recenter ? "on" : "off"); return 0;
    case WM_APP + 3: Log("EXTERNAL_RAW_POST handle=0x%llX", (unsigned long long)kExternalRaw); PostMessageW(h, WM_INPUT, RIM_INPUT, kExternalRaw); return 0;
    case WM_APP + 4: GuardedSendInput(); return 0;
    case WM_APP + 5:
        if (!g_overlayPrevious) { g_overlayPrevious = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)Overlay); Log("OVERLAY_INSTALLED prev=%p", g_overlayPrevious); }
        return 0;
    case WM_APP + 6: g_rawBuffer = w != 0; Log("RAW_BUFFER %s", g_rawBuffer ? "on" : "off"); return 0;
    case WM_APP + 7:
        if (g_overlayPrevious && (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC) == Overlay) {
            const WNDPROC previous = g_overlayPrevious;
            SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)previous); g_overlayPrevious = nullptr;
            Log("OVERLAY_REMOVED");
        }
        return 0;
    case WM_APP + 8: DestroyWindow(h); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
        Log("WM_KEY msg=0x%X vk=0x%02X sc=0x%02X", m, (unsigned)w, (unsigned)((l >> 16) & 0xff)); return 0;
    case WM_CHAR: Log("WM_CHAR u16=0x%04X", (unsigned)w); return 0;
    case WM_MOUSEMOVE: Log("WM_MOUSEMOVE %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_LBUTTONDOWN: {
        POINT p{}; GetCursorPos(&p); ScreenToClient(h, &p);
        Log("WM_LBUTTONDOWN lp=%d,%d cursor=%ld,%ld", (short)LOWORD(l), (short)HIWORD(l), p.x, p.y);
        ++g_clicks; InvalidateRect(h, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONUP: Log("WM_LBUTTONUP %d %d", (short)LOWORD(l), (short)HIWORD(l)); return 0;
    case WM_MOUSEWHEEL: Log("WM_WHEEL %d", (short)HIWORD(w)); return 0;
    case WM_INPUT: {
        if (l == kExternalRaw) { Log("EXTERNAL_RAW_REACHED_APP"); return 0; }
        RAWINPUT raw{}; UINT bytes = sizeof(raw);
        const UINT result = GetRawInputData((HRAWINPUT)l, RID_INPUT, &raw, &bytes, sizeof(RAWINPUTHEADER));
        if (result == (UINT)-1) Log("RAW_ERROR %lu", GetLastError()); else DescribeRaw(raw, "RAW");
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(h, &paint);
        RECT client{}; GetClientRect(h, &client); FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetTextColor(dc, RGB(0, 0, 0)); SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 25, 25, L"Load Game", 9); TextOutW(dc, 25, 75, L"New Game", 8);
        wchar_t count[80]; swprintf_s(count, L"Clicks: %u", g_clicks); TextOutW(dc, 25, 130, count, (int)wcslen(count));
        EndPaint(h, &paint); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2) return 2;
    g_log = _wfsopen(argv[1], L"w", _SH_DENYNO); LocalFree(argv);
    if (!g_log) return 3;
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = instance; cls.lpszClassName = L"vi_channelprobe";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&cls);
    g_window = CreateWindowExW(WS_EX_NOACTIVATE, cls.lpszClassName, L"vinput channel probe", WS_OVERLAPPEDWINDOW,
        -28000, 600, 480, 300, nullptr, nullptr, instance, nullptr);
    if (!g_window) { fclose(g_log); return 4; }
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    RAWINPUTDEVICE devices[2] = {{1, 6, 0, g_window}, {1, 2, 0, g_window}};
    RegisterRawInputDevices(devices, 2, sizeof(devices[0]));
    IDirectInput8W* di = nullptr; IDirectInputDevice8W* keyboard = nullptr; IDirectInputDevice8W* mouse = nullptr;
    if (SUCCEEDED(DirectInput8Create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr))) {
        di->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr); di->CreateDevice(GUID_SysMouse, &mouse, nullptr);
        DIPROPDWORD buffer{}; buffer.diph.dwSize = sizeof(buffer); buffer.diph.dwHeaderSize = sizeof(buffer.diph);
        buffer.diph.dwHow = DIPH_DEVICE; buffer.dwData = 128;
        if (keyboard) { keyboard->SetDataFormat(&c_dfDIKeyboard); keyboard->SetCooperativeLevel(g_window, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE); keyboard->SetProperty(DIPROP_BUFFERSIZE, &buffer.diph); }
        if (mouse) { mouse->SetDataFormat(&c_dfDIMouse2); mouse->SetCooperativeLevel(g_window, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE); mouse->SetProperty(DIPROP_BUFFERSIZE, &buffer.diph); }
    }
    Log("START pid=%lu hwnd=%p noactivate=1", GetCurrentProcessId(), g_window);
    USHORT prevAsync[256]{}; SHORT prevState[256]{}; BYTE prevKeyboard[256]{}, prevDI[256]{};
    POINT prevCursor{-100000, -100000};
    bool running = true;
    while (running) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!running) break;
        if (g_poll) {
            for (int vk = 1; vk < 256; ++vk) {
                const USHORT async = (USHORT)GetAsyncKeyState(vk) & 0x8000;
                const SHORT state = GetKeyState(vk);
                if (async != prevAsync[vk]) { Log("GAKS vk=0x%02X value=0x%04X", vk, (unsigned)(USHORT)async); prevAsync[vk] = async; }
                if (state != prevState[vk]) { Log("GKS vk=0x%02X value=0x%04X", vk, (unsigned)(USHORT)state); prevState[vk] = state; }
            }
            BYTE keyboardState[256]{};
            if (GetKeyboardState(keyboardState)) for (int vk = 1; vk < 256; ++vk) {
                if (keyboardState[vk] != prevKeyboard[vk]) { Log("GKBS vk=0x%02X value=0x%02X", vk, keyboardState[vk]); prevKeyboard[vk] = keyboardState[vk]; }
            }
            if (keyboard) {
                BYTE keys[256]{}; HRESULT hr = keyboard->GetDeviceState(sizeof(keys), keys);
                if (FAILED(hr)) { keyboard->Acquire(); hr = keyboard->GetDeviceState(sizeof(keys), keys); }
                if (SUCCEEDED(hr)) for (unsigned dik = 0; dik < 256; ++dik) {
                    if (keys[dik] != prevDI[dik]) { Log("DI_STATE dik=0x%02X value=0x%02X", dik, keys[dik]); prevDI[dik] = keys[dik]; }
                }
                DIDEVICEOBJECTDATA data[128]{}; DWORD n = 128;
                if (SUCCEEDED(keyboard->GetDeviceData(sizeof(data[0]), data, &n, 0))) for (DWORD i = 0; i < n; ++i)
                    Log("DI_BUFFER dik=0x%02lX value=0x%02lX", data[i].dwOfs, data[i].dwData);
            }
            if (mouse) {
                DIMOUSESTATE2 state{}; HRESULT hr = mouse->GetDeviceState(sizeof(state), &state);
                if (FAILED(hr)) { mouse->Acquire(); hr = mouse->GetDeviceState(sizeof(state), &state); }
                if (SUCCEEDED(hr) && (state.lX || state.lY || state.lZ)) Log("DI_MOUSE dx=%ld dy=%ld wheel=%ld", state.lX, state.lY, state.lZ);
            }
        }
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            ScreenToClient(g_window, &cursor);
            if (cursor.x != prevCursor.x || cursor.y != prevCursor.y) { Log("CURSOR %ld %ld", cursor.x, cursor.y); prevCursor = cursor; }
            if (g_recenter) {
                RECT client{}; GetClientRect(g_window, &client);
                POINT center{client.right / 2, client.bottom / 2};
                if (cursor.x != center.x || cursor.y != center.y) Log("CURSOR_LOOK dx=%ld dy=%ld", cursor.x - center.x, cursor.y - center.y);
                ClientToScreen(g_window, &center); SetCursorPos(center.x, center.y);
            }
        }
        if (g_rawBuffer) {
            alignas(16) BYTE buffer[16384]{}; UINT bytes = sizeof(buffer);
            const UINT count = GetRawInputBuffer((PRAWINPUT)buffer, &bytes, sizeof(RAWINPUTHEADER));
            if (count != (UINT)-1) {
                PRAWINPUT raw = (PRAWINPUT)buffer;
                for (UINT i = 0; i < count && (BYTE*)raw + sizeof(RAWINPUTHEADER) <= buffer + sizeof(buffer); ++i) {
                    DescribeRaw(*raw, "RAW_BUFFER"); raw = NEXTRAWINPUTBLOCK(raw);
                }
            }
        }
        Sleep(10);
    }
    if (keyboard) keyboard->Release(); if (mouse) mouse->Release(); if (di) di->Release();
    Log("STOP"); fclose(g_log); return 0;
}

