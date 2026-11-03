#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <share.h>
#include <string>
static FILE* report;
static bool rejectResize;
static HWND mainWindow;
static int originalW, originalH;
static std::string Utf8(const wchar_t* text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string value(size, '\0'); WideCharToMultiByte(CP_UTF8, 0, text, -1, &value[0], size, nullptr, nullptr); value.resize(size-1); return value;
}
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_WINDOWPOSCHANGING && rejectResize && h == mainWindow) {
        auto position = (WINDOWPOS*)l;
        if (!(position->flags & SWP_NOSIZE)) { position->cx = originalW; position->cy = originalH; }
    }
    if (m == WM_SIZE && report) {
        RECT r{}; GetClientRect(h, &r); fprintf(report, "size_hwnd=%p client=%ld,%ld\n", h, r.right, r.bottom); fflush(report);
    }
    if (m == WM_DESTROY && h == mainWindow) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) return 2;
    report = _wfsopen(argv[1], L"wb", _SH_DENYNO);
    if (!report) return 2;
    wchar_t cwd[32768]{}; GetCurrentDirectoryW(32768, cwd);
    HANDLE marker = CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    fprintf(report, "pid=%lu\ncwd=%s\nmarker=%d\n", GetCurrentProcessId(), Utf8(cwd).c_str(), marker != INVALID_HANDLE_VALUE); fflush(report);
    if (marker != INVALID_HANDLE_VALUE) {
        char text[64]{};DWORD read=0;if(ReadFile(marker,text,63,&read,nullptr)){text[read]=0;fprintf(report,"marker_text=%s\n",text);fflush(report);}
        CloseHandle(marker);
    }
    std::wstring mode = argv[3]; rejectResize = mode == L"override";
    auto setDpi = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = mode == L"splash" ? L"ViLaunchSplash" : L"ViLaunchMain";
    if (!RegisterClassW(&wc)) return 3;
    if (mode == L"splashmain") {
        WNDCLASSW splashClass=wc;splashClass.lpszClassName=L"ViLaunchSplash";
        if(!RegisterClassW(&splashClass))return 7;
        HWND splash=CreateWindowExW(WS_EX_NOACTIVATE,splashClass.lpszClassName,L"owned splash",WS_OVERLAPPEDWINDOW,-28000,600,400,300,nullptr,nullptr,wc.hInstance,nullptr);
        if(!splash)return 8;
        ShowWindow(splash,SW_SHOWNOACTIVATE);RECT area{};GetClientRect(splash,&area);
        fprintf(report,"splash_hwnd=0x%llX\nsplash_client_w=%ld\nsplash_client_h=%ld\n",(unsigned long long)(ULONG_PTR)splash,area.right,area.bottom);fflush(report);
        Sleep(250);
    }
    HMENU menu = CreateMenu(); AppendMenuW(menu, MF_STRING, 1, L"Owned fixture");
    // Large enough for legacy launch enumeration even when the caller is DPI-unaware.
    originalW=700;originalH=500;
    mainWindow = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"owned launch options", WS_OVERLAPPEDWINDOW,
        -28000, 600, originalW, originalH, nullptr, menu, wc.hInstance, nullptr);
    if (!mainWindow) return 4;
    ShowWindow(mainWindow, SW_SHOWNOACTIVATE);
    RECT before{}, client{};GetWindowRect(mainWindow,&before);GetClientRect(mainWindow,&client);
    fprintf(report,"main_hwnd=0x%llX\ninitial_x=%ld\ninitial_y=%ld\nvisible=%d style=0x%llX\n",(unsigned long long)(ULONG_PTR)mainWindow,before.left,before.top,(int)IsWindowVisible(mainWindow),(unsigned long long)GetWindowLongPtrW(mainWindow,GWL_STYLE));fflush(report);
    fprintf(report,"initial_client_w=%ld\ninitial_client_h=%ld\n",client.right,client.bottom);fflush(report);
    MSG message; while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    fclose(report);return 0;
}
