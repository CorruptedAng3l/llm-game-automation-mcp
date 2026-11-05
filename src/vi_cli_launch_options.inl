// Optional direct-exe child cwd and one-shot, nonactivating adopted-window geometry.
#include "vi_launch_affinity.h"
struct LaunchOptions {
    std::wstring cwd;
    bool explicitCwd = false, clientSize = false;
    unsigned width = 0, height = 0;
    bool affinitySpecified=false;std::uint64_t affinityMask=0;
};
static bool LaunchAbsolutePath(const std::wstring& path, std::wstring& absolute) {
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed || needed > 32768) return false;
    std::vector<wchar_t> buffer(needed);
    DWORD written = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
    if (!written || written >= needed) return false;
    absolute.assign(buffer.data(), written); return true;
}
static bool LaunchDimension(const std::string& text, unsigned& value) {
    value = 0;
    if (text.empty()) return false;
    for (char digit : text) {
        if (digit < '0' || digit > '9' || value > (8192u - (unsigned)(digit - '0')) / 10u) return false;
        value = value * 10u + (unsigned)(digit - '0');
    }
    return value > 0;
}
static bool PrepareLaunchOptions(const Args& a, std::wstring& exe, LaunchOptions& options, std::string& error) {
    options.explicitCwd = HasOpt(a, "cwd"); options.clientSize = HasOpt(a, "client-size");
    options.affinitySpecified=HasOpt(a,"cpu-affinity");
    if (!options.explicitCwd && !options.clientSize && !options.affinitySpecified) return true;
    if (exe.empty() || HasOpt(a, "steam") || HasOpt(a, "aumid")) { error = "--cwd/--client-size/--cpu-affinity currently require direct --exe launch; Steam/AUMID launcher semantics are unsupported"; return false; }
    for (const char* key : {"cwd", "client-size", "cpu-affinity"}) {
        unsigned count = 0; for (const auto& item : a.opt) if (item.first == key) ++count;
        if (count > 1) { error = "duplicate --" + std::string(key); return false; }
    }
    if(options.affinitySpecified){
        if(!ParseLaunchAffinity(GetOpt(a,"cpu-affinity"),(std::numeric_limits<DWORD_PTR>::max)(),options.affinityMask)){error="--cpu-affinity needs a nonzero hexadecimal processor mask such as 0xF";return false;}
        DWORD_PTR current=0,system=0;
        if(!GetProcessAffinityMask(GetCurrentProcess(),&current,&system)||(options.affinityMask&~(std::uint64_t)system)){error="--cpu-affinity selects processors outside this process group's system mask";return false;}
    }
    if (options.explicitCwd) {
        std::string raw = GetOpt(a, "cwd");
        if (raw.empty() || !LaunchAbsolutePath(U2W(raw), options.cwd)) { error = "--cwd needs an existing directory"; return false; }
        DWORD attributes = GetFileAttributesW(options.cwd.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) { error = "--cwd must name an existing directory; no process or lock was created"; return false; }
        // lpApplicationName relative paths are resolved against the caller, independently of lpCurrentDirectory.
        std::wstring resolved;
        if (!LaunchAbsolutePath(exe, resolved)) { error = "cannot resolve --exe against caller directory"; return false; }
        exe = resolved;
    }
    if (options.clientSize) {
        if (HasOpt(a, "no-inject")) { error = "--client-size needs injected authoritative window adoption; omit --no-inject"; return false; }
        std::string size = GetOpt(a, "client-size"); size_t separator = size.find('x');
        if (separator == std::string::npos || !LaunchDimension(size.substr(0, separator), options.width) || !LaunchDimension(size.substr(separator + 1), options.height)) {
            error = "--client-size must be WIDTHxHEIGHT, each 1..8192 whole client units (for example 1280x720)"; return false;
        }
    }
    return true;
}
struct LaunchDpiScope {
    using SetContext = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    SetContext set = nullptr; DPI_AWARENESS_CONTEXT previous = nullptr;
    bool matched = false;
    explicit LaunchDpiScope(HWND window) {
        HMODULE user = GetModuleHandleW(L"user32.dll");
        auto get = (DPI_AWARENESS_CONTEXT(WINAPI*)(HWND))GetProcAddress(user, "GetWindowDpiAwarenessContext");
        set = (SetContext)GetProcAddress(user, "SetThreadDpiAwarenessContext");
        if (set && get) { previous = set(get(window)); matched = previous != nullptr; }
    }
    ~LaunchDpiScope() { if (set && previous) set(previous); }
};
static std::string ApplyLaunchClientSize(DWORD pid, const LaunchOptions& options) {
    if (!options.clientSize) return {};
    HWND window = nullptr; std::string response, warning;
    if (Request(pid, "{\"cmd\":\"status\"}", response, 1000) && response.find("\"ok\":true") != std::string::npos) {
        std::string hwnd = JsonText(response, "hwnd"); char* end = nullptr;
        unsigned long long number = strtoull(hwnd.c_str(), &end, 16);
        if (!hwnd.empty() && end && !*end) window = (HWND)(ULONG_PTR)number;
    }
    DWORD owner = 0; if (window) GetWindowThreadProcessId(window, &owner);
    std::string name = WindowClass(window), lower = name;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    if (pid != g_initialLaunchPid.load()) warning = "indirect/relaunched target sizing is unsupported; initial child cwd does not assert this process cwd";
    else if (!window || !IsWindow(window) || owner != pid) warning = "no authoritative adopted target window; target remains running";
    else if (GetAncestor(window, GA_ROOT) != window || name == "Windows.UI.Core.CoreWindow" || name == "ApplicationFrameWindow") warning = "hosted/CoreWindow client sizing is unsupported; target remains running";
    else if (lower.find("splash") != std::string::npos) warning = "adopted window still appears to be a splash; no resize attempted, target remains running";
    else if (IsIconic(window)) warning = "adopted window is minimized; no visibility or activation change attempted";
    LaunchDpiScope dpiScope(window);
    RECT before{}, after{}, client{}; GetWindowRect(window, &before); GetClientRect(window, &client);
    bool visibleBefore = IsWindowVisible(window) != FALSE, accepted = false;
    DWORD error = 0; UINT dpi = 96;
    auto getDpi = (UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (getDpi && window) dpi = getDpi(window);
    if (warning.empty() && (!dpiScope.matched || !getDpi || !dpi)) warning = "target DPI context cannot be matched safely; no resize attempted, target remains running";
    if (warning.empty()) {
        RECT desired{0, 0, (LONG)options.width, (LONG)options.height};
        DWORD style = (DWORD)GetWindowLongPtrW(window, GWL_STYLE), extended = (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE);
        auto adjust = (BOOL(WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT))GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
        BOOL adjusted = adjust ? adjust(&desired, style, GetMenu(window) != nullptr, extended, dpi) : AdjustWindowRectEx(&desired, style, GetMenu(window) != nullptr, extended);
        if (!adjusted) { error = GetLastError(); warning = "cannot calculate client-size borders; target remains running"; }
        else {
            SetLastError(0);
            accepted = SetWindowPos(window, nullptr, 0, 0, desired.right-desired.left, desired.bottom-desired.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
            if (!accepted) { error = GetLastError(); warning = "client resize request refused; target remains running"; }
            else {
                ULONGLONG started = GetTickCount64();
                do {
                    DWORD currentOwner = 0; GetWindowThreadProcessId(window, &currentOwner);
                    if (!IsWindow(window) || currentOwner != pid) { warning = "adopted window changed during size verification; target was not relaunched"; break; }
                    GetClientRect(window, &client);
                    if (client.right == (LONG)options.width && client.bottom == (LONG)options.height) break;
                    Sleep(10);
                } while (GetTickCount64()-started < 1000);
                if (warning.empty() && (client.right != (LONG)options.width || client.bottom != (LONG)options.height)) warning = "application did not retain requested client size within 1000 ms; target remains running";
            }
        }
    }
    GetWindowRect(window, &after);
    bool positionPreserved = before.left == after.left && before.top == after.top;
    bool visibilityPreserved = visibleBefore == (IsWindowVisible(window) != FALSE);
    if (warning.empty() && (!positionPreserved || !visibilityPreserved)) warning = "application changed position/visibility during resize; target remains running, no corrective movement attempted";
    bool applied = accepted && warning.empty();
    char fields[640]; sprintf_s(fields, ",\"client_size\":{\"requested_w\":%u,\"requested_h\":%u,\"observed_w\":%ld,\"observed_h\":%ld,\"hwnd\":\"0x%llX\",\"request_accepted\":%s,\"applied\":%s,\"one_shot\":true,\"position_preserved\":%s,\"visibility_preserved\":%s,\"dpi\":%u,\"dpi_context_matched\":%s,\"win32_error\":%lu,\"window_class\":\"",
        options.width, options.height, client.right, client.bottom, (unsigned long long)(ULONG_PTR)window, accepted?"true":"false", applied?"true":"false",positionPreserved?"true":"false",visibilityPreserved?"true":"false",dpi,dpiScope.matched?"true":"false",error);
    std::string json = fields + JEsc(name) + "\",\"units\":\"target client units at observed DPI; later/mixed-monitor changes are not held\"";
    if (!warning.empty()) json += ",\"warning\":\"" + JEsc(warning) + "\"";
    return json + "}";
}
