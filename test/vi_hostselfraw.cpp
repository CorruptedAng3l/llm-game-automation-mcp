// New scoped derivative of tracked test/vi_selfraw.cpp, prepared for row163 transport coverage.
// It reproduces the pinned host's address marker + hidden target + one-shot delta decoder.
// It does not model a game engine or prove the cause of any game's ignored camera input.
// Ordinary MinHook is independent of vinput's renamed MinHook instance.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <share.h>
#include "MinHook.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
using RawRead = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
constexpr UINT kArm = WM_APP + 1;
constexpr UINT kProduce = WM_APP + 2;
constexpr UINT kSnapshot = WM_APP + 3;
constexpr UINT kDeferred = WM_APP + 4;
constexpr UINT kQuit = WM_APP + 5;
FILE* gLog = nullptr;
std::mutex gLogMutex;
HWND gMain = nullptr, gRawTarget = nullptr;
WNDPROC gBeneathOverlay = nullptr;
RawRead gOriginalRaw = nullptr;
bool gBelow = false, gPumpA = false, gDecoder = false;
volatile LONG gDx = 0, gDy = 0;
LONG gDeviceToken = 0;
const HRAWINPUT kMarker = reinterpret_cast<HRAWINPUT>(const_cast<LONG*>(&gDx));
const HANDLE kDevice = reinterpret_cast<HANDLE>(&gDeviceToken);
unsigned long long gRequest = 0;
unsigned gMode = 0;
unsigned long long gPosted = 0, gReceived = 0, gServed = 0, gNonzero = 0, gApplied = 0;
unsigned long long gQueries = 0, gHeaders = 0, gDataOrdinal = 0;
long long gApplicationX = 0, gApplicationY = 0;
std::atomic<unsigned long long> gCursor{0}, gPhysical{0}, gClip{0}, gSend{0}, gMouse{0};
std::atomic<unsigned long long> gSetForeground{0}, gSetActive{0}, gSetFocus{0}, gBring{0}, gSwitch{0};
std::atomic<unsigned long long> gAllow{0}, gWindowPos{0}, gShow{0}, gShowAsync{0}, gAttach{0};
std::atomic<unsigned long long> gSetCapture{0}, gReleaseCapture{0}, gLockForeground{0};
struct Hook { void* target; void* replacement; void* original; };
std::vector<Hook> gHooks;

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    if (!gLog) return;
    va_list args; va_start(args, format); vfprintf(gLog, format, args); va_end(args);
    fputc('\n', gLog); fflush(gLog);
}
// Terminal safety spies: no write trampoline is ever called, including ClipCursor(nullptr).
BOOL WINAPI SafeCursor(int, int) { ++gCursor; return TRUE; }
BOOL WINAPI SafePhysical(int, int) { ++gPhysical; return TRUE; }
BOOL WINAPI SafeClip(const RECT*) { ++gClip; return TRUE; }
UINT WINAPI SafeSend(UINT count, LPINPUT input, int bytes) {
    ++gSend;
    if (bytes != static_cast<int>(sizeof(INPUT)) || (count && !input)) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    return count;
}
void WINAPI SafeMouse(DWORD, DWORD, DWORD, DWORD, ULONG_PTR) { ++gMouse; }
BOOL WINAPI SafeForeground(HWND) { ++gSetForeground; return TRUE; }
HWND WINAPI SafeActive(HWND) { ++gSetActive; return nullptr; }
HWND WINAPI SafeFocus(HWND) { ++gSetFocus; return nullptr; }
BOOL WINAPI SafeBring(HWND) { ++gBring; return TRUE; }
void WINAPI SafeSwitch(HWND, BOOL) { ++gSwitch; }
BOOL WINAPI SafeAllow(DWORD) { ++gAllow; return TRUE; }
BOOL WINAPI SafeWindowPos(HWND, HWND, int, int, int, int, UINT) { ++gWindowPos; return TRUE; }
BOOL WINAPI SafeShow(HWND, int) { ++gShow; return TRUE; }
BOOL WINAPI SafeShowAsync(HWND, int) { ++gShowAsync; return TRUE; }
BOOL WINAPI SafeAttach(DWORD, DWORD, BOOL) { ++gAttach; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
HWND WINAPI SafeCapture(HWND) { ++gSetCapture; return nullptr; }
BOOL WINAPI SafeReleaseCapture() { ++gReleaseCapture; return TRUE; }
BOOL WINAPI SafeLockForeground(UINT) { ++gLockForeground; return TRUE; }
bool EntryRead(const void* address,void* output,SIZE_T bytes){SIZE_T read=0;return address&&ReadProcessMemory(GetCurrentProcess(),address,output,bytes,&read)&&read==bytes;}
bool EntryReaches(void* entry,void* expected){
    auto current=reinterpret_cast<std::uintptr_t>(entry);
    for(unsigned step=0;step<8&&current;step++){
        if(current==reinterpret_cast<std::uintptr_t>(expected))return true;
        BYTE bytes[6]{};if(!EntryRead(reinterpret_cast<void*>(current),bytes,sizeof(bytes)))return false;
        if(bytes[0]==0xE9){std::int32_t delta=0;memcpy(&delta,bytes+1,4);current=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(current)+5+delta);continue;}
        if(bytes[0]==0xEB){current=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(current)+2+static_cast<std::int8_t>(bytes[1]));continue;}
        if(bytes[0]==0xFF&&bytes[1]==0x25){std::int32_t displacement=0;memcpy(&displacement,bytes+2,4);
#ifdef _WIN64
            const auto slot=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(current)+6+displacement);
#else
            const auto slot=static_cast<std::uintptr_t>(static_cast<std::uint32_t>(displacement));
#endif
            if(!EntryRead(reinterpret_cast<void*>(slot),&current,sizeof(current)))return false;continue;
        }
        return false;
    }
    return false;
}
bool HookAt(const char* name, void* target, void* replacement, void** original = nullptr) {
    for(const auto& hook:gHooks)if(hook.target==target){
        const bool same=hook.replacement==replacement;
        const bool setterAlias=(hook.replacement==reinterpret_cast<void*>(&SafeCursor)||hook.replacement==reinterpret_cast<void*>(&SafePhysical))&&
            (replacement==reinterpret_cast<void*>(&SafeCursor)||replacement==reinterpret_cast<void*>(&SafePhysical));
        const bool verified=(same||setterAlias)&&EntryReaches(target,hook.replacement);
        if(verified&&original)*original=hook.original;
        Log("{\"kind\":\"hook\",\"name\":\"%s\",\"ok\":%s,\"alias\":true,\"terminal_setter_alias\":%s,\"entry_verified\":%s,\"counter_owner\":\"%s\"}",name,verified?"true":"false",setterAlias?"true":"false",verified?"true":"false",
            hook.replacement==reinterpret_cast<void*>(&SafeCursor)?"SetCursorPos shared entry":hook.replacement==reinterpret_cast<void*>(&SafePhysical)?"SetPhysicalCursorPos shared entry":"original installed detour");
        return verified;
    }
    void* trampoline = nullptr;
    const MH_STATUS created = target ? MH_CreateHook(target, replacement, &trampoline) : MH_ERROR_NOT_EXECUTABLE;
    if (created == MH_OK && original) *original = trampoline; // publish before enabling for concurrent readers
    const MH_STATUS enabled = created == MH_OK ? MH_EnableHook(target) : created;
    const bool verified=enabled==MH_OK&&EntryReaches(target,replacement);
    Log("{\"kind\":\"hook\",\"name\":\"%s\",\"create\":%d,\"enable\":%d,\"ok\":%s,\"entry_verified\":%s}",
        name,static_cast<int>(created),static_cast<int>(enabled),verified?"true":"false",verified?"true":"false");
    if (!verified) return false;
    gHooks.push_back({target,replacement,trampoline});
    if (original) *original = trampoline;
    return true;
}
UINT WINAPI SyntheticRegistered(PRAWINPUTDEVICE devices, PUINT count, UINT bytes) {
    if (!count || bytes != sizeof(RAWINPUTDEVICE)) { SetLastError(ERROR_INVALID_PARAMETER); return static_cast<UINT>(-1); }
    if (!devices) { *count = 1; return 0; }
    if (*count < 1) { *count = 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); return static_cast<UINT>(-1); }
    devices[0] = { 1, 2, 0, gRawTarget }; *count = 1; return 1;
}
UINT WINAPI SyntheticDevices(PRAWINPUTDEVICELIST devices, PUINT count, UINT bytes) {
    if (!count || bytes != sizeof(RAWINPUTDEVICELIST)) { SetLastError(ERROR_INVALID_PARAMETER); return static_cast<UINT>(-1); }
    if (!devices) { *count = 1; return 0; }
    if (*count < 1) { *count = 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); return static_cast<UINT>(-1); }
    devices[0].hDevice = kDevice; devices[0].dwType = RIM_TYPEMOUSE; *count = 1; return 1;
}
bool InstallSafetyAndMapping() {
    const MH_STATUS initialized = MH_Initialize();
    Log("{\"kind\":\"minhook\",\"initialize\":%d}", static_cast<int>(initialized));
    if (initialized != MH_OK) return false;
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    bool safe = HookAt("SetCursorPos", reinterpret_cast<void*>(GetProcAddress(user, "SetCursorPos")), reinterpret_cast<void*>(&SafeCursor));
    safe = HookAt("SetPhysicalCursorPos", reinterpret_cast<void*>(GetProcAddress(user, "SetPhysicalCursorPos")), reinterpret_cast<void*>(&SafePhysical)) && safe;
    safe = HookAt("ClipCursor", reinterpret_cast<void*>(GetProcAddress(user, "ClipCursor")), reinterpret_cast<void*>(&SafeClip)) && safe;
    safe = HookAt("SendInput", reinterpret_cast<void*>(GetProcAddress(user, "SendInput")), reinterpret_cast<void*>(&SafeSend)) && safe;
    safe = HookAt("mouse_event", reinterpret_cast<void*>(GetProcAddress(user, "mouse_event")), reinterpret_cast<void*>(&SafeMouse)) && safe;
    safe = HookAt("SetForegroundWindow", reinterpret_cast<void*>(GetProcAddress(user, "SetForegroundWindow")), reinterpret_cast<void*>(&SafeForeground)) && safe;
    safe = HookAt("SetActiveWindow", reinterpret_cast<void*>(GetProcAddress(user, "SetActiveWindow")), reinterpret_cast<void*>(&SafeActive)) && safe;
    safe = HookAt("SetFocus", reinterpret_cast<void*>(GetProcAddress(user, "SetFocus")), reinterpret_cast<void*>(&SafeFocus)) && safe;
    safe = HookAt("BringWindowToTop", reinterpret_cast<void*>(GetProcAddress(user, "BringWindowToTop")), reinterpret_cast<void*>(&SafeBring)) && safe;
    safe = HookAt("SwitchToThisWindow", reinterpret_cast<void*>(GetProcAddress(user, "SwitchToThisWindow")), reinterpret_cast<void*>(&SafeSwitch)) && safe;
    safe = HookAt("AllowSetForegroundWindow", reinterpret_cast<void*>(GetProcAddress(user, "AllowSetForegroundWindow")), reinterpret_cast<void*>(&SafeAllow)) && safe;
    safe = HookAt("SetWindowPos", reinterpret_cast<void*>(GetProcAddress(user, "SetWindowPos")), reinterpret_cast<void*>(&SafeWindowPos)) && safe;
    safe = HookAt("ShowWindow", reinterpret_cast<void*>(GetProcAddress(user, "ShowWindow")), reinterpret_cast<void*>(&SafeShow)) && safe;
    safe = HookAt("ShowWindowAsync", reinterpret_cast<void*>(GetProcAddress(user, "ShowWindowAsync")), reinterpret_cast<void*>(&SafeShowAsync)) && safe;
    safe = HookAt("AttachThreadInput", reinterpret_cast<void*>(GetProcAddress(user, "AttachThreadInput")), reinterpret_cast<void*>(&SafeAttach)) && safe;
    safe = HookAt("SetCapture", reinterpret_cast<void*>(GetProcAddress(user, "SetCapture")), reinterpret_cast<void*>(&SafeCapture)) && safe;
    safe = HookAt("ReleaseCapture", reinterpret_cast<void*>(GetProcAddress(user, "ReleaseCapture")), reinterpret_cast<void*>(&SafeReleaseCapture)) && safe;
    safe = HookAt("LockSetForegroundWindow", reinterpret_cast<void*>(GetProcAddress(user, "LockSetForegroundWindow")), reinterpret_cast<void*>(&SafeLockForeground)) && safe;
    if (!safe) return false;
    bool mapped = HookAt("GetRegisteredRawInputDevices", reinterpret_cast<void*>(GetProcAddress(user, "GetRegisteredRawInputDevices")), reinterpret_cast<void*>(&SyntheticRegistered));
    mapped = HookAt("GetRawInputDeviceList", reinterpret_cast<void*>(GetProcAddress(user, "GetRawInputDeviceList")), reinterpret_cast<void*>(&SyntheticDevices)) && mapped;
    return mapped;
}
UINT WINAPI HostRaw(HRAWINPUT handle, UINT command, LPVOID data, PUINT bytes, UINT header) {
    if (handle != kMarker) return gOriginalRaw(handle, command, data, bytes, header);
    if (!bytes || header != sizeof(RAWINPUTHEADER) || (command != RID_INPUT && command != RID_HEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER); return static_cast<UINT>(-1);
    }
    const UINT need = static_cast<UINT>(command == RID_HEADER ? sizeof(RAWINPUTHEADER) : sizeof(RAWINPUT));
    if (!data) { *bytes = need; ++gQueries; return 0; }
    if (*bytes < need) { *bytes = need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return static_cast<UINT>(-1); }
    RAWINPUT input{}; input.header.dwType = RIM_TYPEMOUSE;
    input.header.dwSize = static_cast<DWORD>(sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE));
    input.header.hDevice = kDevice; input.header.wParam = RIM_INPUT;
    if (command == RID_INPUT) {
        input.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
        input.data.mouse.lLastX = InterlockedExchange(&gDx, 0);
        input.data.mouse.lLastY = InterlockedExchange(&gDy, 0);
        ++gServed; ++gDataOrdinal;
        if (input.data.mouse.lLastX || input.data.mouse.lLastY) ++gNonzero;
        Log("{\"kind\":\"decode\",\"request\":%llu,\"ordinal\":%llu,\"served\":%llu,\"nonzero_reads\":%llu,\"dx\":%ld,\"dy\":%ld}",
            gRequest, gDataOrdinal, gServed, gNonzero, input.data.mouse.lLastX, input.data.mouse.lLastY);
    } else ++gHeaders;
    memcpy(data, &input, need); *bytes = need; return need;
}
bool InstallDecoder() {
    if (gDecoder) return true;
    gDecoder = HookAt("host_GetRawInputData", reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetRawInputData")),
        reinterpret_cast<void*>(&HostRaw), reinterpret_cast<void**>(&gOriginalRaw));
    Log("{\"kind\":\"decoder\",\"order\":\"%s\",\"ok\":%s}", gBelow ? "below" : "above", gDecoder ? "true" : "false");
    return gDecoder;
}
HWND ProducerTarget() {
    RAWINPUTDEVICE devices[16]{}; UINT count = 16;
    const UINT result = GetRegisteredRawInputDevices(devices, &count, sizeof(devices[0]));
    if (result != static_cast<UINT>(-1)) for (UINT n = 0; n < result; ++n)
        if (devices[n].usUsagePage == 1 && devices[n].usUsage == 2) return devices[n].hwndTarget;
    return gMain;
}
void Snapshot(unsigned long long token) {
    Log("{\"kind\":\"snapshot\",\"token\":%llu,\"pid\":%lu,\"hwnd\":%llu,\"raw_hwnd\":%llu,\"posted\":%llu,\"received\":%llu,\"queries\":%llu,\"headers\":%llu,\"served\":%llu,\"nonzero_reads\":%llu,\"applied\":%llu,\"application_x\":%lld,\"application_y\":%lld,\"pending_dx\":%ld,\"pending_dy\":%ld,\"write_cursor\":%llu,\"write_physical\":%llu,\"write_clip\":%llu,\"write_send\":%llu,\"write_mouse\":%llu}",
        token, GetCurrentProcessId(), static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(gMain)),
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(gRawTarget)), gPosted, gReceived, gQueries, gHeaders, gServed,
        gNonzero, gApplied, gApplicationX, gApplicationY, InterlockedCompareExchange(&gDx, 0, 0), InterlockedCompareExchange(&gDy, 0, 0),
        gCursor.load(), gPhysical.load(), gClip.load(), gSend.load(), gMouse.load());
    Log("{\"kind\":\"foreground_write_counters\",\"token\":%llu,\"set_foreground\":%llu,\"set_active\":%llu,\"set_focus\":%llu,\"bring_to_top\":%llu,\"switch_to_window\":%llu,\"allow_foreground\":%llu,\"set_window_pos\":%llu,\"show_window\":%llu,\"show_window_async\":%llu,\"attach_thread_input\":%llu,\"set_capture\":%llu,\"release_capture\":%llu,\"lock_foreground\":%llu}",
        token, gSetForeground.load(), gSetActive.load(), gSetFocus.load(), gBring.load(), gSwitch.load(), gAllow.load(),
        gWindowPos.load(), gShow.load(), gShowAsync.load(), gAttach.load(), gSetCapture.load(), gReleaseCapture.load(), gLockForeground.load());
}
void ReadPacket(WPARAM w, LPARAM l, const char* stage, bool integrate) {
    UINT queryBytes = 0; SetLastError(0);
    const UINT query = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, nullptr, &queryBytes, sizeof(RAWINPUTHEADER));
    const DWORD queryError = GetLastError();
    RAWINPUTHEADER header{}; UINT headerBytes = sizeof(header); SetLastError(0);
    const UINT headerResult = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_HEADER, &header, &headerBytes, sizeof(RAWINPUTHEADER));
    const DWORD headerError = GetLastError();
    const LONG afterHeaderX = InterlockedCompareExchange(&gDx, 0, 0), afterHeaderY = InterlockedCompareExchange(&gDy, 0, 0);
    const auto servedBefore = gServed;
    RAWINPUT input{}; UINT bytes = sizeof(input); SetLastError(0);
    const UINT result = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
    const DWORD error = GetLastError();
    const bool valid = reinterpret_cast<HRAWINPUT>(l) == kMarker && w == RIM_INPUT && query == 0 &&
        queryBytes == sizeof(input) && headerResult == sizeof(header) && header.hDevice == kDevice &&
        header.wParam == RIM_INPUT && header.dwSize == sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) &&
        result == sizeof(input) && input.header.dwType == RIM_TYPEMOUSE && input.header.dwSize == header.dwSize &&
        input.header.hDevice == kDevice && input.header.wParam == RIM_INPUT;
    if (valid && integrate && (input.data.mouse.lLastX || input.data.mouse.lLastY)) {
        gApplicationX += input.data.mouse.lLastX; gApplicationY += input.data.mouse.lLastY; ++gApplied;
    }
    Log("{\"kind\":\"packet\",\"request\":%llu,\"stage\":\"%s\",\"scope\":\"%s\",\"marker_original\":%s,\"wparam\":%llu,\"query\":%u,\"query_error\":%lu,\"query_bytes\":%u,\"header_result\":%u,\"header_error\":%lu,\"after_header_dx\":%ld,\"after_header_dy\":%ld,\"result\":%u,\"error\":%lu,\"valid\":%s,\"dx\":%ld,\"dy\":%ld,\"served_before\":%llu,\"served_after\":%llu,\"applied\":%llu,\"application_x\":%lld,\"application_y\":%lld}",
        gRequest, stage, strcmp(stage, "deferred") == 0 ? "outside_dispatch_unsupported" : "inside_dispatch",
        reinterpret_cast<HRAWINPUT>(l) == kMarker ? "true" : "false", static_cast<unsigned long long>(w), query, queryError, queryBytes,
        headerResult, headerError, afterHeaderX, afterHeaderY, result, error, valid ? "true" : "false",
        input.data.mouse.lLastX, input.data.mouse.lLastY, servedBefore, gServed, gApplied, gApplicationX, gApplicationY);
}
LRESULT RawMessage(WPARAM w, LPARAM l) {
    ++gReceived;
    Log("{\"kind\":\"received\",\"request\":%llu,\"count\":%llu,\"marker\":%llu,\"wparam\":%llu}",
        gRequest, gReceived, static_cast<unsigned long long>(static_cast<ULONG_PTR>(l)), static_cast<unsigned long long>(w));
    if (gMode == 2) {
        const BOOL posted = PostMessageW(gMain, kDeferred, static_cast<WPARAM>(gRequest), l);
        Log("{\"kind\":\"deferred_queued\",\"request\":%llu,\"ok\":%s}", gRequest, posted ? "true" : "false");
    } else {
        ReadPacket(w, l, "single", true);
        if (gMode == 1) ReadPacket(w, l, "duplicate", true);
    }
    return 0;
}
LRESULT CALLBACK RawOverlay(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INPUT) return RawMessage(w, l);
    return CallWindowProcW(gBeneathOverlay, window, message, w, l);
}
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_INPUT) return RawMessage(w, l);
    if (message == kArm) {
        const bool decoder = gBelow || InstallDecoder();
        SetLastError(0);
        gBeneathOverlay = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gRawTarget, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RawOverlay)));
        const DWORD error = GetLastError();
        const bool ok = decoder && gBeneathOverlay != nullptr;
        Log("{\"kind\":\"armed\",\"order\":\"%s\",\"ok\":%s,\"subclass_error\":%lu}", gBelow ? "below" : "above", ok ? "true" : "false", error);
        return 0;
    }
    if (message == kProduce) {
        if (!w || l < 0 || l > 2) { Log("{\"kind\":\"control_error\",\"reason\":\"invalid_produce\"}"); return 0; }
        gRequest = static_cast<unsigned long long>(w); gMode = static_cast<unsigned>(l);
        InterlockedExchangeAdd(&gDx, 37); InterlockedExchangeAdd(&gDy, -19);
        const HWND target = ProducerTarget();
        SetLastError(0); const BOOL ok = PostMessageW(target, WM_INPUT, RIM_INPUT, reinterpret_cast<LPARAM>(kMarker));
        const DWORD error = GetLastError(); if (ok) ++gPosted;
        Log("{\"kind\":\"produce\",\"request\":%llu,\"mode\":%u,\"ok\":%s,\"error\":%lu,\"target\":%llu,\"hidden_target\":%s}",
            gRequest, gMode, ok ? "true" : "false", error, static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(target)), target == gRawTarget ? "true" : "false");
        return 0;
    }
    if (message == kSnapshot) { Snapshot(static_cast<unsigned long long>(w)); return 0; }
    if (message == kDeferred) {
        // This intentionally executes after the original DispatchMessage scope ended.
        // An ABOVE decoder can answer before vinput is entered; this remains unsupported.
        gRequest = static_cast<unsigned long long>(w); ReadPacket(RIM_INPUT, l, "deferred", true); return 0;
    }
    if (message == kQuit) { DestroyWindow(gMain); return 0; }
    if (message == WM_DESTROY && window == gMain) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 4) { if (args) LocalFree(args); return 2; }
    gLog = _wfsopen(args[1], L"w", _SH_DENYNO);
    gBelow = std::wstring(args[2]) == L"below"; gPumpA = std::wstring(args[3]) == L"a"; LocalFree(args);
    if (!gLog) return 2;
    if (!InstallSafetyAndMapping() || (gBelow && !InstallDecoder())) return 3;
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = Window; wc.lpszClassName = L"ViHostSelfRaw";
    if (!RegisterClassW(&wc)) return 4;
    gMain = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Host raw synthetic application",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, -26000, 700, 600, 360, nullptr, nullptr, instance, nullptr);
    gRawTarget = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Synthetic registered hidden raw target",
        WS_OVERLAPPEDWINDOW, -26000, 1200, 80, 60, nullptr, nullptr, instance, nullptr);
    if (!gMain || !gRawTarget) return 4;
    ShowWindow(gMain, SW_SHOWNOACTIVATE);
    UINT devices = 0; const UINT enumeration = GetRawInputDeviceList(nullptr, &devices, sizeof(RAWINPUTDEVICELIST));
    Log("{\"kind\":\"ready\",\"pid\":%lu,\"hwnd\":%llu,\"raw_hwnd\":%llu,\"marker\":%llu,\"device\":%llu,\"order\":\"%s\",\"pump\":\"%s\",\"synthetic_device_query\":%u,\"synthetic_device_count\":%u,\"no_real_registration\":true,\"write_spies_terminal\":true,\"foreground_spies_terminal\":true}",
        GetCurrentProcessId(), static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(gMain)),
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(gRawTarget)), static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(kMarker)),
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(kDevice)), gBelow ? "below" : "above", gPumpA ? "a" : "w", enumeration, devices);
    const ULONGLONG until = GetTickCount64() + 120000; bool running = true;
    while (running && GetTickCount64() < until) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            if (gPumpA) DispatchMessageA(&message); else DispatchMessageW(&message);
        }
        Sleep(2);
    }
    // Keep all lower safety hooks through cleanup and process termination.
    if (IsWindow(gRawTarget)) DestroyWindow(gRawTarget);
    if (IsWindow(gMain)) DestroyWindow(gMain);
    Log("{\"kind\":\"exit\",\"write_spies_retained\":true}"); fclose(gLog); gLog = nullptr; return 0;
}
