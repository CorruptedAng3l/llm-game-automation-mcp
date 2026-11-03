// Owned diagnostic fixture: no desktop input or foreground request.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#ifdef BUILD_EXIT_DLL
#ifdef PIPE_EXIT
static DWORD WINAPI ExitAtPipe(LPVOID) {
    wchar_t name[80]; swprintf_s(name, L"\\\\.\\pipe\\vinput_%lu", GetCurrentProcessId());
    HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024, 1024, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) ExitProcess(74);
    ConnectNamedPipe(pipe, nullptr);
#ifdef PIPE_STAYS_ALIVE
    CloseHandle(pipe);
    return 0;
#else
    ExitProcess(73);
#endif
}
#endif
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
#ifdef PIPE_EXIT
        HANDLE thread = CreateThread(nullptr, 0, ExitAtPipe, nullptr, 0, nullptr);
        if (!thread) ExitProcess(75);
        CloseHandle(thread);
#else
        ExitProcess(73);
#endif
    }
    return TRUE;
}
#else
int main(int argc, char** argv) {
    if (argc > 1) {
        if (argc > 2) { FILE* ledger = nullptr; if (fopen_s(&ledger, argv[2], "w") || !ledger) return 6; fprintf(ledger, "%lu", GetCurrentProcessId()); fclose(ledger); }
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ViInjectExitOwned";
        if (!RegisterClassW(&wc)) return 4;
        HWND h = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"owned inject-exit target", WS_OVERLAPPEDWINDOW,
            -28000, 600, 320, 200, nullptr, nullptr, wc.hInstance, nullptr);
        if (!h) return 5;
        ShowWindow(h, SW_SHOWNOACTIVATE);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    } else Sleep(60000);
    return 0;
}
#endif
