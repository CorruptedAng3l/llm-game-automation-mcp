// Standard Windows message-pump witness. No product translator is called directly.
// Scoped safety header provenance is documented in vi_char_spies.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <share.h>
#include "vi_char_spies.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
namespace {
constexpr UINT kBegin = WM_APP + 0x70, kSnapshot = WM_APP + 0x71, kPause = WM_APP + 0x72, kQuit = WM_APP + 0x73;
FILE* logFile = nullptr; HWND mainWindow = nullptr; HANDLE resumeEvent = nullptr;
bool pumpA = false; unsigned long long token = 0, ordinal = 0;
std::vector<unsigned> chars, syschars; unsigned keyDown = 0, keyUp = 0;
void Log(const char* format, ...) { if (!logFile) return; va_list args; va_start(args, format); vfprintf(logFile, format, args); va_end(args); fputc('\n', logFile); fflush(logFile); }
std::string Numbers(const std::vector<unsigned>& values) { std::string out = "["; for (size_t n = 0; n < values.size(); ++n) { if (n) out += ','; out += std::to_string(values[n]); } return out + ']'; }
void Snapshot(unsigned long long request) {
    Log("{\"kind\":\"snapshot\",\"token\":%llu,\"case_token\":%llu,\"chars\":%s,\"syschars\":%s,\"key_down\":%u,\"key_up\":%u,\"ordinal\":%llu,\"absorbed_writes\":%llu,\"layout_reads\":%llu}", request, token, Numbers(chars).c_str(), Numbers(syschars).c_str(), keyDown, keyUp, ordinal, char_spy::writes.load(), char_spy::layoutReads.load());
}
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == kBegin) { token = static_cast<unsigned long long>(w); chars.clear(); syschars.clear(); keyDown = keyUp = 0; Log("{\"kind\":\"begin\",\"token\":%llu}", token); return 0; }
    if (message == kSnapshot) { Snapshot(static_cast<unsigned long long>(w)); return 0; }
    if (message == kPause) {
        const BOOL reset = ResetEvent(resumeEvent); Log("{\"kind\":\"paused\",\"token\":%llu,\"ok\":%s}", static_cast<unsigned long long>(w), reset ? "true" : "false");
        const DWORD result = reset ? WaitForSingleObject(resumeEvent, 8000) : WAIT_FAILED;
        Log("{\"kind\":\"resumed\",\"token\":%llu,\"wait_result\":%lu,\"timed_out\":%s}", static_cast<unsigned long long>(w), result, result == WAIT_TIMEOUT ? "true" : "false"); return 0;
    }
    if (message == kQuit) { DestroyWindow(window); return 0; }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP || message == WM_CHAR || message == WM_SYSCHAR) {
        ++ordinal;
        if (message == WM_CHAR) chars.push_back(static_cast<unsigned>(w));
        else if (message == WM_SYSCHAR) syschars.push_back(static_cast<unsigned>(w));
        else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ++keyDown; else ++keyUp;
        Log("{\"kind\":\"input\",\"token\":%llu,\"ordinal\":%llu,\"message\":%u,\"code\":%llu,\"lparam\":%llu}", token, ordinal, message, static_cast<unsigned long long>(w), static_cast<unsigned long long>(static_cast<ULONG_PTR>(l)));
        return 0; // No DefWindowProc menu or activation side effects on system characters.
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 3) { if (args) LocalFree(args); return 2; }
    logFile = _wfsopen(args[1], L"w", _SH_DENYNO); pumpA = std::wstring(args[2]) == L"a"; LocalFree(args);
    if (!logFile) return 2;
    if (!char_spy::Install()) { Log("{\"kind\":\"fatal\",\"stage\":\"safety_or_loaded_us_layout_unavailable\",\"loaded_us\":%s}", char_spy::layout ? "true" : "false"); return 3; }
    const auto suffix = GetTickCount64(); wchar_t eventName[128]{};
    swprintf_s(eventName, L"Local\\vi_chartranslate_%lu_%llu", GetCurrentProcessId(), suffix);
    resumeEvent = CreateEventW(nullptr, TRUE, FALSE, eventName); if (!resumeEvent) return 4;
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = Window; wc.lpszClassName = L"ViCharTranslate";
    if (!RegisterClassW(&wc)) return 5;
    mainWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Synthetic standard character pump", WS_OVERLAPPEDWINDOW | WS_VISIBLE, -26000, 600, 640, 360, nullptr, nullptr, instance, nullptr);
    if (!mainWindow) return 5;
    Log("{\"kind\":\"ready\",\"pid\":%lu,\"hwnd\":%llu,\"pump\":\"%s\",\"resume_event\":\"Local\\\\vi_chartranslate_%lu_%llu\",\"layout\":%llu,\"loaded_us_only\":true,\"no_layout_mutation\":true,\"write_spies_terminal\":true,\"foreground_spies_terminal\":true}", GetCurrentProcessId(), static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(mainWindow)), pumpA ? "a" : "w", GetCurrentProcessId(), suffix, static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(char_spy::layout)));
    bool running = true; const auto deadline = GetTickCount64() + 120000;
    while (running && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { running = false; break; }
            // Intentionally ordinary native pump. Product hooks alone may translate.
            const BOOL translated = TranslateMessage(&message);
            if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)
                Log("{\"kind\":\"translated\",\"token\":%llu,\"message\":%u,\"code\":%llu,\"result\":%s}", token, message.message, static_cast<unsigned long long>(message.wParam), translated ? "true" : "false");
            if (pumpA) DispatchMessageA(&message); else DispatchMessageW(&message);
        }
        Sleep(1);
    }
    if (IsWindow(mainWindow)) DestroyWindow(mainWindow);
    Log("{\"kind\":\"exit\",\"write_spies_retained\":true}"); CloseHandle(resumeEvent); fclose(logFile); logFile = nullptr;
    // No MH_Uninitialize or keyboard-layout unload: no global state is restored/mutated.
    return 0;
}
