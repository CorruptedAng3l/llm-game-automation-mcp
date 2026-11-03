// vinput_core.cpp - per-process virtual input, fake focus and swapchain capture.
// See vinput.h / README.md. Offline / single-player games only: see DISCLAIMER.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <sddl.h>
#include <psapi.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <GL/gl.h>
// Vulkan: types only (VK_NO_PROTOTYPES); every entry point is resolved from an already-loaded vulkan-1.dll
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "../third_party/vulkan/vulkan_core.h"
#include "../third_party/vulkan/vulkan_win32.h"
#include <type_traits>
#include <dinput.h>
#include <wincodec.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include "vi_mh_rename.h"
#include "MinHook.h"
#include "vinput.h"
#include "vi_keys.h"
#include "vi_trace.h"
#include "vi_vision.h"
#include "vi_acguard.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#define VI_VERSION "vinput 1.0"
typedef unsigned __int64 QWORD;  // used by NEXTRAWINPUTBLOCK; not always declared under WIN32_LEAN_AND_MEAN

// ----------------------------------------------------------------------------------------------
// state
// ----------------------------------------------------------------------------------------------
namespace {

struct Config {
    std::atomic<bool> fakefocus{true};    // swallow deactivation, report game as foreground
    std::atomic<bool> autoactivate{true}; // background game never told it is active: post one activation to its own window
    std::atomic<bool> isolate{true};      // while NOT really foreground: hide real kb/mouse from the game
    std::atomic<bool> blockfocus{true};   // while NOT really foreground: game cannot steal focus
    std::atomic<bool> nofullscreen{true}; // refuse DXGI exclusive fullscreen
    std::atomic<bool> keepvisible{true};  // while NOT really foreground: refuse/undo minimise (minimised games stop presenting)
    std::atomic<int>  cursor{0};          // 0 auto (virtual when not fg), 1 always virtual, 2 always real
    std::atomic<int>  rawmode{0};         // 0 auto, 1 force post WM_INPUT, 2 force GetRawInputBuffer queue, 3 off
    std::atomic<int>  msgmode{0};         // 0 auto (unless RIDEV_NOLEGACY), 1 force, 2 off
};
Config g_cfg;

DWORD   g_pid = 0;
HWND    g_hwnd = nullptr;
bool    g_hwndUnicode = true;
WNDPROC g_origWndProc = nullptr;
HMODULE g_self = nullptr;
DWORD   g_t0 = 0;
std::atomic<bool> g_realFg{false};
std::atomic<bool> g_hwndForced{false};
std::atomic<int>  g_appActive{-1};     // last WM_ACTIVATEAPP the game's window procedure saw: -1 none, 0 inactive, 1 active
std::atomic<DWORD> g_lastSynthAct{0};
std::atomic<int>  g_synthLeft{3};       // synthetic activations left for this window (refilled when a real deactivation reaches the game)
bool    g_appContainer = false;   // UWP / Xbox app games: pipe in the container namespace, CoreWindow target

std::atomic<uint8_t> g_vk[256];
std::atomic<uint8_t> g_vkPressed[256];
KeyTrace g_keyTrace[256];
std::atomic<LONG> g_vcurX{0}, g_vcurY{0};
std::atomic<bool> g_vcurTouched{false};
std::atomic<long> g_mouseMovesDelivered{0};
std::atomic<LONG> g_diDX{0}, g_diDY{0}, g_diDZ{0};

std::atomic<bool> g_padActive{false};
std::atomic<WORD> g_padButtons{0};
std::atomic<int>  g_padLT{0}, g_padRT{0}, g_padLX{0}, g_padLY{0}, g_padRX{0}, g_padRY{0};
std::atomic<DWORD> g_padPacket{1};

struct Counters {
    std::atomic<long> gaks{0}, gks{0}, gkbs{0}, gcur{0}, scur{0}, clip{0}, gfg{0}, setfg{0}, setfg_blocked{0};
    std::atomic<long> rid{0}, rid_magic{0}, rib{0}, rib_injected{0}, reg_raw{0};
    std::atomic<long> di_kbd{0}, di_mouse{0}, di_data_kbd{0}, di_data_mouse{0}, di_acq{0}, di_fake{0};
    std::atomic<long> xinput{0};
    std::atomic<long> wm_key{0}, wm_char{0}, wm_mouse{0}, wm_input{0}, wm_input_dropped{0}, wm_mouse_dropped{0};
    std::atomic<long> min_blocked{0}, min_restored{0};
    std::atomic<long> act_swallowed{0}, act_synth{0}, present{0}, present_ours{0}, captures{0};
    std::atomic<long> posted{0}, raw_posted{0}, raw_queued{0};
    std::atomic<long> fs_refused{0};   // SetFullscreenState(TRUE) calls refused by nofullscreen
} C;

std::string g_hooksOk, g_hooksFail;
std::mutex g_logMx;
std::deque<std::string> g_logRing;
wchar_t g_logPath[MAX_PATH] = {0};

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
    char line[1200];
    sprintf_s(line, "[%7.3f] %s", (GetTickCount() - g_t0) / 1000.0, buf);
    std::lock_guard<std::mutex> lk(g_logMx);
    g_logRing.push_back(line);
    while (g_logRing.size() > 200) g_logRing.pop_front();
    OutputDebugStringA(line); OutputDebugStringA("\n");
    if (g_logPath[0]) {
        HANDLE f = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, line, (DWORD)strlen(line), &w, nullptr); WriteFile(f, "\r\n", 2, &w, nullptr); CloseHandle(f); }
    }
}

std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}

// ----------------------------------------------------------------------------------------------
// originals
// ----------------------------------------------------------------------------------------------
using PFN_GetFG = HWND(WINAPI*)();
using PFN_SetFG = BOOL(WINAPI*)(HWND);
using PFN_GAKS = SHORT(WINAPI*)(int);
using PFN_GKBS = BOOL(WINAPI*)(PBYTE);
using PFN_GCP = BOOL(WINAPI*)(LPPOINT);
using PFN_SCP = BOOL(WINAPI*)(int, int);
using PFN_Clip = BOOL(WINAPI*)(const RECT*);
using PFN_GRID = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using PFN_GRIB = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
using PFN_RRID = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
using PFN_SWP = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using PFN_SW = BOOL(WINAPI*)(HWND, int);
using PFN_WFP = HWND(WINAPI*)(POINT);

PFN_GetFG o_GetForegroundWindow = nullptr, o_GetActiveWindow = nullptr, o_GetFocus = nullptr;
PFN_SetFG o_SetForegroundWindow = nullptr, o_BringWindowToTop = nullptr;
PFN_GAKS o_GetAsyncKeyState = nullptr, o_GetKeyState = nullptr;
PFN_GKBS o_GetKeyboardState = nullptr;
PFN_GCP o_GetCursorPos = nullptr, o_GetPhysicalCursorPos = nullptr;
PFN_SCP o_SetCursorPos = nullptr, o_SetPhysicalCursorPos = nullptr;
PFN_Clip o_ClipCursor = nullptr;
PFN_GRID o_GetRawInputData = nullptr;
PFN_GRIB o_GetRawInputBuffer = nullptr;
PFN_RRID o_RegisterRawInputDevices = nullptr;
PFN_SWP o_SetWindowPos = nullptr;
PFN_SW o_ShowWindow = nullptr;
PFN_WFP o_WindowFromPoint = nullptr;

HWND RealForeground() { return o_GetForegroundWindow ? o_GetForegroundWindow() : GetForegroundWindow(); }
bool ComputeRealFg() {
    HWND f = RealForeground(); if (!f) return false;
    DWORD p = 0; GetWindowThreadProcessId(f, &p); if (p == g_pid) return true;
    // UWP: the foreground window is ApplicationFrameHost's frame that hosts our CoreWindow
    return g_hwnd && GetParent(g_hwnd) == f;
}
inline bool RealFg() { return g_realFg.load(std::memory_order_relaxed); }
inline bool Isolating() { return g_cfg.isolate && !RealFg(); }
inline bool Faking() { return g_cfg.fakefocus && !RealFg(); }
bool UseVirtualCursor() {
    if (!g_hwnd) return false;
    int c = g_cfg.cursor; if (c == 1) return true; if (c == 2) return false;
    return !RealFg();
}
bool IsOurWindow(HWND h) { if (!h) return false; DWORD p = 0; GetWindowThreadProcessId(h, &p); return p == g_pid; }

// ----------------------------------------------------------------------------------------------
// posted-mouse-message ring: lets the subclass tell OUR posted mouse messages from real ones
// ----------------------------------------------------------------------------------------------
struct PostedMsg { UINT m; WPARAM w; LPARAM l; DWORD t; };
SRWLOCK g_ringLock = SRWLOCK_INIT;
PostedMsg g_ring[512];
int g_ringPos = 0;
void RingAdd(UINT m, WPARAM w, LPARAM l) {
    AcquireSRWLockExclusive(&g_ringLock);
    g_ring[g_ringPos] = {m, w, l, GetTickCount()}; g_ringPos = (g_ringPos + 1) % 512;
    ReleaseSRWLockExclusive(&g_ringLock);
}
bool RingConsume(UINT m, WPARAM w, LPARAM l) {
    bool hit = false;
    AcquireSRWLockExclusive(&g_ringLock);
    for (int i = 0; i < 512; i++) if (g_ring[i].m == m && g_ring[i].w == w && g_ring[i].l == l && g_ring[i].t) { g_ring[i].t = 0; g_ring[i].m = 0; hit = true; break; }
    ReleaseSRWLockExclusive(&g_ringLock);
    return hit;
}
void PostOurs(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!h) return;
    if ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST)) RingAdd(m, w, l);
    PostMessageW(h, m, w, l); C.posted++;
}

// ----------------------------------------------------------------------------------------------
// raw input synthesis
// ----------------------------------------------------------------------------------------------
const ULONG_PTR kMagicBase = 0x56490000;  // fake HRAWINPUT values: 0x5649xxxx
RAWINPUT g_rawRing[4096];
std::atomic<unsigned> g_rawRingPos{0};
std::mutex g_rawQMx;
std::deque<RAWINPUT> g_rawQ;          // for GetRawInputBuffer consumers
std::atomic<DWORD> g_lastRIB{0};      // tick of last GetRawInputBuffer call
inline bool IsMagic(HRAWINPUT h) { ULONG_PTR v = (ULONG_PTR)h; return (v & ~(ULONG_PTR)0xFFF) == kMagicBase; }

struct RawReg { bool kbd = false, mouse = false, kbdNoLegacy = false, mouseNoLegacy = false; HWND kbdTarget = nullptr, mouseTarget = nullptr; };
RawReg QueryRawReg() {
    RawReg r; UINT n = 0;
    GetRegisteredRawInputDevices(nullptr, &n, sizeof(RAWINPUTDEVICE));
    if (!n) return r;
    std::vector<RAWINPUTDEVICE> v(n);
    if (GetRegisteredRawInputDevices(v.data(), &n, sizeof(RAWINPUTDEVICE)) == (UINT)-1) return r;
    for (UINT i = 0; i < n; i++) {
        if (v[i].usUsagePage != 1) continue;
        if (v[i].usUsage == 6) { r.kbd = true; r.kbdNoLegacy = (v[i].dwFlags & RIDEV_NOLEGACY) != 0; r.kbdTarget = v[i].hwndTarget; }
        if (v[i].usUsage == 2) { r.mouse = true; r.mouseNoLegacy = (v[i].dwFlags & RIDEV_NOLEGACY) != 0; r.mouseTarget = v[i].hwndTarget; }
    }
    return r;
}
HANDLE g_rawKbdDev = nullptr, g_rawMouseDev = nullptr;
void FindRawDevices() {
    UINT n = 0; GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST));
    if (!n) return;
    std::vector<RAWINPUTDEVICELIST> v(n);
    if (GetRawInputDeviceList(v.data(), &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return;
    for (UINT i = 0; i < n; i++) {
        if (v[i].dwType == RIM_TYPEKEYBOARD && !g_rawKbdDev) g_rawKbdDev = v[i].hDevice;
        if (v[i].dwType == RIM_TYPEMOUSE && !g_rawMouseDev) g_rawMouseDev = v[i].hDevice;
    }
}
void EmitRaw(const RAWINPUT& ri, HWND target) {
    int mode = g_cfg.rawmode;
    if (mode == 3) return;
    bool useBuffer = (mode == 2) || (mode == 0 && g_lastRIB && GetTickCount() - g_lastRIB < 3000);
    if (useBuffer) {
        std::lock_guard<std::mutex> lk(g_rawQMx);
        if (g_rawQ.size() < 1024) g_rawQ.push_back(ri);
        C.raw_queued++;
        return;
    }
    unsigned idx = (g_rawRingPos++) & 0xFFF;
    g_rawRing[idx] = ri;
    PostMessageW(target ? target : g_hwnd, WM_INPUT, RIM_INPUT, (LPARAM)(kMagicBase | idx));
    C.raw_posted++;
}
void RawKey(int vk, bool down, UINT sc, bool ext, HWND target) {
    RAWINPUT ri = {};
    ri.header.dwType = RIM_TYPEKEYBOARD;
    ri.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    ri.header.hDevice = g_rawKbdDev;
    ri.header.wParam = RIM_INPUT;
    ri.data.keyboard.MakeCode = (USHORT)sc;
    ri.data.keyboard.Flags = (USHORT)((down ? RI_KEY_MAKE : RI_KEY_BREAK) | (ext ? RI_KEY_E0 : 0));
    ri.data.keyboard.VKey = (USHORT)vk;
    ri.data.keyboard.Message = down ? WM_KEYDOWN : WM_KEYUP;
    EmitRaw(ri, target);
}
void RawMouse(LONG dx, LONG dy, USHORT btnFlags, SHORT wheel, HWND target) {
    RAWINPUT ri = {};
    ri.header.dwType = RIM_TYPEMOUSE;
    ri.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    ri.header.hDevice = g_rawMouseDev;
    ri.header.wParam = RIM_INPUT;
    ri.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    ri.data.mouse.usButtonFlags = btnFlags;
    ri.data.mouse.usButtonData = (USHORT)wheel;
    ri.data.mouse.lLastX = dx; ri.data.mouse.lLastY = dy;
    EmitRaw(ri, target);
}

// ----------------------------------------------------------------------------------------------
// DirectInput synthesis
// ----------------------------------------------------------------------------------------------
std::mutex g_diMx;
std::deque<std::pair<DWORD, DWORD>> g_diKbdQ, g_diMouseQ;  // (ofs, data)
std::atomic<DWORD> g_diSeq{1};
void DiPush(bool kbd, DWORD ofs, DWORD data) {
    if (kbd ? C.di_data_kbd == 0 : C.di_data_mouse == 0) return;  // nobody reads buffered data
    std::lock_guard<std::mutex> lk(g_diMx);
    auto& q = kbd ? g_diKbdQ : g_diMouseQ;
    if (q.size() < 512) q.push_back({ofs, data});
}

// ----------------------------------------------------------------------------------------------
// key helpers
// ----------------------------------------------------------------------------------------------
bool IsExtendedVk(int vk) {
    switch (vk) {
    case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_NUMLOCK: case VK_DIVIDE: case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_SNAPSHOT:
        return true;
    }
    return false;
}
UINT ScanOf(int vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (vk == VK_RSHIFT) sc = 0x36;
    return sc & 0xFF;
}
void SetVkState(int vk, bool down) {
    if (vk <= 0 || vk > 255) return;
    g_keyTrace[vk].injection(down, GetTickCount64());
    g_vk[vk] = down ? 1 : 0;
    if (down) g_vkPressed[vk] = 1;
    auto mirror = [](int generic, int l, int r) { g_vk[generic] = (g_vk[l] || g_vk[r]) ? 1 : 0; if (g_vk[generic]) g_vkPressed[generic] = 1; };
    switch (vk) {
    case VK_SHIFT: g_vk[VK_LSHIFT] = down; break;
    case VK_CONTROL: g_vk[VK_LCONTROL] = down; break;
    case VK_MENU: g_vk[VK_LMENU] = down; break;
    case VK_LSHIFT: case VK_RSHIFT: mirror(VK_SHIFT, VK_LSHIFT, VK_RSHIFT); break;
    case VK_LCONTROL: case VK_RCONTROL: mirror(VK_CONTROL, VK_LCONTROL, VK_RCONTROL); break;
    case VK_LMENU: case VK_RMENU: mirror(VK_MENU, VK_LMENU, VK_RMENU); break;
    }
}
WPARAM MouseKeyFlags() {
    WPARAM f = 0;
    if (g_vk[VK_LBUTTON]) f |= MK_LBUTTON; if (g_vk[VK_RBUTTON]) f |= MK_RBUTTON; if (g_vk[VK_MBUTTON]) f |= MK_MBUTTON;
    if (g_vk[VK_XBUTTON1]) f |= MK_XBUTTON1; if (g_vk[VK_XBUTTON2]) f |= MK_XBUTTON2;
    if (g_vk[VK_SHIFT]) f |= MK_SHIFT; if (g_vk[VK_CONTROL]) f |= MK_CONTROL;
    return f;
}
LPARAM ClientLParam() { return MAKELPARAM((SHORT)g_vcurX.load(), (SHORT)g_vcurY.load()); }

bool MsgsWanted(bool kbd, const RawReg& rr) {
    int m = g_cfg.msgmode; if (m == 1) return true; if (m == 2) return false;
    return kbd ? !(rr.kbd && rr.kbdNoLegacy) : !(rr.mouse && rr.mouseNoLegacy);
}
bool RawWanted(bool kbd, const RawReg& rr) {
    int m = g_cfg.rawmode; if (m == 3) return false; if (m == 1 || m == 2) return true;
    return kbd ? rr.kbd : rr.mouse;
}

void MouseButton(int vk, bool down);

void KeyEvent(int vk, bool down) {
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) { MouseButton(vk, down); return; }
    SetVkState(vk, down);
    UINT sc = ScanOf(vk); bool ext = IsExtendedVk(vk);
    DiPush(true, sc | (ext ? 0x80 : 0), down ? 0x80 : 0);
    RawReg rr = QueryRawReg();
    if (RawWanted(true, rr)) RawKey(vk, down, sc, ext, rr.kbdTarget);
    if (MsgsWanted(true, rr) && g_hwnd) {
        bool sys = g_vk[VK_MENU] && vk != VK_MENU ? true : (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10);
        UINT msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
        LPARAM lp = 1 | ((LPARAM)sc << 16) | (ext ? (1 << 24) : 0) | (g_vk[VK_MENU] ? (1 << 29) : 0);
        if (!down) lp |= (1u << 30) | (1u << 31);
        PostOurs(g_hwnd, msg, (WPARAM)vk, lp);
    }
}

void MouseButton(int vk, bool down) {
    SetVkState(vk, down);
    RawReg rr = QueryRawReg();
    USHORT bf = 0; UINT msg = 0; WPARAM extra = 0; int dib = 0;
    switch (vk) {
    case VK_LBUTTON: bf = down ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP; msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; dib = 0; break;
    case VK_RBUTTON: bf = down ? RI_MOUSE_RIGHT_BUTTON_DOWN : RI_MOUSE_RIGHT_BUTTON_UP; msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; dib = 1; break;
    case VK_MBUTTON: bf = down ? RI_MOUSE_MIDDLE_BUTTON_DOWN : RI_MOUSE_MIDDLE_BUTTON_UP; msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; dib = 2; break;
    case VK_XBUTTON1: bf = down ? RI_MOUSE_BUTTON_4_DOWN : RI_MOUSE_BUTTON_4_UP; msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP; extra = (WPARAM)XBUTTON1 << 16; dib = 3; break;
    case VK_XBUTTON2: bf = down ? RI_MOUSE_BUTTON_5_DOWN : RI_MOUSE_BUTTON_5_UP; msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP; extra = (WPARAM)XBUTTON2 << 16; dib = 4; break;
    }
    DiPush(false, DIMOFS_BUTTON0 + dib, down ? 0x80 : 0);
    if (RawWanted(false, rr)) RawMouse(0, 0, bf, 0, rr.mouseTarget);
    if (MsgsWanted(false, rr) && g_hwnd) PostOurs(g_hwnd, msg, MouseKeyFlags() | extra, ClientLParam());
}

void ClampToClient(LONG& x, LONG& y) {
    RECT rc{}; if (!g_hwnd || !GetClientRect(g_hwnd, &rc)) return;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x > rc.right - 1) x = rc.right - 1; if (y > rc.bottom - 1) y = rc.bottom - 1;
}
struct AppliedMouse { long long dx=0, dy=0; };
AppliedMouse MouseMoveTo(LONG x, LONG y, bool relativeOrigin, LONG rdx, LONG rdy, bool unbounded=false) {
    LONG ox = g_vcurX, oy = g_vcurY;
    LONG dx = relativeOrigin ? rdx : x - ox, dy = relativeOrigin ? rdy : y - oy;
    long long wideX = relativeOrigin ? (long long)ox + rdx : x, wideY = relativeOrigin ? (long long)oy + rdy : y;
    LONG nx = (LONG)(std::max)(-2147483648LL, (std::min)(2147483647LL, wideX));
    LONG ny = (LONG)(std::max)(-2147483648LL, (std::min)(2147483647LL, wideY));
    if (!unbounded) ClampToClient(nx, ny);
    g_vcurX = nx; g_vcurY = ny; g_vcurTouched = true;
    g_diDX += dx; g_diDY += dy;
    DiPush(false, DIMOFS_X, (DWORD)dx); DiPush(false, DIMOFS_Y, (DWORD)dy);
    RawReg rr = QueryRawReg();
    if (RawWanted(false, rr) && (dx || dy)) RawMouse(dx, dy, 0, 0, rr.mouseTarget);
    if (MsgsWanted(false, rr) && g_hwnd) PostOurs(g_hwnd, WM_MOUSEMOVE, MouseKeyFlags(), ClientLParam());
    return {(long long)nx-ox, (long long)ny-oy};
}
void MouseWheel(int delta, bool horizontal) {
    g_diDZ += delta; DiPush(false, DIMOFS_Z, (DWORD)delta);
    RawReg rr = QueryRawReg();
    if (RawWanted(false, rr)) RawMouse(0, 0, horizontal ? RI_MOUSE_HWHEEL : RI_MOUSE_WHEEL, (SHORT)delta, rr.mouseTarget);
    if (MsgsWanted(false, rr) && g_hwnd) {
        POINT p{g_vcurX, g_vcurY}; ClientToScreen(g_hwnd, &p);
        PostOurs(g_hwnd, horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL, MAKEWPARAM((WORD)MouseKeyFlags(), (SHORT)delta), MAKELPARAM((SHORT)p.x, (SHORT)p.y));
    }
}

// ----------------------------------------------------------------------------------------------
// user32 detours
// ----------------------------------------------------------------------------------------------
HWND WINAPI h_GetForegroundWindow() {
    C.gfg++;
    HWND r = o_GetForegroundWindow();
    if (g_cfg.fakefocus && g_hwnd && !IsOurWindow(r)) return g_hwnd;
    return r;
}
HWND WINAPI h_GetActiveWindow() {
    HWND r = o_GetActiveWindow();
    if (!r && g_cfg.fakefocus && g_hwnd && GetCurrentThreadId() == GetWindowThreadProcessId(g_hwnd, nullptr)) return g_hwnd;
    return r;
}
HWND WINAPI h_GetFocus() {
    HWND r = o_GetFocus();
    if (!r && g_cfg.fakefocus && g_hwnd && GetCurrentThreadId() == GetWindowThreadProcessId(g_hwnd, nullptr)) return g_hwnd;
    return r;
}
BOOL WINAPI h_SetForegroundWindow(HWND h) {
    C.setfg++;
    Log("trace SetForegroundWindow(%p) fg=%d", h, (int)RealFg());
    if (g_cfg.blockfocus && !RealFg()) { C.setfg_blocked++; return TRUE; }
    return o_SetForegroundWindow(h);
}
using PFN_SetFocus = HWND(WINAPI*)(HWND);
PFN_SetFocus o_SetFocus = nullptr, o_SetActiveWindow = nullptr;
std::atomic<long> g_setFocusCalls{0}, g_setActiveCalls{0};
HWND WINAPI h_SetActiveWindow(HWND h) {
    g_setActiveCalls++;
    if (g_cfg.blockfocus && !RealFg()) { Log("SetActiveWindow(%p) while in background -> suppressed", h); C.setfg_blocked++; return o_GetActiveWindow ? o_GetActiveWindow() : nullptr; }
    return o_SetActiveWindow(h);
}
HWND WINAPI h_SetFocus(HWND h) {
    g_setFocusCalls++;
    if (!RealFg()) Log("trace SetFocus(%p) in background", h);
    // SetFocus on a window whose top-level is not active ACTIVATES it (and can take the foreground when the
    // system has no foreground window). While in the background only allow it inside the already-active tree.
    if (g_cfg.blockfocus && !RealFg() && h) {
        HWND act = o_GetActiveWindow ? o_GetActiveWindow() : nullptr;
        if (!act || GetAncestor(h, GA_ROOT) != act) { C.setfg_blocked++; return o_GetFocus ? o_GetFocus() : nullptr; }
    }
    return o_SetFocus(h);
}
BOOL WINAPI h_BringWindowToTop(HWND h) {
    if (g_cfg.blockfocus && !RealFg()) { C.setfg_blocked++; return TRUE; }
    return o_BringWindowToTop(h);
}
BOOL WINAPI h_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT f) {
    if (!(f & SWP_NOACTIVATE) && IsOurWindow(h) && !GetParent(h)) Log("trace SetWindowPos(%p, after=%p, flags=0x%X) fg=%d", h, after, f, (int)RealFg());
    if (g_cfg.blockfocus && !RealFg() && IsOurWindow(h)) {
        f |= SWP_NOACTIVATE;
        if (after == HWND_TOPMOST || after == HWND_TOP) f |= SWP_NOZORDER;
    }
    return o_SetWindowPos(h, after, x, y, cx, cy, f);
}
BOOL WINAPI h_ShowWindow(HWND h, int cmd) {
    if (g_cfg.keepvisible && !RealFg() && h == g_hwnd && (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE)) {
        C.min_blocked++; Log("blocked game's own ShowWindow(%d) minimise while in background", cmd); return TRUE;
    }
    if (IsOurWindow(h) && !GetParent(h)) Log("trace ShowWindow(%p, %d) fg=%d", h, cmd, (int)RealFg());
    if (g_cfg.blockfocus && !RealFg() && g_hwnd) {
        if (cmd == SW_SHOW) cmd = SW_SHOWNA; else if (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE || cmd == SW_SHOWDEFAULT) cmd = SW_SHOWNOACTIVATE;
    }
    return o_ShowWindow(h, cmd);
}
SHORT WINAPI h_GetAsyncKeyState(int vk) {
    C.gaks++;
    SHORT r = Isolating() ? 0 : o_GetAsyncKeyState(vk);
    if (vk > 0 && vk < 256) {
        if (g_vk[vk]) r |= (SHORT)0x8000;
        if (g_vkPressed[vk].exchange(0)) r |= 1;
        g_keyTrace[vk].gaks.record((USHORT)r, (r & 0x8000) != 0);
    }
    return r;
}
SHORT WINAPI h_GetKeyState(int vk) {
    C.gks++;
    SHORT r = o_GetKeyState(vk);
    if (Isolating()) r &= 1;  // keep toggle bit only
    if (vk > 0 && vk < 256 && g_vk[vk]) r |= (SHORT)0xFF80;
    if (vk > 0 && vk < 256) g_keyTrace[vk].gks.record((USHORT)r, (r & 0x8000) != 0);
    return r;
}
BOOL WINAPI h_GetKeyboardState(PBYTE ks) {
    C.gkbs++;
    BOOL r = o_GetKeyboardState(ks);
    if (ks) {
        if (!r) { memset(ks, 0, 256); r = TRUE; }
        bool iso = Isolating();
        for (int i = 0; i < 256; i++) { if (iso) ks[i] &= 1; if (g_vk[i]) ks[i] |= 0x80; g_keyTrace[i].gkbs.record(ks[i]); }
    }
    return r;
}
BOOL WINAPI h_GetCursorPos(LPPOINT p) {
    C.gcur++;
    if (p && UseVirtualCursor()) { POINT c{g_vcurX, g_vcurY}; ClientToScreen(g_hwnd, &c); *p = c; return TRUE; }
    return o_GetCursorPos(p);
}
BOOL WINAPI h_GetPhysicalCursorPos(LPPOINT p) {
    if (p && UseVirtualCursor()) { POINT c{g_vcurX, g_vcurY}; ClientToScreen(g_hwnd, &c); *p = c; return TRUE; }
    return o_GetPhysicalCursorPos(p);
}
BOOL WINAPI h_SetCursorPos(int x, int y) {
    C.scur++;
    if (UseVirtualCursor()) { POINT p{x, y}; ScreenToClient(g_hwnd, &p); g_vcurX = p.x; g_vcurY = p.y; return TRUE; }
    return o_SetCursorPos(x, y);
}
BOOL WINAPI h_SetPhysicalCursorPos(int x, int y) {
    if (UseVirtualCursor()) { POINT p{x, y}; ScreenToClient(g_hwnd, &p); g_vcurX = p.x; g_vcurY = p.y; return TRUE; }
    return o_SetPhysicalCursorPos(x, y);
}
BOOL WINAPI h_ClipCursor(const RECT* r) {
    C.clip++;
    if (r && !RealFg()) return TRUE;  // a background game must never trap the real cursor
    return o_ClipCursor(r);
}
HWND WINAPI h_WindowFromPoint(POINT pt) {
    if (UseVirtualCursor()) {
        POINT c{g_vcurX, g_vcurY}; ClientToScreen(g_hwnd, &c);
        if (c.x == pt.x && c.y == pt.y) return g_hwnd;
    }
    return o_WindowFromPoint(pt);
}
UINT WINAPI h_GetRawInputData(HRAWINPUT h, UINT cmd, LPVOID data, PUINT pcb, UINT hdr) {
    C.rid++;
    if (IsMagic(h)) {
        C.rid_magic++;
        const RAWINPUT& r = g_rawRing[(ULONG_PTR)h & 0xFFF];
        UINT need = (cmd == RID_HEADER) ? (UINT)sizeof(RAWINPUTHEADER) : r.header.dwSize;
        if (!pcb) return (UINT)-1;
        if (!data) { *pcb = need; return 0; }
        if (*pcb < need) { *pcb = need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        memcpy(data, &r, need);
        if (cmd == RID_INPUT && r.header.dwType == RIM_TYPEKEYBOARD && r.data.keyboard.VKey < 256)
            g_keyTrace[r.data.keyboard.VKey].raw_data.record(r.data.keyboard.Flags, !(r.data.keyboard.Flags & RI_KEY_BREAK));
        return need;
    }
    UINT result = o_GetRawInputData(h, cmd, data, pcb, hdr);
    if (result != (UINT)-1 && cmd == RID_INPUT && data && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
        const RAWINPUT& r = *(const RAWINPUT*)data;
        if (r.header.dwType == RIM_TYPEKEYBOARD && r.data.keyboard.VKey < 256)
            g_keyTrace[r.data.keyboard.VKey].raw_data.record(r.data.keyboard.Flags, !(r.data.keyboard.Flags & RI_KEY_BREAK));
    }
    return result;
}
UINT WINAPI h_GetRawInputBuffer(PRAWINPUT data, PUINT pcb, UINT hdr) {
    C.rib++;
    g_lastRIB = GetTickCount();
    UINT n = o_GetRawInputBuffer(data, pcb, hdr);
#ifdef _WIN64
    bool pending; { std::lock_guard<std::mutex> lk(g_rawQMx); pending = !g_rawQ.empty(); }
    if (!data) { if (pcb && pending && *pcb < sizeof(RAWINPUT)) *pcb = sizeof(RAWINPUT); return n; }
    if (n == (UINT)-1) { if (!pending) return n; n = 0; }
    if (Isolating()) n = 0;  // drop real records while in the background
    if (!pending || !pcb) return n;
    BYTE* base = (BYTE*)data; BYTE* end = base + *pcb;
    BYTE* p = base;
    for (UINT i = 0; i < n; i++) p = (BYTE*)NEXTRAWINPUTBLOCK((PRAWINPUT)p);
    std::lock_guard<std::mutex> lk(g_rawQMx);
    while (!g_rawQ.empty()) {
        const RAWINPUT& r = g_rawQ.front();
        if (p + r.header.dwSize > end) break;
        memcpy(p, &r, r.header.dwSize);
        p = (BYTE*)NEXTRAWINPUTBLOCK((PRAWINPUT)p);
        g_rawQ.pop_front(); n++; C.rib_injected++;
    }
    BYTE* traced = base;
    for (UINT i = 0; i < n; ++i) {
        const RAWINPUT& r = *(PRAWINPUT)traced;
        if (r.header.dwType == RIM_TYPEKEYBOARD && r.data.keyboard.VKey < 256)
            g_keyTrace[r.data.keyboard.VKey].raw_buffer.record(r.data.keyboard.Flags, !(r.data.keyboard.Flags & RI_KEY_BREAK));
        traced = (BYTE*)NEXTRAWINPUTBLOCK((PRAWINPUT)traced);
    }
#endif
    return n;
}
BOOL WINAPI h_RegisterRawInputDevices(PCRAWINPUTDEVICE d, UINT n, UINT cb) {
    C.reg_raw++;
    for (UINT i = 0; d && i < n; i++) Log("RegisterRawInputDevices page=%u usage=%u flags=0x%X target=%p", d[i].usUsagePage, d[i].usUsage, d[i].dwFlags, d[i].hwndTarget);
    return o_RegisterRawInputDevices(d, n, cb);
}

// ----------------------------------------------------------------------------------------------
// window subclass: fake focus + isolation of real mouse/raw input in the background
// ----------------------------------------------------------------------------------------------
LRESULT CallOrig(HWND h, UINT m, WPARAM w, LPARAM l) {
    return g_hwndUnicode ? CallWindowProcW(g_origWndProc, h, m, w, l) : CallWindowProcA(g_origWndProc, h, m, w, l);
}
LRESULT CALLBACK h_WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    bool fg = ComputeRealFg();
    g_realFg = fg;
    if (g_cfg.fakefocus) {
        switch (m) {
        case WM_ACTIVATEAPP: if (!w) { C.act_swallowed++; return 0; } break;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !IsOurWindow((HWND)l)) { C.act_swallowed++; return 0; } break;
        case WM_NCACTIVATE: if (!w && !fg) { C.act_swallowed++; return TRUE; } break;
        case WM_KILLFOCUS: if (!IsOurWindow((HWND)w)) { C.act_swallowed++; return 0; } break;
        }
    }
    if (m == WM_ACTIVATEAPP) { g_appActive = w ? 1 : 0; if (!w) g_synthLeft = 3; }   // reaches the game (a swallowed deactivation returned above)
    if (m == WM_SYSCOMMAND && (w & 0xFFF0) == SC_MINIMIZE) {
        Log("WM_SYSCOMMAND SC_MINIMIZE received (fg=%d)", (int)fg);
        if (g_cfg.keepvisible && !fg) { C.min_blocked++; return 0; }
    }
    if ((m == WM_ACTIVATE && LOWORD(w) != WA_INACTIVE) || (m == WM_ACTIVATEAPP && w) || m == WM_SETFOCUS || m == WM_MOUSEACTIVATE)
        Log("trace msg 0x%X w=0x%llX l=0x%llX realfg=%d", m, (unsigned long long)w, (unsigned long long)l, (int)fg);
    if (m == WM_SIZE && w == SIZE_MINIMIZED) Log("window minimised (WM_SIZE SIZE_MINIMIZED, fg=%d)", (int)fg);
    if ((m >= WM_KEYFIRST && m <= WM_KEYLAST)) {
        if (m == WM_CHAR || m == WM_SYSCHAR) C.wm_char++; else C.wm_key++;
        bool ours = RingConsume(m, w, l);
        if (ours && w > 0 && w < 256 && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_KEYUP || m == WM_SYSKEYUP))
            g_keyTrace[w].delivery(m == WM_KEYDOWN || m == WM_SYSKEYDOWN);
    }
    if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) {
        C.wm_mouse++;
        bool ours = RingConsume(m, w, l);
        if (!ours && g_cfg.isolate && !fg) { C.wm_mouse_dropped++; return 0; }
        if (ours && m == WM_MOUSEMOVE) g_mouseMovesDelivered++;
    }
    if (m == WM_INPUT) {
        C.wm_input++;
        if (!IsMagic((HRAWINPUT)l) && g_cfg.isolate && !fg) {
            C.wm_input_dropped++;
            return g_hwndUnicode ? DefWindowProcW(h, m, w, l) : DefWindowProcA(h, m, w, l);
        }
        if (IsMagic((HRAWINPUT)l)) {
            // let the game read it; DefWindowProc on a fake handle is harmless but skip it anyway
            LRESULT r = CallOrig(h, m, w, l);
            return r;
        }
    }
    return CallOrig(h, m, w, l);
}

// ----------------------------------------------------------------------------------------------
// DirectInput detours (vtable functions shared by all devices of dinput8)
// ----------------------------------------------------------------------------------------------
using PFN_DIGDS = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using PFN_DIGDD = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using PFN_DIACQ = HRESULT(STDMETHODCALLTYPE*)(void*);
using PFN_DICAPS = HRESULT(STDMETHODCALLTYPE*)(void*, LPDIDEVCAPS);
PFN_DIGDS o_DIGetDeviceState = nullptr; PFN_DIGDD o_DIGetDeviceData = nullptr; PFN_DIACQ o_DIAcquire = nullptr;
// legacy dinput.dll (IDirectInputDeviceA, DirectInput 1-7): keyboard and mouse may use different vtable functions
constexpr int kDiLegacySets = 2;
PFN_DIGDS o_DILGetDeviceState[kDiLegacySets] = {}; PFN_DIGDD o_DILGetDeviceData[kDiLegacySets] = {}; PFN_DIACQ o_DILAcquire[kDiLegacySets] = {};
bool g_diHooked = false;
SRWLOCK g_devLock = SRWLOCK_INIT;
std::map<void*, int> g_devType;  // 1 kbd, 2 mouse, 0 other
int DevType(void* self) {
    AcquireSRWLockShared(&g_devLock);
    auto it = g_devType.find(self); int t = it == g_devType.end() ? -1 : it->second;
    ReleaseSRWLockShared(&g_devLock);
    if (t >= 0) return t;
    DIDEVCAPS caps = {}; caps.dwSize = sizeof(caps);
    PFN_DICAPS gc = (PFN_DICAPS)(*(void***)self)[3];
    t = 0;
    HRESULT chr = gc(self, &caps);
    if (FAILED(chr)) { caps = {}; caps.dwSize = 24; chr = gc(self, &caps); }  // DirectInput 3 devices only accept the 24-byte DIDEVCAPS
    if (SUCCEEDED(chr)) { BYTE dt = GET_DIDEVICE_TYPE(caps.dwDevType); t = (dt == DI8DEVTYPE_KEYBOARD || dt == 3 /*DIDEVTYPE_KEYBOARD*/) ? 1 : (dt == DI8DEVTYPE_MOUSE || dt == 2 /*DIDEVTYPE_MOUSE*/) ? 2 : 0; }  // legacy dinput.dll reports 3 / 2, dinput8 0x13 / 0x12
    AcquireSRWLockExclusive(&g_devLock); g_devType[self] = t; ReleaseSRWLockExclusive(&g_devLock);
    return t;
}
HRESULT DIAcquireImpl(PFN_DIACQ orig, void* self) {
    C.di_acq++;
    HRESULT hr = orig(self);
    if (FAILED(hr) && Faking()) { C.di_fake++; return DI_OK; }
    return hr;
}
HRESULT DIGetDeviceStateImpl(PFN_DIGDS orig, void* self, DWORD cb, LPVOID data) {
    HRESULT hr = orig(self, cb, data);
    if (!data) return hr;
    bool kbd = cb == 256, mouse = cb == sizeof(DIMOUSESTATE) || cb == sizeof(DIMOUSESTATE2);
    if (!kbd && !mouse) return hr;
    if (FAILED(hr)) { if (!Faking()) return hr; memset(data, 0, cb); hr = DI_OK; C.di_fake++; }
    else if (Isolating()) memset(data, 0, cb);
    if (kbd) {
        C.di_kbd++;
        BYTE* k = (BYTE*)data;
        for (int vk = 1; vk < 256; vk++) if (g_vk[vk] && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk > 6) {
            UINT sc = ScanOf(vk) | (IsExtendedVk(vk) ? 0x80 : 0); if (sc && sc < 256) k[sc] = 0x80;
        }
        for (int vk = 7; vk < 256; ++vk) {
            UINT sc = ScanOf(vk) | (IsExtendedVk(vk) ? 0x80 : 0);
            if (sc && sc < 256) g_keyTrace[vk].di_state.record(k[sc]);
        }
    } else {
        C.di_mouse++;
        DIMOUSESTATE2* m = (DIMOUSESTATE2*)data;
        m->lX += g_diDX.exchange(0); m->lY += g_diDY.exchange(0); m->lZ += g_diDZ.exchange(0);
        if (g_vk[VK_LBUTTON]) m->rgbButtons[0] = 0x80; if (g_vk[VK_RBUTTON]) m->rgbButtons[1] = 0x80;
        if (g_vk[VK_MBUTTON]) m->rgbButtons[2] = 0x80; if (g_vk[VK_XBUTTON1]) m->rgbButtons[3] = 0x80;
        // trace: what the game was actually handed for each mouse button (shows in `trace key lbutton`)
        g_keyTrace[VK_LBUTTON].di_state.record(m->rgbButtons[0]); g_keyTrace[VK_RBUTTON].di_state.record(m->rgbButtons[1]);
        g_keyTrace[VK_MBUTTON].di_state.record(m->rgbButtons[2]);
    }
    return hr;
}
HRESULT DIGetDeviceDataImpl(PFN_DIGDD orig, void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags) {
    DWORD req = inout ? *inout : 0;  // caller's capacity (the real call overwrites it with the count)
    HRESULT hr = orig(self, cbObj, rg, inout, flags);
    int t = DevType(self);
    if (t == 0 || !inout) return hr;
    if (t == 1) C.di_data_kbd++; else C.di_data_mouse++;
    if (FAILED(hr)) { if (!Faking()) return hr; hr = DI_OK; *inout = 0; C.di_fake++; }
    if (!rg || (flags & DIGDD_PEEK) || cbObj < 16) return hr;
    DWORD have = *inout;
    if (Isolating()) have = 0;
    std::lock_guard<std::mutex> lk(g_diMx);
    auto& q = t == 1 ? g_diKbdQ : g_diMouseQ;
    while (!q.empty() && have < req) {
        BYTE* slot = (BYTE*)rg + (size_t)have * cbObj;
        memset(slot, 0, cbObj);
        DIDEVICEOBJECTDATA* d = (DIDEVICEOBJECTDATA*)slot;
        d->dwOfs = q.front().first; d->dwData = q.front().second; d->dwTimeStamp = GetTickCount(); d->dwSequence = g_diSeq++;
        if (t == 1) {
            UINT scan = (d->dwOfs & 0x7f) | ((d->dwOfs & 0x80) ? 0xe000 : 0);
            UINT vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
            if (vk > 0 && vk < 256) g_keyTrace[vk].di_buffer.record((int)d->dwData);
        }
        q.pop_front(); have++;
    }
    *inout = have;
    return hr;
}

HRESULT STDMETHODCALLTYPE h_DIAcquire(void* self) { return DIAcquireImpl(o_DIAcquire, self); }
HRESULT STDMETHODCALLTYPE h_DIGetDeviceState(void* self, DWORD cb, LPVOID data) { return DIGetDeviceStateImpl(o_DIGetDeviceState, self, cb, data); }
HRESULT STDMETHODCALLTYPE h_DIGetDeviceData_entry(void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags) { return DIGetDeviceDataImpl(o_DIGetDeviceData, self, cbObj, rg, inout, flags); }
template<int N> HRESULT STDMETHODCALLTYPE h_DILAcquire(void* self) { return DIAcquireImpl(o_DILAcquire[N], self); }
template<int N> HRESULT STDMETHODCALLTYPE h_DILGetDeviceState(void* self, DWORD cb, LPVOID data) { return DIGetDeviceStateImpl(o_DILGetDeviceState[N], self, cb, data); }
template<int N> HRESULT STDMETHODCALLTYPE h_DILGetDeviceData(void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA rg, LPDWORD inout, DWORD flags) { return DIGetDeviceDataImpl(o_DILGetDeviceData[N], self, cbObj, rg, inout, flags); }

// ----------------------------------------------------------------------------------------------
// XInput
// ----------------------------------------------------------------------------------------------
struct XGP { WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XST { DWORD pkt; XGP gp; };
using PFN_XGS = DWORD(WINAPI*)(DWORD, XST*);
// slots 0-3: XInputGetState of each DLL; 4-7: XInputGetStateEx (ordinal 100, same struct, adds the Guide button)
PFN_XGS o_XGS[8] = {};
bool g_xHooked[4] = {};
const wchar_t* kXinputDlls[4] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinputuap.dll"};
// axes the last pad command set explicitly (bit 0 lt,1 rt,2 lx,3 ly,4 rx,5 ry): a set axis overrides the real pad even
// when the value is 0 (centres a drifting stick); an unset axis passes the real pad through
std::atomic<int> g_padSetMask{0};
DWORD XGSCommon(int i, DWORD user, XST* s) {
    C.xinput++;
    DWORD r = o_XGS[i](user, s);
    if (user != 0 || !s || !g_padActive) return r;
    if (r != ERROR_SUCCESS) { memset(s, 0, sizeof(*s)); r = ERROR_SUCCESS; }
    if (Isolating()) memset(&s->gp, 0, sizeof(s->gp));
    int m = g_padSetMask;
    s->gp.b |= (WORD)(i < 4 ? (g_padButtons & ~0x400) : g_padButtons);   // the documented API never reports Guide
    if (m & 1) s->gp.lt = (BYTE)g_padLT; if (m & 2) s->gp.rt = (BYTE)g_padRT;
    if (m & 4) s->gp.lx = (SHORT)g_padLX; if (m & 8) s->gp.ly = (SHORT)g_padLY;
    if (m & 16) s->gp.rx = (SHORT)g_padRX; if (m & 32) s->gp.ry = (SHORT)g_padRY;
    s->pkt = g_padPacket;
    return r;
}
template <int I> DWORD WINAPI h_XGS(DWORD user, XST* s) { return XGSCommon(I, user, s); }
void* kXgsDetours[8] = {(void*)&h_XGS<0>, (void*)&h_XGS<1>, (void*)&h_XGS<2>, (void*)&h_XGS<3>,
                        (void*)&h_XGS<4>, (void*)&h_XGS<5>, (void*)&h_XGS<6>, (void*)&h_XGS<7>};

// ----------------------------------------------------------------------------------------------
// DXGI: Present hook + capture
// ----------------------------------------------------------------------------------------------
using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using PFN_SetFS = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL, IDXGIOutput*);
PFN_Present o_Present = nullptr; PFN_Present1 o_Present1 = nullptr; PFN_SetFS o_SetFullscreenState = nullptr;

std::atomic<LONG> g_capReq{0};  // 0 idle, 1 requested, 2 in progress
HANDLE g_capEvent = nullptr;
std::vector<BYTE> g_capPix; UINT g_capW = 0, g_capH = 0; std::string g_capErr; DXGI_FORMAT g_capFmt = DXGI_FORMAT_UNKNOWN;
std::atomic<UINT> g_scW{0}, g_scH{0}; std::atomic<int> g_scFmt{0}; std::atomic<int> g_scApi{0}; // 11 / 12 / 0
std::atomic<long> g_capturedPresent{0};
LONG g_captureOriginX = 0, g_captureOriginY = 0, g_captureClientW = 0, g_captureClientH = 0;

float HalfToFloat(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = std::ldexp((float)m, -24); else if (e == 31) v = m ? 0.f : 65504.f; else v = std::ldexp((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
BYTE LinToSrgb8(float v) {
    if (v <= 0) return 0; if (v >= 1) return 255;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
    return (BYTE)(s * 255.f + 0.5f);
}
DXGI_FORMAT Typed(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
bool ConvertRow(DXGI_FORMAT f, const BYTE* src, BYTE* dst, UINT w) {
    switch (Typed(f)) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        for (UINT x = 0; x < w; x++) { dst[4 * x] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1]; dst[4 * x + 2] = src[4 * x]; dst[4 * x + 3] = 255; } return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        for (UINT x = 0; x < w; x++) { memcpy(dst + 4 * x, src + 4 * x, 3); dst[4 * x + 3] = 255; } return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        for (UINT x = 0; x < w; x++) { uint32_t v = ((const uint32_t*)src)[x]; dst[4 * x + 2] = (BYTE)((v & 1023) >> 2); dst[4 * x + 1] = (BYTE)(((v >> 10) & 1023) >> 2); dst[4 * x] = (BYTE)(((v >> 20) & 1023) >> 2); dst[4 * x + 3] = 255; } return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        for (UINT x = 0; x < w; x++) { const uint16_t* p = (const uint16_t*)src + 4 * x; dst[4 * x + 2] = LinToSrgb8(HalfToFloat(p[0])); dst[4 * x + 1] = LinToSrgb8(HalfToFloat(p[1])); dst[4 * x] = LinToSrgb8(HalfToFloat(p[2])); dst[4 * x + 3] = 255; } return true;
    default: return false;
    }
}

bool SwapchainIsOurs(IDXGISwapChain* sc) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d))) return false;
    g_scW = d.BufferDesc.Width; g_scH = d.BufferDesc.Height; g_scFmt = (int)d.BufferDesc.Format;
    if (!g_hwnd) return true;
    if (!d.OutputWindow) return true;   // CoreWindow / composition swapchain (UWP): no HWND to compare
    return d.OutputWindow == g_hwnd || GetAncestor(d.OutputWindow, GA_ROOT) == g_hwnd;
}
// ---- shared native-capture plumbing (DXGI 11/12, OpenGL, Vulkan) ----
// API codes: 11 d3d11, 12 d3d12, 1 opengl, 2 vulkan, 0 none/unknown, -1 foreign (DXGI swapchain owned by a GL/VK driver)
enum { API_NONE = 0, API_GL = 1, API_VK = 2, API_D3D11 = 11, API_D3D12 = 12, API_FOREIGN = -1 };
const char* ApiName(int a) {
    switch (a) { case API_D3D11: return "d3d11"; case API_D3D12: return "d3d12"; case API_GL: return "gl"; case API_VK: return "vk"; case API_FOREIGN: return "foreign"; default: return "none"; }
}
std::atomic<int> g_frameApi{0};        // API of the frames counted in present_ours (last one seen)
std::atomic<int> g_capApi{0};          // API that served the last capture request (set with g_capReq -> done)
std::atomic<int> g_capPending{0};      // API whose GPU readback the requester must finish (12 / vk), 0 = pixels ready
std::atomic<bool> g_capRetryable{false}; // the hook failed for a transient reason (queue hook still installing)
std::atomic<long> g_frameGl{0}, g_frameVk{0}, g_frame11{0}, g_frame12{0}, g_frameForeign{0};
bool HookAddr(const char* name, void* target, void* det, void** orig);
void OnFrame(int api) {
    C.present_ours++;
    g_frameApi = api;
    switch (api) { case API_GL: g_frameGl++; break; case API_VK: g_frameVk++; break; case API_D3D11: g_frame11++; break; case API_D3D12: g_frame12++; break; }
}
// Claims a pending capture request for this frame. Caller must then fill g_capPix/g_capErr (or set g_capPending)
// and call EndCapture().
bool BeginCaptureClaim() {
    LONG exp = 1;
    if (!g_capReq.compare_exchange_strong(exp, 2)) return false;
    g_capErr.clear(); g_capPix.clear(); g_capW = g_capH = 0; g_capPending = 0; g_capRetryable = false;
    return true;
}
void EndCapture(int api) {
    g_capApi = api;
    g_capturedPresent = C.present_ours.load();
    g_capReq = 0;
    SetEvent(g_capEvent);
}

#include "vi_cap12.h"
#include "vi_capgl.h"
#include "vi_capvk.h"

void CaptureFromD3D11(IDXGISwapChain* sc, ID3D11Device* dev) {
    dev->AddRef();
    ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
    ID3D11Texture2D* bb = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr) || !bb) { g_capErr = "GetBuffer failed"; ctx->Release(); dev->Release(); return; }
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    g_capFmt = td.Format;
    ID3D11Texture2D* src = bb; ID3D11Texture2D* resolved = nullptr;
    if (td.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC rd = td; rd.SampleDesc.Count = 1; rd.SampleDesc.Quality = 0; rd.BindFlags = 0; rd.MiscFlags = 0; rd.Usage = D3D11_USAGE_DEFAULT; rd.CPUAccessFlags = 0;
        if (SUCCEEDED(dev->CreateTexture2D(&rd, nullptr, &resolved))) { ctx->ResolveSubresource(resolved, 0, bb, 0, Typed(td.Format)); src = resolved; }
    }
    D3D11_TEXTURE2D_DESC sd = td; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0; sd.BindFlags = 0; sd.MiscFlags = 0; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MipLevels = 1; sd.ArraySize = 1;
    ID3D11Texture2D* st = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &st))) {
        ctx->CopyResource(st, src);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            g_capPix.resize((size_t)td.Width * td.Height * 4);
            bool ok = true;
            for (UINT y = 0; y < td.Height && ok; y++) ok = ConvertRow(td.Format, (const BYTE*)m.pData + (size_t)y * m.RowPitch, g_capPix.data() + (size_t)y * td.Width * 4, td.Width);
            ctx->Unmap(st, 0);
            if (ok) { g_capW = td.Width; g_capH = td.Height; }
            else { g_capPix.clear(); char b[64]; sprintf_s(b, "unsupported backbuffer format %d", (int)td.Format); g_capErr = b; }
        } else g_capErr = "Map failed";
        st->Release();
    } else g_capErr = "CreateTexture2D(staging) failed";
    if (resolved) resolved->Release();
    bb->Release(); ctx->Release(); dev->Release();
}
// Which API owns this swapchain? Cached per swapchain pointer (re-probed every 256 frames in case the pointer was
// recycled). GL/Vulkan drivers may present through an internal DXGI swapchain; those are "foreign": not counted
// here (the GL/VK hook counts the frame) and never captured through DXGI.
IDXGISwapChain* g_apiSc = nullptr; int g_apiScApi = 0; unsigned g_apiScAge = 0;
int SwapchainApi(IDXGISwapChain* sc, ID3D11Device** d11) {
    *d11 = nullptr;
    ID3D11Device* dev = nullptr;
    bool fresh = sc != g_apiSc || ++g_apiScAge >= 256;
    if (!fresh && g_apiScApi != API_D3D11) return g_apiScApi;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) && dev) { *d11 = dev; g_apiSc = sc; g_apiScApi = API_D3D11; if (fresh) g_apiScAge = 0; return API_D3D11; }
    if (!fresh) return g_apiScApi;
    int api = API_FOREIGN;
    {   // a Vulkan swapchain is live for this window: the DXGI one is the Vulkan driver's (measured: AMD, D3D12-backed)
        std::lock_guard<std::mutex> lk(g_vkMx);
        for (auto& kv : g_vkScs) if (!kv.second.hwnd || kv.second.hwnd == g_hwnd) { g_apiSc = sc; g_apiScApi = API_FOREIGN; g_apiScAge = 0; return API_FOREIGN; }
    }
    IUnknown* u = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D12CommandQueue), (void**)&u)) && u) { api = API_D3D12; u->Release(); }
    else if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D12Device), (void**)&u)) && u) { api = API_D3D12; u->Release(); }
    else if (GetModuleHandleW(L"d3d12.dll") && !GetModuleHandleW(L"opengl32.dll") && !GetModuleHandleW(L"vulkan-1.dll")) api = API_D3D12;  // queue not exposed: ECL fallback
    g_apiSc = sc; g_apiScApi = api; g_apiScAge = 0;
    return api;
}
void OnPresent(IDXGISwapChain* sc) {
    C.present++;
    if (!SwapchainIsOurs(sc)) return;
    if (t_vkIn || t_inSwap) { g_scApi = API_FOREIGN; g_frameForeign++; return; }   // issued by a GL/VK driver inside our hook
    ID3D11Device* d11 = nullptr;
    int api = SwapchainApi(sc, &d11);
    // GL / Vulkan frames are being counted for this window: a DXGI swapchain on it is the driver's, even when its
    // Present runs on another thread (measured on AMD with a Unity GL render thread)
    if ((api == API_D3D11 || api == API_D3D12) && (g_frameGl.load() || g_frameVk.load())) {
        if (d11) { d11->Release(); d11 = nullptr; }
        g_apiScApi = API_FOREIGN; api = API_FOREIGN;
    }
    g_scApi = api;
    if (api == API_FOREIGN) { g_frameForeign++; return; }
    if (api == API_D3D12 && !g_eclInstalled) g_eclWanted = true;   // monitor thread installs the queue hook
    OnFrame(api);
    if (BeginCaptureClaim()) {
        if (api == API_D3D11 && d11) CaptureFromD3D11(sc, d11);
        else if (api == API_D3D12) { if (Begin12(sc, g_capErr)) g_capPending = API_D3D12; }
        else g_capErr = "swapchain device is neither D3D11 nor D3D12";
        EndCapture(api);
    }
    if (d11) d11->Release();
}
HRESULT STDMETHODCALLTYPE h_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) OnPresent(sc);
    return o_Present(sc, sync, flags);
}
HRESULT STDMETHODCALLTYPE h_Present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp) {
    if (!(flags & DXGI_PRESENT_TEST)) OnPresent(sc);
    return o_Present1(sc, sync, flags, pp);
}
HRESULT STDMETHODCALLTYPE h_SetFullscreenState(IDXGISwapChain* sc, BOOL fs, IDXGIOutput* out) {
    if (fs && g_cfg.nofullscreen) {
        // a game that retries every frame would flood the log (and the 200-line ring): log the first and every 256th
        long n = ++C.fs_refused;
        if (n == 1 || n % 256 == 0) Log("refused DXGI exclusive fullscreen (%ld so far); set the game to windowed or borderless in its own settings", n);
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    return o_SetFullscreenState(sc, fs, out);
}
// ResizeBuffers: drop our D3D12 readback objects (they never hold a back buffer, so the resize is never blocked).
using PFN_RB = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using PFN_RB1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
PFN_RB o_ResizeBuffers = nullptr; PFN_RB1 o_ResizeBuffers1 = nullptr;
std::atomic<long> g_resizes{0};
HRESULT STDMETHODCALLTYPE h_ResizeBuffers(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    g_resizes++; Drop12("ResizeBuffers"); g_apiSc = nullptr;
    return o_ResizeBuffers(sc, n, w, h, f, fl);
}
HRESULT STDMETHODCALLTYPE h_ResizeBuffers1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* mask, IUnknown* const* q) {
    g_resizes++; Drop12("ResizeBuffers1"); g_apiSc = nullptr;
    return o_ResizeBuffers1(sc, n, w, h, f, fl, mask, q);
}

// DDraw wrappers (DDrawCompat, dgVoodoo, ...) render into their OWN top-level "presentation" window and leave the game's
// window empty, so PrintWindow(game) is black (or a stale frame) while the screen shows the game. Find that window (same
// process, visible, largest) so capture can use it; it wins unless it prints one flat colour and the game window does not.
struct OverlaySearch { DWORD pid; HWND best; long long area; };
BOOL CALLBACK OverlayEnumProc(HWND h, LPARAM lp) {
    OverlaySearch* o = (OverlaySearch*)lp;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != o->pid || !IsWindowVisible(h) || h == g_hwnd) return TRUE;
    wchar_t cls[96] = {}; GetClassNameW(h, cls, 96);
    if (wcsstr(cls, L"PresentationWindow") == nullptr) return TRUE;
    RECT rc; if (!GetClientRect(h, &rc)) return TRUE;
    long long a = (long long)rc.right * rc.bottom;
    if (a > o->area) { o->area = a; o->best = h; }
    return TRUE;
}
// a window that printed nothing is one flat colour (black for a never-painted DDraw window, white for an unpainted one)
bool PixelsUniform(const std::vector<BYTE>& px) {
    if (px.size() < 8) return true;
    for (size_t i = 4; i + 3 < px.size(); i += 4) if (std::memcmp(&px[i], &px[0], 3) != 0) return false;
    return true;
}
std::atomic<bool> g_capViaOverlay{false};
std::atomic<HWND> g_overlayHint{nullptr};   // presentation window handle sent by the CLI with a capture request
bool CaptureViaPrintWindow(std::string& err) {
    g_capPix.clear(); g_capW = g_capH = 0; g_capViaOverlay = false;
    if (!g_hwnd) { err = "no window"; return false; }
    RECT rc; GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom; if (w <= 0 || h <= 0) { err = "empty client rect"; return false; }
    HDC wdc = GetDC(g_hwnd); HDC mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    BOOL ok = PrintWindow(g_hwnd, mdc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    if (ok) { g_capPix.assign((BYTE*)bits, (BYTE*)bits + (size_t)w * h * 4); for (size_t i = 3; i < g_capPix.size(); i += 4) g_capPix[i] = 255; g_capW = w; g_capH = h; }
    else err = "PrintWindow failed";
    if (ok) {   // a wrapper's presentation window, when there is one, is what the screen shows: the game window may be blank OR a stale frame
        OverlaySearch os{ GetCurrentProcessId(), nullptr, 0 };
        // Walk the z-order with GetWindow instead of EnumWindows: DDraw wrappers hook EnumWindows (and may hide their own windows) in the game process
        for (HWND w2 = GetTopWindow(nullptr); w2; w2 = GetWindow(w2, GW_HWNDNEXT)) OverlayEnumProc(w2, (LPARAM)&os);
        if (!os.best) {   // DDrawCompat hides its presentation windows from user32 enumeration INSIDE the game process: the CLI looked it up from outside
            HWND hint = g_overlayHint.load(); DWORD hp = 0; wchar_t hc[96] = {};
            if (hint && IsWindow(hint) && GetWindowThreadProcessId(hint, &hp) && hp == os.pid && GetClassNameW(hint, hc, 96) && wcsstr(hc, L"PresentationWindow")) os.best = hint;
        }
        RECT oc{}; if (os.best) GetClientRect(os.best, &oc);
        if (os.best && oc.right > 0 && oc.bottom > 0) {
            BITMAPINFO bo = bi; bo.bmiHeader.biWidth = oc.right; bo.bmiHeader.biHeight = -oc.bottom;
            void* obits = nullptr; HBITMAP obmp = CreateDIBSection(wdc, &bo, DIB_RGB_COLORS, &obits, nullptr, 0);
            HDC odc = CreateCompatibleDC(wdc); HGDIOBJ oold = SelectObject(odc, obmp);
            BOOL pwo = PrintWindow(os.best, odc, PW_CLIENTONLY);
            if (pwo) {   // a GDI window answers WM_PRINTCLIENT; a layered/D3D presentation window only prints through DWM
                GdiFlush(); std::vector<BYTE> chk((BYTE*)obits, (BYTE*)obits + (size_t)oc.right * oc.bottom * 4);
                if (PixelsUniform(chk)) { pwo = PrintWindow(os.best, odc, PW_CLIENTONLY | PW_RENDERFULLCONTENT); }
            }
            if (pwo) {
                // the wrapper scales the game surface to fit the presentation window, keeping the aspect ratio, centred
                double sc = (std::min)((double)oc.right / w, (double)oc.bottom / h);
                int cw = (int)(w * sc + 0.5), ch = (int)(h * sc + 0.5), cx = (oc.right - cw) / 2, cy = (oc.bottom - ch) / 2;
                SetStretchBltMode(mdc, HALFTONE); SetBrushOrgEx(mdc, 0, 0, nullptr);
                if (StretchBlt(mdc, 0, 0, w, h, odc, cx, cy, cw, ch, SRCCOPY)) {
                    GdiFlush();   // the DIB section is read directly below: drain GDI batched drawing first
                    std::vector<BYTE> shot((BYTE*)bits, (BYTE*)bits + (size_t)w * h * 4);
                    for (size_t i = 3; i < shot.size(); i += 4) shot[i] = 255;
                    // keep the game window's own print only when the presentation window printed a flat colour and it did not
                    if (!PixelsUniform(shot) || PixelsUniform(g_capPix)) { g_capPix = std::move(shot); g_capViaOverlay = true; }
                }
            }
            SelectObject(odc, oold); DeleteObject(obmp); DeleteDC(odc);
        }
    }
    SelectObject(mdc, old); DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(g_hwnd, wdc);
    return ok != FALSE;
}
bool SavePng(const std::wstring& path, const std::vector<BYTE>& px, UINT w, UINT h, std::string& err) {
    IWICImagingFactory* f = nullptr; IWICStream* s = nullptr; IWICBitmapEncoder* e = nullptr; IWICBitmapFrameEncode* fr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    if (SUCCEEDED(hr)) hr = f->CreateStream(&s);
    if (SUCCEEDED(hr)) hr = s->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e);
    if (SUCCEEDED(hr)) hr = e->Initialize(s, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = e->CreateNewFrame(&fr, nullptr);
    if (SUCCEEDED(hr)) hr = fr->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = fr->SetSize(w, h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = fr->SetPixelFormat(&pf);
    if (SUCCEEDED(hr)) hr = fr->WritePixels(h, w * 4, (UINT)px.size(), (BYTE*)px.data());
    if (SUCCEEDED(hr)) hr = fr->Commit();
    if (SUCCEEDED(hr)) hr = e->Commit();
    if (fr) fr->Release(); if (e) e->Release(); if (s) s->Release(); if (f) f->Release();
    if (FAILED(hr)) { char b[64]; sprintf_s(b, "WIC png encode failed hr=0x%08X", (unsigned)hr); err = b; return false; }
    return true;
}

// ----------------------------------------------------------------------------------------------
// hook installation
// ----------------------------------------------------------------------------------------------
// HookAddr runs on the init/monitor threads AND inside the loader's DLL-load notification (early injection),
// so the two report strings are guarded; StatusJson reads them under the same lock.
std::mutex g_hookStrMx;
bool HookAddr(const char* name, void* target, void* det, void** orig) {
    if (!target) { std::lock_guard<std::mutex> lk(g_hookStrMx); g_hooksFail += std::string(name) + "(missing) "; return false; }
    MH_STATUS s = VIMH_CreateHook(target, det, orig);
    if (s == MH_ERROR_ALREADY_CREATED) {  // export alias of a function already hooked (GetPhysicalCursorPos == GetCursorPos on Win10)
        std::lock_guard<std::mutex> lk(g_hookStrMx);
        g_hooksOk += std::string(name) + "(alias) ";
        return true;
    }
    if (s == MH_OK) s = VIMH_EnableHook(target);
    if (s != MH_OK) { { std::lock_guard<std::mutex> lk(g_hookStrMx); g_hooksFail += std::string(name) + "(" + VIMH_StatusToString(s) + ") "; } Log("hook %s failed: %s", name, VIMH_StatusToString(s)); return false; }
    std::lock_guard<std::mutex> lk(g_hookStrMx);
    g_hooksOk += std::string(name) + " ";
    return true;
}
template <class T> bool HookApi(const wchar_t* mod, const char* name, T det, T* orig) {
    HMODULE h = GetModuleHandleW(mod); if (!h) h = LoadLibraryW(mod);
    void* p = h ? (void*)GetProcAddress(h, name) : nullptr;
    return HookAddr(name, p, (void*)det, (void**)orig);
}
void InstallUser32Hooks() {
    const wchar_t* u = L"user32.dll";
    HookApi(u, "GetForegroundWindow", &h_GetForegroundWindow, &o_GetForegroundWindow);
    HookApi(u, "GetActiveWindow", &h_GetActiveWindow, &o_GetActiveWindow);
    HookApi(u, "GetFocus", &h_GetFocus, &o_GetFocus);
    HookApi(u, "SetForegroundWindow", &h_SetForegroundWindow, &o_SetForegroundWindow);
    HookApi(u, "BringWindowToTop", &h_BringWindowToTop, &o_BringWindowToTop);
    HookApi(u, "SetWindowPos", &h_SetWindowPos, &o_SetWindowPos);
    HookApi(u, "ShowWindow", &h_ShowWindow, &o_ShowWindow);
    HookApi(u, "SetFocus", &h_SetFocus, &o_SetFocus);
    HookApi(u, "SetActiveWindow", &h_SetActiveWindow, &o_SetActiveWindow);
    HookApi(u, "GetAsyncKeyState", &h_GetAsyncKeyState, &o_GetAsyncKeyState);
    HookApi(u, "GetKeyState", &h_GetKeyState, &o_GetKeyState);
    HookApi(u, "GetKeyboardState", &h_GetKeyboardState, &o_GetKeyboardState);
    HookApi(u, "GetCursorPos", &h_GetCursorPos, &o_GetCursorPos);
    HookApi(u, "GetPhysicalCursorPos", &h_GetPhysicalCursorPos, &o_GetPhysicalCursorPos);
    HookApi(u, "SetCursorPos", &h_SetCursorPos, &o_SetCursorPos);
    HookApi(u, "SetPhysicalCursorPos", &h_SetPhysicalCursorPos, &o_SetPhysicalCursorPos);
    HookApi(u, "ClipCursor", &h_ClipCursor, &o_ClipCursor);
    HookApi(u, "WindowFromPoint", &h_WindowFromPoint, &o_WindowFromPoint);
    HookApi(u, "GetRawInputData", &h_GetRawInputData, &o_GetRawInputData);
    HookApi(u, "GetRawInputBuffer", &h_GetRawInputBuffer, &o_GetRawInputBuffer);
    HookApi(u, "RegisterRawInputDevices", &h_RegisterRawInputDevices, &o_RegisterRawInputDevices);
}

LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
void InstallDxgiHooks() {
    WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = DummyProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"vinput_dummy_dxgi";
    RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, L"vinput", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferCount = 1; d.BufferDesc.Width = 64; d.BufferDesc.Height = 64; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.OutputWindow = hw; d.SampleDesc.Count = 1; d.Windowed = TRUE; d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx);
    if (SUCCEEDED(hr) && sc) {
        void** vt = *(void***)sc;
        HookAddr("IDXGISwapChain::Present", vt[8], (void*)&h_Present, (void**)&o_Present);
        HookAddr("IDXGISwapChain::SetFullscreenState", vt[10], (void*)&h_SetFullscreenState, (void**)&o_SetFullscreenState);
        HookAddr("IDXGISwapChain::ResizeBuffers", vt[13], (void*)&h_ResizeBuffers, (void**)&o_ResizeBuffers);
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)) && sc1) {
            void** vt1 = *(void***)sc1;
            HookAddr("IDXGISwapChain1::Present1", vt1[22], (void*)&h_Present1, (void**)&o_Present1);
            sc1->Release();
        }
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) && sc3) {
            void** vt3 = *(void***)sc3;
            HookAddr("IDXGISwapChain3::ResizeBuffers1", vt3[39], (void*)&h_ResizeBuffers1, (void**)&o_ResizeBuffers1);
            sc3->Release();
        }
    } else {
        g_hooksFail += "DXGI(dummy device failed) ";
        Log("dummy D3D11 device failed hr=0x%08X", (unsigned)hr);
    }
    if (ctx) ctx->Release(); if (dev) dev->Release(); if (sc) sc->Release();
    DestroyWindow(hw); UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
void TryHookDInput() {
    if (g_diHooked) return;
    HMODULE h = GetModuleHandleW(L"dinput8.dll"); if (!h) return;
    g_diHooked = true;
    using PFN_DI8C = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    PFN_DI8C create = (PFN_DI8C)GetProcAddress(h, "DirectInput8Create");
    if (!create) { g_hooksFail += "dinput8(no DirectInput8Create) "; return; }
    IDirectInput8W* di = nullptr;
    if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr)) || !di) { g_hooksFail += "dinput8(create failed) "; return; }
    IDirectInputDevice8W* dev = nullptr;
    if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &dev, nullptr)) && dev) {
        void** vt = *(void***)dev;
        HookAddr("IDirectInputDevice8::Acquire", vt[7], (void*)&h_DIAcquire, (void**)&o_DIAcquire);
        HookAddr("IDirectInputDevice8::GetDeviceState", vt[9], (void*)&h_DIGetDeviceState, (void**)&o_DIGetDeviceState);
        HookAddr("IDirectInputDevice8::GetDeviceData", vt[10], (void*)&h_DIGetDeviceData_entry, (void**)&o_DIGetDeviceData);
        dev->Release();
    }
    di->Release();
    Log("dinput8 hooks installed");
}
// Legacy dinput.dll (DirectInputCreateA, DirectInput 1-7: Fallout, many 1990s games). Its devices have their own vtable,
// so dinput8 hooks never see them. Keyboard and mouse may share implementation functions: each distinct one is hooked once.
bool g_diLegacyHooked = false;
void TryHookDInputLegacy() {
    if (g_diLegacyHooked) return;
    HMODULE h = GetModuleHandleW(L"dinput.dll"); if (!h) return;
    g_diLegacyHooked = true;
    using PFN_DICA = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
    PFN_DICA create = (PFN_DICA)GetProcAddress(h, "DirectInputCreateA");
    if (!create) { g_hooksFail += "dinput(no DirectInputCreateA) "; return; }
    LPDIRECTINPUTA di = nullptr;
    if (FAILED(create(GetModuleHandleW(nullptr), 0x0300, &di, nullptr)) || !di) { g_hooksFail += "dinput(create failed) "; return; }
    void* seen[3 * kDiLegacySets] = {}; int nSeen = 0, set = 0;
    static const GUID* kGuids[2] = {&GUID_SysKeyboard, &GUID_SysMouse};
    for (const GUID* g : kGuids) {
        LPDIRECTINPUTDEVICEA dev = nullptr;
        if (FAILED(di->CreateDevice(*g, &dev, nullptr)) || !dev) continue;
        void** vt = *(void***)dev;
        bool dup = false; for (int i = 0; i < nSeen; i++) if (seen[i] == vt[9]) dup = true;
        if (!dup && set < kDiLegacySets) {
            seen[nSeen++] = vt[9];
            bool ok = true;
            switch (set) {
                case 0: ok = HookAddr("IDirectInputDeviceA::Acquire", vt[7], (void*)&h_DILAcquire<0>, (void**)&o_DILAcquire[0])
                          && HookAddr("IDirectInputDeviceA::GetDeviceState", vt[9], (void*)&h_DILGetDeviceState<0>, (void**)&o_DILGetDeviceState[0])
                          && HookAddr("IDirectInputDeviceA::GetDeviceData", vt[10], (void*)&h_DILGetDeviceData<0>, (void**)&o_DILGetDeviceData[0]); break;
                case 1: ok = HookAddr("IDirectInputDeviceA#2::Acquire", vt[7], (void*)&h_DILAcquire<1>, (void**)&o_DILAcquire[1])
                          && HookAddr("IDirectInputDeviceA#2::GetDeviceState", vt[9], (void*)&h_DILGetDeviceState<1>, (void**)&o_DILGetDeviceState[1])
                          && HookAddr("IDirectInputDeviceA#2::GetDeviceData", vt[10], (void*)&h_DILGetDeviceData<1>, (void**)&o_DILGetDeviceData[1]); break;
            }
            (void)ok; set++;
        }
        dev->Release();
    }
    di->Release();
    Log("dinput (legacy) hooks installed: %d vtable set(s)", set);
}
void TryHookXInput() {
    for (int i = 0; i < 4; i++) {
        if (g_xHooked[i]) continue;
        HMODULE h = GetModuleHandleW(kXinputDlls[i]); if (!h) continue;
        g_xHooked[i] = true;
        char nm[64]; sprintf_s(nm, "XInputGetState[%d]", i);
        HookAddr(nm, (void*)GetProcAddress(h, "XInputGetState"), kXgsDetours[i], (void**)&o_XGS[i]);
        void* ex = (void*)GetProcAddress(h, MAKEINTRESOURCEA(100));   // XInputGetStateEx (xinput1_3 / 1_4 only)
        if (ex) { sprintf_s(nm, "XInputGetStateEx[%d]", i); HookAddr(nm, ex, kXgsDetours[i + 4], (void**)&o_XGS[i + 4]); }
    }
}

// ----------------------------------------------------------------------------------------------
// main window discovery / subclass
// ----------------------------------------------------------------------------------------------
struct FindCtx { HWND best; long long area; };
BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    FindCtx* c = (FindCtx*)lp; DWORD p = 0; GetWindowThreadProcessId(h, &p);
    if (p != g_pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE); if (ex & WS_EX_TOOLWINDOW) return TRUE;
    wchar_t cls[128] = {0}; GetClassNameW(h, cls, 127);
    if (!wcscmp(cls, L"vinput_dummy_dxgi")) return TRUE;
    RECT r; GetClientRect(h, &r); long long a = (long long)r.right * r.bottom;
    if (!wcscmp(cls, L"UnrealWindow") || !wcscmp(cls, L"UnityWndClass")) a += 1LL << 40;
    if (a > c->area) { c->area = a; c->best = h; }
    return TRUE;
}
// UWP: the game's Windows.UI.Core.CoreWindow is a CHILD of ApplicationFrameHost's ApplicationFrameWindow (another
// process), so EnumWindows never sees it and Process.MainWindowHandle is 0.
BOOL CALLBACK EnumCoreChild(HWND h, LPARAM lp) {
    FindCtx* c = (FindCtx*)lp; DWORD p = 0; GetWindowThreadProcessId(h, &p);
    if (p != g_pid || !IsWindowVisible(h)) return TRUE;
    wchar_t cls[128] = {0}; GetClassNameW(h, cls, 127);
    if (wcscmp(cls, L"Windows.UI.Core.CoreWindow")) return TRUE;
    RECT r; GetClientRect(h, &r); long long a = (long long)r.right * r.bottom;
    if (a > c->area) { c->area = a; c->best = h; }
    return TRUE;
}
BOOL CALLBACK EnumFrames(HWND h, LPARAM lp) {
    wchar_t cls[128] = {0}; GetClassNameW(h, cls, 127);
    if (!wcscmp(cls, L"ApplicationFrameWindow")) EnumChildWindows(h, EnumCoreChild, lp);
    else { DWORD p = 0; GetWindowThreadProcessId(h, &p); if (p == g_pid && !wcscmp(cls, L"Windows.UI.Core.CoreWindow")) EnumCoreChild(h, lp); }
    return TRUE;
}
HWND FindMainWindow() {
    FindCtx c{nullptr, 0}; EnumWindows(EnumProc, (LPARAM)&c);
    if (c.area >= 200 * 150) return c.best;
    FindCtx u{nullptr, 0}; EnumWindows(EnumFrames, (LPARAM)&u);
    return u.area >= 200 * 150 ? u.best : nullptr;
}
void Retarget(HWND h) {
    if (h == g_hwnd || !h) return;
    if (g_hwnd && IsWindow(g_hwnd) && g_origWndProc && (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) == h_WndProc)
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    g_hwndUnicode = IsWindowUnicode(h) != FALSE;
    WNDPROC prev = g_hwndUnicode ? (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)h_WndProc) : (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
    g_origWndProc = prev;
    g_appActive = -1; g_lastSynthAct = 0; g_synthLeft = 3;   // a new window has not been told anything yet
    RECT rc; GetClientRect(h, &rc);
    if (!g_vcurTouched) { g_vcurX = rc.right / 2; g_vcurY = rc.bottom / 2; }
    g_hwnd = h;
    wchar_t cls[128] = {0}, title[256] = {0}; GetClassNameW(h, cls, 127); GetWindowTextW(h, title, 255);
    Log("target window %p class=%s title=%s client=%dx%d unicode=%d", h, W2U(cls).c_str(), W2U(title).c_str(), rc.right, rc.bottom, (int)g_hwndUnicode);
}

// ----------------------------------------------------------------------------------------------
// tiny flat JSON
// ----------------------------------------------------------------------------------------------
struct JV { int t = 0; std::string s; double n = 0; bool b = false; };  // t: 1 str 2 num 3 bool 4 null
using JObj = std::map<std::string, JV>;
void SkipWs(const std::string& s, size_t& i) { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
bool ParseStr(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false; i++;
    while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') { unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16); i += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                std::wstring w; if (cp >= 0x10000) { cp -= 0x10000; w += (wchar_t)(0xD800 + (cp >> 10)); w += (wchar_t)(0xDC00 + (cp & 1023)); } else w += (wchar_t)cp;
                out += W2U(w); break;
            }
            default: out += e;
            }
        } else out += c;
    }
    if (i >= s.size()) return false; i++; return true;
}
bool ParseJson(const std::string& s, JObj& o) {
    size_t i = 0; SkipWs(s, i); if (i >= s.size() || s[i] != '{') return false; i++;
    for (;;) {
        SkipWs(s, i); if (i < s.size() && s[i] == '}') return true;
        std::string k; if (!ParseStr(s, i, k)) return false;
        SkipWs(s, i); if (i >= s.size() || s[i] != ':') return false; i++; SkipWs(s, i);
        JV v;
        if (i < s.size() && s[i] == '"') { v.t = 1; if (!ParseStr(s, i, v.s)) return false; }
        else if (!s.compare(i, 4, "true")) { v.t = 3; v.b = true; i += 4; }
        else if (!s.compare(i, 5, "false")) { v.t = 3; v.b = false; i += 5; }
        else if (!s.compare(i, 4, "null")) { v.t = 4; i += 4; }
        else { char* e = nullptr; v.n = strtod(s.c_str() + i, &e); if (e == s.c_str() + i) return false; v.t = 2; i = e - s.c_str(); }
        o[k] = v;
        SkipWs(s, i); if (i < s.size() && s[i] == ',') { i++; continue; } if (i < s.size() && s[i] == '}') return true; return false;
    }
}
std::string JEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; } else o += c; }
    return o;
}
struct JOut {
    std::string s = "{"; bool first = true;
    void key(const char* k) { if (!first) s += ','; first = false; s += '"'; s += k; s += "\":"; }
    void str(const char* k, const std::string& v) { key(k); s += '"' + JEsc(v) + '"'; }
    void num(const char* k, double v) { key(k); char b[64]; if (v == (long long)v) sprintf_s(b, "%lld", (long long)v); else sprintf_s(b, "%.4f", v); s += b; }
    void bol(const char* k, bool v) { key(k); s += v ? "true" : "false"; }
    void raw(const char* k, const std::string& v) { key(k); s += v; }
    std::string done() { return s + "}"; }
};
double JNum(const JObj& o, const char* k, double d) { auto it = o.find(k); if (it == o.end()) return d; if (it->second.t == 2) return it->second.n; if (it->second.t == 1) return atof(it->second.s.c_str()); if (it->second.t == 3) return it->second.b; return d; }
std::string JStr(const JObj& o, const char* k, const char* d = "") { auto it = o.find(k); if (it == o.end()) return d; if (it->second.t == 1) return it->second.s; if (it->second.t == 2) { char b[64]; sprintf_s(b, "%g", it->second.n); return b; } if (it->second.t == 3) return it->second.b ? "true" : "false"; return d; }
bool JHas(const JObj& o, const char* k) { return o.find(k) != o.end(); }
bool JBool(const JObj& o, const char* k, bool d) { auto it = o.find(k); if (it == o.end()) return d; if (it->second.t == 3) return it->second.b; if (it->second.t == 2) return it->second.n != 0; if (it->second.t == 1) return it->second.s == "true" || it->second.s == "1" || it->second.s == "on"; return d; }

// ----------------------------------------------------------------------------------------------
// key names
// ----------------------------------------------------------------------------------------------
int VkFromName(const std::string& n) { return vi_keys::VkFromName(n); }
std::vector<int> ParseCombo(const std::string& s) { return vi_keys::ParseCombo(s); }
// unknown names used to be ignored silently (the command then returned ok with nothing pressed); they are now reported
WORD PadButtonsFromNames(const std::string& s, std::string* bad = nullptr) {
    WORD b = 0; size_t st = 0;
    while (st < s.size()) {
        size_t p = s.find_first_of("+, ", st); std::string n = s.substr(st, p == std::string::npos ? std::string::npos : p - st);
        for (auto& c : n) c = (char)tolower((unsigned char)c);
        if (n == "a") b |= 0x1000; else if (n == "b") b |= 0x2000; else if (n == "x") b |= 0x4000; else if (n == "y") b |= 0x8000;
        else if (n == "up") b |= 1; else if (n == "down") b |= 2; else if (n == "left") b |= 4; else if (n == "right") b |= 8;
        else if (n == "start") b |= 0x10; else if (n == "back") b |= 0x20; else if (n == "ls") b |= 0x40; else if (n == "rs") b |= 0x80;
        else if (n == "lb") b |= 0x100; else if (n == "rb") b |= 0x200;
        else if (n == "guide") b |= 0x400;   // only visible through XInputGetStateEx
        else if (n.empty() || n == "none") {}
        else if (bad && bad->empty()) *bad = n;
        if (p == std::string::npos) break; st = p + 1;
    }
    return b;
}

// ----------------------------------------------------------------------------------------------
// commands
// ----------------------------------------------------------------------------------------------
// Anti-cheat refusal (vi_acguard.h): set when a known anti-cheat module is (or becomes) loaded in this process.
// From then on every command except ping/status/log is refused and held input is released.
std::atomic<const char*> g_acProduct{nullptr};
wchar_t g_acModule[128] = {0};
std::atomic<bool> g_acLogged{false};
void NoteAntiCheat(const char* product, const wchar_t* name, size_t len) {
    if (g_acProduct.load()) return;
    size_t n = len < 127 ? len : 127; wmemcpy(g_acModule, name, n); g_acModule[n] = 0;
    g_acProduct = product;
}
// Scans the modules already loaded in this process (never call under the loader lock).
bool ScanOwnModulesForAntiCheat() {
    HMODULE mods[1024]; DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return false;
    DWORD count = need / sizeof(HMODULE); if (count > 1024) count = 1024;
    for (DWORD i = 0; i < count; i++) {
        wchar_t name[MAX_PATH]; DWORD len = K32GetModuleBaseNameW(GetCurrentProcess(), mods[i], name, MAX_PATH);
        if (!len) continue;
        if (const char* p = vi_ac::MatchModule(name, len)) { NoteAntiCheat(p, name, len); return true; }
    }
    return false;
}
void ReleaseAll() {
    for (int vk = 1; vk < 256; vk++) if (g_vk[vk]) KeyEvent(vk, false);
    g_padButtons = 0; g_padSetMask = 0; g_padLT = g_padRT = g_padLX = g_padLY = g_padRX = g_padRY = 0; g_padPacket++;
}
std::string ErrJ(const std::string& e) { JOut o; o.bol("ok", false); o.str("error", e); return o.done(); }
std::string OkJ() { JOut o; o.bol("ok", true); return o.done(); }
std::string AntiCheatRefusal() {
    char name[256] = {0}; WideCharToMultiByte(CP_UTF8, 0, g_acModule, -1, name, sizeof(name) - 1, nullptr, nullptr);
    JOut o; o.bol("ok", false); o.bol("anticheat_refused", true);
    o.str("error", std::string("refused: anti-cheat module loaded in this process (") + g_acProduct.load() + ", " + name +
          "). vinput is for single-player / offline games only; all input and capture commands are disabled. No override.");
    JOut a; a.str("product", g_acProduct.load()); a.str("kind", "module"); a.str("name", name); o.raw("anticheat", a.done());
    return o.done();
}

std::string KeyTraceJson(int vk) {
    if (vk <= 0 || vk > 255) return ErrJ("trace needs a valid key");
    const KeyTrace& t = g_keyTrace[vk];
    JOut o; o.bol("ok", true); o.num("vk", vk); o.str("name", vi_keys::Name(vk));
    o.bol("virtual_down", g_vk[vk] != 0);
    o.num("injected_down", (double)t.injected_down.load()); o.num("injected_up", (double)t.injected_up.load());
    o.num("message_down", (double)t.delivered_down.load()); o.num("message_up", (double)t.delivered_up.load());
    o.num("last_injected_down_ms", (double)t.last_injected_down_ms.load());
    o.num("last_injected_up_ms", (double)t.last_injected_up_ms.load());
    JOut channels;
    auto add = [&](const char* name, const ChannelTrace& c) {
        JOut x; x.num("reads", (double)c.reads.load()); x.num("down_reads", (double)c.down_reads.load()); x.num("last", c.last.load());
        channels.raw(name, x.done());
    };
    add("GetAsyncKeyState", t.gaks); add("GetKeyState", t.gks); add("GetKeyboardState", t.gkbs);
    add("DirectInputState", t.di_state); add("DirectInputBuffer", t.di_buffer);
    add("RawInputData", t.raw_data); add("RawInputBuffer", t.raw_buffer);
    o.raw("channels", channels.done());
    o.str("meaning", "API reads/returned values and messages dispatched through vinput; application action is not implied");
    return o.done();
}

// ----------------------------------------------------------------------------------------------
// ownership: one agent claims a target; commands carrying another (or no) owner are refused until the claim is
// released or expires. The TTL slides: every accepted command from the owner extends it.
// ----------------------------------------------------------------------------------------------
std::mutex g_claimMx;
std::string g_claimOwner;
ULONGLONG g_claimExpiry = 0, g_claimSince = 0;
DWORD g_claimTtlMs = 0;
std::atomic<long> g_claimRefused{0};
bool ClaimActiveLocked(ULONGLONG now) { return !g_claimOwner.empty() && now < g_claimExpiry; }
void ClaimFields(JOut& o) {
    std::lock_guard<std::mutex> lk(g_claimMx);
    ULONGLONG now = GetTickCount64(); bool active = ClaimActiveLocked(now);
    JOut c; c.bol("active", active); c.str("owner", active ? g_claimOwner : std::string());
    c.num("expires_in_ms", active ? (double)(g_claimExpiry - now) : 0); c.num("ttl_ms", active ? g_claimTtlMs : 0);
    c.num("held_ms", active ? (double)(now - g_claimSince) : 0); c.num("refused", (double)g_claimRefused.load());
    o.raw("claim", c.done());
}
// Commands that neither drive input nor change state: always allowed, and they bypass the exec mutex so an
// agent can read status while a long hover / --after-frames / hold is running on the same target.
bool IsOpenCommand(const std::string& cmd) {
    return cmd == "ping" || cmd == "status" || cmd == "log" || cmd == "trace" || cmd == "claim" || cmd == "unclaim" || cmd == "restore-foreground";
}
std::string ClaimRefusal(const char* why) {
    // caller holds g_claimMx
    ULONGLONG now = GetTickCount64();
    JOut o; o.bol("ok", false);
    o.str("error", std::string(why) + " - target is claimed by '" + g_claimOwner + "' (pass that --owner, wait for expiry, or --force-claim)");
    o.str("claim_owner", g_claimOwner); o.num("claim_expires_in_ms", (double)(g_claimExpiry - now)); o.bol("claim_refused", true);
    return o.done();
}
// Returns an error reply when the command must be refused, empty when it may run.
std::string ClaimGate(const std::string& cmd, const JObj& j) {
    std::string owner = JStr(j, "owner"); bool force = JBool(j, "force_claim", false);
    std::lock_guard<std::mutex> lk(g_claimMx);
    ULONGLONG now = GetTickCount64(); bool active = ClaimActiveLocked(now);
    if (cmd == "claim") {
        if (owner.empty()) return ErrJ("claim needs an owner (vinput --owner NAME ... or VINPUT_OWNER)");
        double ttl = JNum(j, "ttl", 300);
        if (!(ttl >= 1 && ttl <= 86400)) return ErrJ("claim ttl must be 1..86400 s");
        if (active && g_claimOwner != owner && !force) { g_claimRefused++; return ClaimRefusal("claim refused"); }
        if (!active || g_claimOwner != owner) { Log("CLAIM %s by '%s' (ttl %g s)%s", active ? "TAKEN OVER" : "set", owner.c_str(), ttl, active ? (" from '" + g_claimOwner + "'").c_str() : ""); g_claimSince = now; }
        g_claimOwner = owner; g_claimTtlMs = (DWORD)(ttl * 1000); g_claimExpiry = now + g_claimTtlMs;
        return std::string();
    }
    if (cmd == "unclaim") {
        if (!active) { g_claimOwner.clear(); return std::string(); }
        if (g_claimOwner != owner && !force) { g_claimRefused++; return ClaimRefusal("unclaim refused"); }
        Log("CLAIM released by '%s'", owner.c_str()); g_claimOwner.clear(); g_claimExpiry = 0;
        return std::string();
    }
    if (IsOpenCommand(cmd) || !active) return std::string();
    if (g_claimOwner == owner) { g_claimExpiry = now + g_claimTtlMs; return std::string(); }
    if (force) { Log("CLAIM bypassed by '%s' (--force-claim) for '%s'", owner.c_str(), cmd.c_str()); return std::string(); }
    g_claimRefused++;
    return ClaimRefusal(("'" + cmd + "' refused").c_str());
}

std::string StatusJson() {
    JOut o; o.bol("ok", true); o.str("version", VI_VERSION); o.num("pid", g_pid);
#ifdef _WIN64
    o.num("bits", 64);
#else
    o.num("bits", 32);
#endif
    o.bol("app_container", g_appContainer);
    o.bol("anticheat_disabled", g_acProduct.load() != nullptr);
    char hb[32]; sprintf_s(hb, "0x%llX", (unsigned long long)(ULONG_PTR)g_hwnd); o.str("hwnd", hb);
    if (g_hwnd) {
        wchar_t cls[128] = {0}, title[256] = {0}; GetClassNameW(g_hwnd, cls, 127); GetWindowTextW(g_hwnd, title, 255);
        RECT rc; GetClientRect(g_hwnd, &rc);
        o.str("class", W2U(cls)); o.str("title", W2U(title)); o.num("client_w", rc.right); o.num("client_h", rc.bottom);
        o.bol("subclassed", (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) == h_WndProc);
    }
    bool fg = ComputeRealFg(); o.bol("really_foreground", fg);
    o.bol("fakefocus", g_cfg.fakefocus); o.bol("autoactivate", g_cfg.autoactivate); o.num("app_active", g_appActive); o.bol("isolate", g_cfg.isolate); o.bol("blockfocus", g_cfg.blockfocus); o.bol("nofullscreen", g_cfg.nofullscreen); o.bol("keepvisible", g_cfg.keepvisible); if (g_hwnd) o.bol("minimized", IsIconic(g_hwnd) != FALSE);
    o.num("cursor_mode", g_cfg.cursor); o.num("rawmode", g_cfg.rawmode); o.num("msgmode", g_cfg.msgmode);
    o.num("vcur_x", g_vcurX); o.num("vcur_y", g_vcurY);
    RawReg rr = QueryRawReg();
    o.bol("raw_kbd", rr.kbd); o.bol("raw_kbd_nolegacy", rr.kbdNoLegacy); o.bol("raw_mouse", rr.mouse); o.bol("raw_mouse_nolegacy", rr.mouseNoLegacy);
    o.num("swapchain_w", g_scW); o.num("swapchain_h", g_scH); o.num("swapchain_fmt", g_scFmt); o.num("swapchain_api", g_scApi);
    {   // native capture: which API the counted frames come from, and what served the last capture
        o.str("frame_api", ApiName(g_frameApi)); o.num("frames", (double)C.present_ours.load());
        o.str("capture_path", C.captures ? ApiName(g_capApi) : "none");
        JOut fa; fa.num("d3d11", (double)g_frame11.load()); fa.num("d3d12", (double)g_frame12.load());
        fa.num("gl", (double)g_frameGl.load()); fa.num("vk", (double)g_frameVk.load()); fa.num("foreign_dxgi", (double)g_frameForeign.load());
        o.raw("frames_by_api", fa.done());
        JOut d12; d12.num("queue_source", g_c12QueueSource); d12.bol("ecl_hook", o_ExecuteCommandLists != nullptr);
        d12.num("submits", (double)g_c12.submits); d12.num("releases", (double)g_c12.releases); d12.num("leaks", (double)g_c12.leaks); d12.num("resizes", (double)g_resizes.load());
        o.raw("d3d12", d12.done());
        o.bol("gl_hooked", o_wglSwapBuffers != nullptr);
        {
            std::lock_guard<std::mutex> lk(g_vkMx);
            JOut v; v.bol("hooked", o_vkQueuePresentKHR != nullptr); v.bol("enabled", g_vkEnabled.load());
            v.num("devices", (double)g_vkDevs.size()); v.num("queues", (double)g_vkQueues.size()); v.num("swapchains", (double)g_vkScs.size());
            v.num("usage_added", g_vkUsageAdded.load()); v.num("presents", g_vkPresents.load()); v.num("presents_other_window", g_vkPresentsOther.load());
            v.num("submits", g_vkSubmits.load()); v.num("releases", g_vkReleases.load());
            o.raw("vk", v.done());
        }
    }
    std::string vd = "["; bool f1 = true; for (int i = 1; i < 256; i++) if (g_vk[i]) { if (!f1) vd += ','; f1 = false; vd += std::to_string(i); } vd += "]";
    o.raw("virtual_down", vd);
    JOut c;
#define CN(x) c.num(#x, (double)C.x.load())
    CN(gaks); CN(gks); CN(gkbs); CN(gcur); CN(scur); CN(clip); CN(gfg); CN(setfg); CN(setfg_blocked);
    CN(rid); CN(rid_magic); CN(rib); CN(rib_injected); CN(reg_raw); CN(di_kbd); CN(di_mouse); CN(di_data_kbd); CN(di_data_mouse); CN(di_acq); CN(di_fake); CN(xinput);
    CN(wm_key); CN(wm_char); CN(wm_mouse); CN(wm_input); CN(wm_input_dropped); CN(wm_mouse_dropped); CN(act_swallowed); CN(act_synth); CN(min_blocked); CN(min_restored); CN(present); CN(present_ours); CN(captures);
    CN(posted); CN(raw_posted); CN(raw_queued); CN(fs_refused);
#undef CN
    o.raw("counters", c.done());
    { std::lock_guard<std::mutex> lk(g_hookStrMx); o.str("hooks_ok", g_hooksOk); o.str("hooks_failed", g_hooksFail); }
    o.num("uptime_ms", GetTickCount() - g_t0);
    if (g_logPath[0]) o.str("log_path", W2U(g_logPath));
    ClaimFields(o);
    return o.done();
}

std::string DoCapture(const JObj& j) {
    std::string path = JStr(j, "path");
    bool hashOnly = JBool(j, "hash_only", false);
    if (path.empty() && !hashOnly) return ErrJ("capture needs path");
    // frame-hash (wait-change) must not redefine --capture-space: clicks map to the last SAVED capture.
    struct CaptureSpaceGuard {
        bool active; UINT w, h; LONG ox, oy, cw, ch;
        explicit CaptureSpaceGuard(bool a) : active(a), w(g_capW), h(g_capH), ox(g_captureOriginX), oy(g_captureOriginY), cw(g_captureClientW), ch(g_captureClientH) {}
        ~CaptureSpaceGuard() { if (active) { g_capW = w; g_capH = h; g_captureOriginX = ox; g_captureOriginY = oy; g_captureClientW = cw; g_captureClientH = ch; } }
    } captureSpaceGuard(hashOnly);
    std::string method = JStr(j, "method", "auto");
    if (method == "native") method = "swapchain";
    if (method != "auto" && method != "swapchain" && method != "printwindow") return ErrJ("unknown capture method");
    g_overlayHint = (HWND)(UINT_PTR)(unsigned long long)JNum(j, "overlay", 0);
    int timeout = (int)JNum(j, "timeout_ms", 3000);
    double requestedFrames = JNum(j, "after_frames", 0);
    if (timeout < 1 || timeout > 120000 || requestedFrames < 0 || requestedFrames > 3600 || requestedFrames != std::floor(requestedFrames)) return ErrJ("capture timeout/after-frames out of range");
    long frameBefore = C.present_ours;
    DWORD frameStart = GetTickCount();
    while (C.present_ours - frameBefore < requestedFrames && GetTickCount() - frameStart < (DWORD)timeout) Sleep(2);
    if (C.present_ours - frameBefore < requestedFrames) return ErrJ("not enough target Present calls before capture timeout");
    std::string used, err, path_ = "printwindow";
    bool got = false;
    DWORD capStart = GetTickCount();
    // A game window that has never presented through a hooked swapchain (DDraw/GDI games, or DXGI loaded only by a wrapper) will
    // not start now: do not burn the whole timeout before falling back to PrintWindow.
    const int nativeWait = (C.present_ours == 0 && timeout > 300) ? 300 : timeout;
    auto native = [&]() -> bool {   // one request to whichever native frame hook fires next for our window
        ResetEvent(g_capEvent);
        g_capReq = 1;
        bool fired = WaitForSingleObject(g_capEvent, nativeWait) == WAIT_OBJECT_0;
        if (!fired) {
            LONG exp = 1;
            if (g_capReq.compare_exchange_strong(exp, 0)) { err = "no frame (Present / SwapBuffers / vkQueuePresentKHR) for the game window within timeout"; return false; }
            // a hook is mid-capture: wait for it so we never race the pixel buffer
            fired = WaitForSingleObject(g_capEvent, 2000) == WAIT_OBJECT_0;
            if (!fired) { err = "native capture did not finish"; return false; }
        }
        int pend = g_capPending.exchange(0);
        DWORD spent = GetTickCount() - capStart, left = spent < (DWORD)timeout ? (DWORD)timeout - spent : 1;
        if (pend == API_D3D12) { std::string e; if (!Finish12(left < 500 ? 500 : left, e)) { err = e; return false; } }
        else if (pend == API_VK) { std::string e; if (!FinishVk(left < 500 ? 500 : left, e)) { err = e; return false; } }
        if (g_capW && !g_capPix.empty()) return true;
        err = g_capErr.empty() ? "native capture produced nothing" : g_capErr;
        return false;
    };
    if (method == "auto" || method == "swapchain") {
        if (o_Present || o_wglSwapBuffers || o_vkQueuePresentKHR) {
            got = native();
            // the D3D12 queue fallback hook is installed asynchronously: give it one retry
            // transient: the D3D12 queue hook is installed by the monitor thread (<= 1 s) and the queue is only known
            // after the game's next submission; retry inside the caller's timeout
            while (!got && g_capRetryable && GetTickCount() - capStart < (DWORD)timeout) { Sleep(50); err.clear(); got = native(); }
            if (got) { used = "swapchain"; path_ = ApiName(g_capApi); }
            else if (g_capApi != API_NONE && err.compare(0, strlen(ApiName(g_capApi)), ApiName(g_capApi)) != 0) err = std::string(ApiName(g_capApi)) + ": " + err;
        } else err = "no native frame hook installed";
    }
    if (!got && (method == "auto" || method == "printwindow")) {
        std::string e2;
        if (CaptureViaPrintWindow(e2)) { got = true; used = "printwindow"; path_ = g_capViaOverlay ? "printwindow_overlay" : "printwindow"; } else err += (err.empty() ? "" : "; ") + e2;
    }
    if (!got) return ErrJ(err);
    RECT rc{}; if (g_hwnd) GetClientRect(g_hwnd, &rc);
    g_captureOriginX = g_captureOriginY = 0; g_captureClientW = rc.right; g_captureClientH = rc.bottom;
    if (JHas(j, "region")) {
        int x=0, y=0, w=0, h=0, consumed=0;
        std::string region = JStr(j, "region");
        if (sscanf_s(region.c_str(), "%d,%d,%d,%d%n", &x, &y, &w, &h, &consumed) != 4 || consumed != (int)region.size() ||
            x < 0 || y < 0 || w <= 0 || h <= 0 || (long long)x+w > rc.right || (long long)y+h > rc.bottom || !rc.right || !rc.bottom)
            return ErrJ("region must be x,y,w,h in client pixels inside the window");
        int left = (int)((long long)x*g_capW/rc.right), top = (int)((long long)y*g_capH/rc.bottom);
        int right = (int)((long long)(x+w)*g_capW/rc.right), bottom = (int)((long long)(y+h)*g_capH/rc.bottom);
        vi_vision::Image source{(int)g_capW, (int)g_capH, std::move(g_capPix)}, crop;
        std::string e;
        if (!vi_vision::Crop(source, {left,top,right-left,bottom-top}, crop, e)) return ErrJ(e);
        g_capPix = std::move(crop.pixels); g_capW = (UINT)crop.w; g_capH = (UINT)crop.h;
        g_captureOriginX=x; g_captureOriginY=y; g_captureClientW=w; g_captureClientH=h;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned shift=0; shift<32; shift+=8) { hash ^= (g_capW >> shift) & 255; hash *= 1099511628211ULL; hash ^= (g_capH >> shift) & 255; hash *= 1099511628211ULL; }
    for (BYTE byte : g_capPix) { hash ^= byte; hash *= 1099511628211ULL; }
    char hashText[24]; sprintf_s(hashText, "%016llX", (unsigned long long)hash);
    std::wstring wp = U2W(path);
    std::string e3;
    if (!hashOnly && !SavePng(wp, g_capPix, g_capW, g_capH, e3)) return ErrJ(e3);
    C.captures++;
    JOut o; o.bol("ok", true); o.str("path", path); o.str("method", used); o.str("capture_path", path_); o.num("w", g_capW); o.num("h", g_capH);
    o.num("frame", used == "printwindow" ? (double)C.present_ours.load() : (double)g_capturedPresent.load());
    o.str("hash", hashText); o.num("present_before", frameBefore); o.num("captured_present", g_capturedPresent);
    o.num("after_frames", requestedFrames); o.num("origin_x", g_captureOriginX); o.num("origin_y", g_captureOriginY);
    o.num("region_client_w", g_captureClientW); o.num("region_client_h", g_captureClientH);
    if (!err.empty()) o.str("note", err);
    o.num("client_w", rc.right); o.num("client_h", rc.bottom);
    return o.done();
}

void SleepMs(double ms) { if (ms > 0) Sleep((DWORD)ms); }

// coordinates: client pixels; "norm":true => fractions of the client size; "space":"capture" => pixels of the last capture image
void ResolveXY(const JObj& j, LONG& x, LONG& y) {
    double fx = JNum(j, "x", g_vcurX), fy = JNum(j, "y", g_vcurY);
    RECT rc{}; if (g_hwnd) GetClientRect(g_hwnd, &rc);
    if (JBool(j, "norm", false)) { fx *= rc.right; fy *= rc.bottom; }
    else if (JStr(j, "space") == "capture" && g_capW && g_capH) { fx = g_captureOriginX + fx * g_captureClientW / g_capW; fy = g_captureOriginY + fy * g_captureClientH / g_capH; }
    x = (LONG)std::lround(fx); y = (LONG)std::lround(fy);
}

std::string Exec(const std::string& line) {
    JObj j;
    if (!ParseJson(line, j)) return ErrJ("bad json");
    std::string cmd = JStr(j, "cmd");
    { std::string refused = ClaimGate(cmd, j); if (!refused.empty()) return refused; }
    if (g_acProduct.load() && cmd != "ping" && cmd != "status" && cmd != "log") {
        if (!g_acLogged.exchange(true)) { ReleaseAll(); Log("anti-cheat module detected (%s): input and capture disabled", g_acProduct.load()); }
        return AntiCheatRefusal();
    }
    if (cmd == "claim" || cmd == "unclaim") { JOut o; o.bol("ok", true); ClaimFields(o); return o.done(); }
    if (cmd == "ping") return OkJ();
    if (cmd == "status") return StatusJson();
    if (cmd == "trace") return KeyTraceJson(VkFromName(JStr(j, "key")));
    if (cmd == "log") {
        std::lock_guard<std::mutex> lk(g_logMx);
        std::string a = "["; bool f = true; for (auto& l : g_logRing) { if (!f) a += ','; f = false; a += '"' + JEsc(l) + '"'; } a += "]";
        JOut o; o.bol("ok", true); o.raw("log", a); return o.done();
    }
    if (cmd == "config") {
        if (JHas(j, "fakefocus")) g_cfg.fakefocus = JBool(j, "fakefocus", true);
        if (JHas(j, "isolate")) g_cfg.isolate = JBool(j, "isolate", true);
        if (JHas(j, "autoactivate")) g_cfg.autoactivate = JBool(j, "autoactivate", true);
        if (JHas(j, "blockfocus")) g_cfg.blockfocus = JBool(j, "blockfocus", true);
        if (JHas(j, "nofullscreen")) g_cfg.nofullscreen = JBool(j, "nofullscreen", true);
        if (JHas(j, "keepvisible")) g_cfg.keepvisible = JBool(j, "keepvisible", true);
        if (JHas(j, "vk")) g_vkEnabled = JBool(j, "vk", true);   // vk=off: Vulkan hooks pass through (no usage change, no capture)
        if (JHas(j, "cursor")) { std::string c = JStr(j, "cursor"); g_cfg.cursor = c == "virtual" ? 1 : c == "real" ? 2 : 0; }
        if (JHas(j, "raw")) { std::string c = JStr(j, "raw"); g_cfg.rawmode = c == "post" ? 1 : c == "buffer" ? 2 : c == "off" ? 3 : 0; }
        if (JHas(j, "msgs")) { std::string c = JStr(j, "msgs"); g_cfg.msgmode = c == "on" ? 1 : c == "off" ? 2 : 0; }
        if (JHas(j, "hwnd")) { HWND h = (HWND)(ULONG_PTR)strtoull(JStr(j, "hwnd").c_str(), nullptr, 0); if (IsWindow(h)) { g_hwndForced = true; Retarget(h); } else return ErrJ("not a window"); }
        return StatusJson();
    }
    if (cmd == "activate") {
        if (!g_hwnd) return ErrJ("no window");
        // Posting activation messages can run an application's activation handler and
        // indirectly take the real foreground. Refresh the API shims without invoking it.
        g_cfg.fakefocus = true;
        JOut o; o.bol("ok", true); o.str("activation", "virtual");
        o.bol("really_foreground", ComputeRealFg()); o.bol("posts_activation", false);
        return o.done();
    }
    if (cmd == "restore-foreground") {
        // Only relinquishes a real foreground already owned by this process.
        // It cannot grant foreground to a background game or generate system input.
        HWND previous = (HWND)(ULONG_PTR)strtoull(JStr(j, "hwnd").c_str(), nullptr, 0);
        if (!ComputeRealFg()) return OkJ();
        if (!IsWindow(previous) || IsOurWindow(previous)) return ErrJ("restore needs a previous window outside the target");
        BOOL restored = o_SetForegroundWindow(previous);
        JOut o; o.bol("ok", restored != FALSE); o.bol("foreground_restored", !ComputeRealFg()); return o.done();
    }
    if (cmd == "release") { ReleaseAll(); return OkJ(); }
    if (cmd == "restore") { if (!g_hwnd) return ErrJ("no window"); ShowWindowAsync(g_hwnd, SW_SHOWNOACTIVATE); Sleep(300); return StatusJson(); }
    if (cmd == "key" || cmd == "hold" || cmd == "down" || cmd == "up") {
        std::vector<int> vks = ParseCombo(JStr(j, "key"));
        if (vks.empty()) return ErrJ("unknown key: " + JStr(j, "key"));
        int count = (int)JNum(j, "count", 1);
        double ms = JNum(j, "ms", cmd == "hold" ? 1000 : 80), gap = JNum(j, "gap", 80);
        for (int r = 0; r < count; r++) {
            if (cmd != "up") for (int vk : vks) { KeyEvent(vk, true); if (vks.size() > 1) Sleep(15); }
            if (cmd == "down") break;
            if (cmd != "up") SleepMs(ms);
            for (size_t i = vks.size(); i-- > 0;) { KeyEvent(vks[i], false); if (vks.size() > 1) Sleep(15); }
            if (cmd == "up") break;
            if (r + 1 < count) SleepMs(gap);
        }
        return OkJ();
    }
    if (cmd == "type") {
        std::wstring t = U2W(JStr(j, "text"));
        double delay = JNum(j, "delay", 30);
        bool keys = JBool(j, "keys", false);
        if (!g_hwnd) return ErrJ("no window");
        for (wchar_t ch : t) {
            if (keys) {
                SHORT vs = VkKeyScanW(ch);
                if (vs != -1) {
                    int vk = vs & 0xFF; bool sh = (vs & 0x100) != 0;
                    if (sh) KeyEvent(VK_SHIFT, true);
                    KeyEvent(vk, true); Sleep(20); KeyEvent(vk, false);
                    if (sh) KeyEvent(VK_SHIFT, false);
                }
            } else {
                if (ch == L'\n') { KeyEvent(VK_RETURN, true); Sleep(20); KeyEvent(VK_RETURN, false); }
                else PostMessageW(g_hwnd, WM_CHAR, (WPARAM)ch, 1);
            }
            SleepMs(delay);
        }
        return OkJ();
    }
    if (cmd == "mouse" || cmd == "move") {
        if (!g_hwnd) return ErrJ("no window");
        int steps = (int)JNum(j, "steps", 1);
        if (steps < 1 || steps > 10000) return ErrJ("steps must be 1..10000");
        double stepms = JNum(j, "step_ms", 10);
        if (!(stepms >= 0 && stepms <= 1000) || stepms*(steps-1) > 120000) return ErrJ("mouse timing exceeds bounds");
        bool unbounded = JBool(j, "raw", false);
        if (unbounded && cmd != "move") return ErrJ("--raw is a relative move mode");
        if (unbounded) g_cfg.cursor=1;
        long long appliedX=0, appliedY=0;
        LONG rawX=0, rawY=0;
        if (JHas(j, "dx") || JHas(j, "dy")) {
            double dxValue=JNum(j,"dx",0), dyValue=JNum(j,"dy",0);
            if (!(dxValue>=-1000000 && dxValue<=1000000 && dyValue>=-1000000 && dyValue<=1000000)) return ErrJ("relative delta must be within +/-1000000");
            LONG dx=(LONG)dxValue, dy=(LONG)dyValue; LONG sx=0, sy=0; rawX=dx; rawY=dy;
            for (int s = 1; s <= steps; s++) {
                LONG tx=(LONG)((long long)dx*s/steps), ty=(LONG)((long long)dy*s/steps);
                AppliedMouse a=MouseMoveTo(0,0,true,tx-sx,ty-sy,unbounded); appliedX+=a.dx; appliedY+=a.dy;
                sx=tx; sy=ty; if(s<steps) SleepMs(stepms);
            }
        } else {
            LONG x, y; ResolveXY(j, x, y); LONG ox = g_vcurX, oy = g_vcurY;
            for (int s=1; s<=steps; s++) {
                AppliedMouse a=MouseMoveTo((LONG)(ox+((long long)x-ox)*s/steps),(LONG)(oy+((long long)y-oy)*s/steps),false,0,0);
                appliedX+=a.dx; appliedY+=a.dy; if(s<steps) SleepMs(stepms);
            }
        }
        JOut o; o.bol("ok", true); o.num("x", g_vcurX); o.num("y", g_vcurY);
        o.num("applied_dx", (double)appliedX); o.num("applied_dy", (double)appliedY);
        o.num("raw_dx", rawX); o.num("raw_dy", rawY); o.bol("unbounded", unbounded);
        o.str("cursor", UseVirtualCursor() ? "virtual" : "real"); return o.done();
    }
    if (cmd == "click" || cmd == "mousedown" || cmd == "mouseup") {
        if (!g_hwnd) return ErrJ("no window");
        std::string b = JStr(j, "button", "left");
        if (b != "left" && b != "right" && b != "middle" && b != "x1" && b != "x2") return ErrJ("unknown mouse button: " + b);
        int vk = b == "right" ? VK_RBUTTON : b == "middle" ? VK_MBUTTON : b == "x1" ? VK_XBUTTON1 : b == "x2" ? VK_XBUTTON2 : VK_LBUTTON;
        int previousCursor = g_cfg.cursor;
        // Keep virtual mode after the command: applications may poll GetCursorPos on a later frame.
        // A scoped override would report success then race the game's input consumer.
        g_cfg.cursor = 1;
        long hoverFrames = 0;
        if (JHas(j, "x") || JHas(j, "y")) {
            LONG x, y; ResolveXY(j, x, y);
            long delivered = g_mouseMovesDelivered;
            MouseMoveTo(x, y, false, 0, 0);
            DWORD start = GetTickCount(), timeout = (DWORD)JNum(j, "hover_timeout_ms", 1500);
            RawReg rr = QueryRawReg();
            if (MsgsWanted(false, rr)) {
                while (g_mouseMovesDelivered == delivered && GetTickCount() - start < timeout) Sleep(2);
                if (g_mouseMovesDelivered == delivered) return ErrJ("virtual hover not dispatched before timeout");
            }
            long frame = C.present_ours, requested = (long)JNum(j, "hover_frames", 3);
            if (frame > 0 && requested > 0) {
                while (C.present_ours - frame < requested && GetTickCount() - start < timeout) Sleep(2);
                hoverFrames = C.present_ours - frame;
                if (hoverFrames < requested) return ErrJ("virtual hover not rendered before timeout");
            }
            SleepMs(JNum(j, "settle_ms", frame > 0 ? 0 : 100));
        }
        int count = (int)JNum(j, "count", 1); double ms = JNum(j, "ms", 60);
        if (cmd == "mousedown") MouseButton(vk, true);
        else if (cmd == "mouseup") MouseButton(vk, false);
        else for (int r = 0; r < count; r++) { MouseButton(vk, true); SleepMs(ms); MouseButton(vk, false); if (r + 1 < count) SleepMs(JNum(j, "gap", 80)); }
        JOut o; o.bol("ok", true); o.num("x", g_vcurX); o.num("y", g_vcurY);
        o.str("cursor", "virtual"); o.num("previous_cursor_mode", previousCursor); o.num("hover_frames", hoverFrames);
        o.str("delivery", "injected; application response is not implied"); return o.done();
    }
    if (cmd == "wheel") { MouseWheel((int)JNum(j, "delta", 120), JBool(j, "horizontal", false)); return OkJ(); }
    if (cmd == "pad") {
        std::string names = JStr(j, "buttons");
        std::string lower = names; for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
        if (lower == "off") {   // stop overriding: pad 0 is the real pad again (or "not connected" when there is none)
            g_padActive = false; g_padButtons = 0; g_padSetMask = 0; g_padPacket++;
            JOut o; o.bol("ok", true); o.bol("pad_active", false); return o.done();
        }
        std::string bad;
        WORD b = PadButtonsFromNames(names, &bad);
        if (!bad.empty()) return ErrJ("pad: unknown button '" + bad + "' (a b x y up down left right start back ls rs lb rb, none, off)");
        const double kUnset = -1e18;
        const char* axes[6] = {"lt", "rt", "lx", "ly", "rx", "ry"};
        int v[6] = {}, mask = 0; bool clamped = false;
        for (int a = 0; a < 6; a++) {
            double d = JNum(j, axes[a], kUnset);
            if (d == kUnset) continue;
            double lo = a < 2 ? 0 : -32768, hi = a < 2 ? 255 : 32767;
            if (d < lo) { d = lo; clamped = true; } if (d > hi) { d = hi; clamped = true; }
            v[a] = (int)d; mask |= 1 << a;
        }
        g_padLT = v[0]; g_padRT = v[1]; g_padLX = v[2]; g_padLY = v[3]; g_padRX = v[4]; g_padRY = v[5];
        g_padButtons = b; g_padSetMask = mask; g_padActive = true; g_padPacket++;
        double ms = JNum(j, "ms", 100);
        bool hold = JBool(j, "hold", false);
        if (!hold) { SleepMs(ms); g_padButtons = 0; g_padSetMask = 0; g_padLX = g_padLY = g_padRX = g_padRY = g_padLT = g_padRT = 0; g_padPacket++; }
        JOut o; o.bol("ok", true); o.num("buttons", b); o.num("axes_set_mask", mask); o.bol("clamped", clamped);
        o.bol("hold", hold); o.num("xinput_calls", (double)C.xinput.load());
        o.str("delivery", "XInputGetState/Ex user 0 overridden; application response is not implied"); return o.done();
    }
    if (cmd == "capture") return DoCapture(j);
    if (cmd == "frame-hash") { j["hash_only"] = JV{3, {}, 0, true}; return DoCapture(j); }
    if (cmd == "wait") { SleepMs(JNum(j, "ms", 100)); return OkJ(); }
    return ErrJ("unknown cmd: " + cmd);
}

// ----------------------------------------------------------------------------------------------
// pipe server
// ----------------------------------------------------------------------------------------------
DWORD WINAPI ClientThread(LPVOID p) {
    HANDLE pipe = (HANDLE)p;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static std::mutex execMx;  // commands from several clients run one at a time
    std::string buf; char tmp[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(pipe, tmp, sizeof(tmp), &n, nullptr) || n == 0) break;
        buf.append(tmp, n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            std::string resp;
            // Read-only / ownership commands skip the exec mutex. The mutex lives in this target process, so it only
            // serialises clients of THIS game; other targets (other processes) are never blocked by it.
            JObj peek; bool open = ParseJson(line, peek) && IsOpenCommand(JStr(peek, "cmd"));
            if (open) { try { resp = Exec(line); } catch (...) { resp = ErrJ("exception"); } }
            else { std::lock_guard<std::mutex> lk(execMx); try { resp = Exec(line); } catch (...) { resp = ErrJ("exception"); } }
            resp += "\n";
            DWORD w; if (!WriteFile(pipe, resp.data(), (DWORD)resp.size(), &w, nullptr)) goto out;
        }
    }
out:
    FlushFileBuffers(pipe); DisconnectNamedPipe(pipe); CloseHandle(pipe);
    CoUninitialize();
    return 0;
}
DWORD WINAPI PipeThread(LPVOID) {
    // An AppContainer (UWP / Xbox app game) may only create pipes in its own namespace: \\.\pipe\LOCAL\<name> lands in
    // \Sessions\<n>\AppContainerNamedObjects\<container SID>\<name>; vinput.exe resolves that path from our token.
    // The DACL must also grant the AppContainer groups (AC = ALL APPLICATION PACKAGES, S-1-15-2-2 = restricted),
    // or our own next CreateNamedPipe instance is denied (dual access check).
    wchar_t name[64]; swprintf_s(name, g_appContainer ? L"\\\\.\\pipe\\LOCAL\\vinput_%lu" : L"\\\\.\\pipe\\vinput_%lu", g_pid);
    // Access: only the game's own user account (and SYSTEM); never other users, never the network
    // (PIPE_REJECT_REMOTE_CLIENTS). The AppContainer groups and the low-integrity label are added only when the game
    // itself runs in an AppContainer, where they are required (see above).
    std::wstring userSid;
    {
        HANDLE tok = nullptr; BYTE buf[256]; DWORD n = 0; LPWSTR s = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n) && ConvertSidToStringSidW(((TOKEN_USER*)buf)->User.Sid, &s)) { userSid = s; LocalFree(s); }
            CloseHandle(tok);
        }
        if (userSid.empty()) Log("pipe: cannot read the user SID (%lu); the pipe will only admit SYSTEM", GetLastError());
    }
    std::wstring sddl = L"D:(A;;GA;;;SY)";
    if (!userSid.empty()) sddl += L"(A;;GA;;;" + userSid + L")";
    if (g_appContainer) sddl += L"(A;;GA;;;AC)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    SECURITY_ATTRIBUTES sa = {sizeof(sa)}; PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) Log("pipe: bad security descriptor %lu", GetLastError());
    sa.lpSecurityDescriptor = sd;
    Log("pipe %s", W2U(name).c_str());
    for (;;) {
        HANDLE h = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, sd ? &sa : nullptr);
        if (h == INVALID_HANDLE_VALUE) { Log("CreateNamedPipe failed %lu", GetLastError()); Sleep(1000); continue; }
        BOOL c = ConnectNamedPipe(h, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!c) { CloseHandle(h); continue; }
        HANDLE t = CreateThread(nullptr, 0, ClientThread, h, 0, nullptr);
        if (t) CloseHandle(t); else CloseHandle(h);
    }
}

// A game started in the background never receives WM_ACTIVATEAPP(TRUE). Games that keep their own "application is
// active" flag (Fallout 1/2's GNW95 loop, many DirectDraw-era titles) then ignore all input, and fakefocus only swallows
// DEactivations. Post the activation sequence a real switch would send, once, to the game's own window: posted messages
// cannot change the real foreground, and blockfocus (required) keeps the game's activation handler from taking it.
// Re-posted (at most every 2 s, 3 times) while the game's window procedure still has not seen an activation.
void MaybeSynthActivate() {
    HWND h = g_hwnd;
    if (!h || !g_origWndProc || !g_cfg.autoactivate || !g_cfg.fakefocus || !g_cfg.blockfocus || RealFg()) return;
    if (g_appActive.load() == 1 || g_synthLeft.load() <= 0 || IsIconic(h)) return;
    DWORD now = GetTickCount();
    if (g_lastSynthAct && now - g_lastSynthAct < 2000) return;
    g_lastSynthAct = now ? now : 1; g_synthLeft--;
    DWORD otherTid = GetWindowThreadProcessId(RealForeground(), nullptr);   // WM_ACTIVATEAPP lParam: the thread losing activation
    PostMessageW(h, WM_NCACTIVATE, TRUE, 0);
    PostMessageW(h, WM_ACTIVATEAPP, TRUE, (LPARAM)otherTid);
    PostMessageW(h, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessageW(h, WM_SETFOCUS, 0, 0);
    C.act_synth++;
    Log("game window not told it is active (app_active=%d) -> posted WM_NCACTIVATE/WM_ACTIVATEAPP/WM_ACTIVATE/WM_SETFOCUS (no foreground change)", g_appActive.load());
}

DWORD WINAPI MonitorThread(LPVOID) {
    bool lastFg = ComputeRealFg();
    DWORD lastSlow = 0;
    HWND lastFgWnd = RealForeground();
    for (;;) {
        HWND fw = RealForeground();
        bool fg = ComputeRealFg();
        g_realFg = fg;
        if (fg != lastFg) {
            DWORD pp = 0; GetWindowThreadProcessId(lastFgWnd, &pp);
            wchar_t t[128] = {0}; GetWindowTextW(lastFgWnd, t, 127);
            Log("REAL foreground %s (previous fg: pid %lu '%s')", fg ? "GAINED" : "lost", pp, W2U(t).c_str());
        }
        if (fw) lastFgWnd = fw;
        static DWORD lastRestore = 0;
        static RECT goodRect = {0, 0, 0, 0};
        if (g_hwnd) {
            RECT wr; GetWindowRect(g_hwnd, &wr);
            bool parked = wr.left <= -30000 || wr.top <= -30000;   // minimised placement (also seen NON-iconic after a shell "minimise all")
            bool iconic = IsIconic(g_hwnd) != FALSE;
            if (!iconic && !parked && wr.right - wr.left >= 200 && wr.bottom - wr.top >= 150) goodRect = wr;
            if (g_cfg.keepvisible && !fg && (iconic || parked) && GetTickCount() - lastRestore > 1500) {
                lastRestore = GetTickCount(); C.min_restored++;
                Log("game window minimised/parked (iconic=%d at %ld,%ld) while in background -> restoring without activation", (int)iconic, wr.left, wr.top);
                if (iconic) ShowWindowAsync(g_hwnd, SW_SHOWNOACTIVATE);
                if (goodRect.right > goodRect.left)
                    o_SetWindowPos(g_hwnd, nullptr, goodRect.left, goodRect.top, goodRect.right - goodRect.left, goodRect.bottom - goodRect.top,
                                   SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
            }
        }
        if (lastFg && !fg && g_hwnd) {
            // the game just lost the real foreground: make sure it does not keep the real cursor trapped
            RECT clip, wr; GetClipCursor(&clip); GetWindowRect(g_hwnd, &wr);
            RECT inter; if (IntersectRect(&inter, &clip, &wr) && EqualRect(&inter, &clip)) o_ClipCursor(nullptr);
            for (int vk = 1; vk < 256; vk++) g_vkPressed[vk] = 0;
        }
        lastFg = fg;
        MaybeSynthActivate();
        DWORD now = GetTickCount();
        if (now - lastSlow > 1000) {
            lastSlow = now;
            TryHookDInput(); TryHookDInputLegacy(); TryHookXInput(); TryHookEcl(); TryHookGL(); TryHookVk();
            if (!g_hwndForced) {
                HWND best = FindMainWindow();
                if (!g_hwnd || !IsWindow(g_hwnd)) { if (best) Retarget(best); }
                else if (best && best != g_hwnd && now - g_t0 < 180000) Retarget(best);
            }
        }
        Sleep(15);
    }
}

// Loader DLL-load notification: hook vulkan-1 / opengl32 the moment they are mapped. With early injection
// (`launch --early`: process created suspended, vinput in before the game's first instruction) this closes the
// window between the game's LoadLibrary and its first vkCreateInstance/vkCreateDevice, which the 1 s monitor poll
// cannot. Runs under the loader lock on the loading thread: only GetProcAddress + MinHook, no LoadLibrary.
struct ViUnicodeString { USHORT Length, MaximumLength; PWSTR Buffer; };
struct ViLdrDllData { ULONG Flags; const ViUnicodeString* FullDllName; const ViUnicodeString* BaseDllName; PVOID DllBase; ULONG SizeOfImage; };
typedef VOID (CALLBACK* ViLdrNotify)(ULONG reason, const ViLdrDllData* data, PVOID ctx);
typedef LONG (NTAPI* PFN_LdrRegisterDllNotification)(ULONG flags, ViLdrNotify cb, PVOID ctx, PVOID* cookie);
std::atomic<long> g_ldrNotified{0};
bool BaseNameIs(const ViUnicodeString* n, const wchar_t* want) {
    if (!n || !n->Buffer) return false;
    size_t len = n->Length / sizeof(wchar_t), wl = wcslen(want);
    return len == wl && !_wcsnicmp(n->Buffer, want, wl);
}
VOID CALLBACK OnDllNotification(ULONG reason, const ViLdrDllData* data, PVOID) {
    if (reason != 1 || !data) return;   // 1 = LDR_DLL_NOTIFICATION_REASON_LOADED
    if (data->BaseDllName && data->BaseDllName->Buffer) {
        size_t len = data->BaseDllName->Length / sizeof(wchar_t);
        if (const char* p = vi_ac::MatchModule(data->BaseDllName->Buffer, len)) { NoteAntiCheat(p, data->BaseDllName->Buffer, len); return; }
    }
    if (BaseNameIs(data->BaseDllName, L"vulkan-1.dll")) { g_ldrNotified++; TryHookVk((HMODULE)data->DllBase); }
    else if (BaseNameIs(data->BaseDllName, L"opengl32.dll")) { g_ldrNotified++; TryHookGL((HMODULE)data->DllBase); }
}
void RegisterDllNotification() {
    auto reg = (PFN_LdrRegisterDllNotification)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification");
    PVOID cookie = nullptr;
    LONG st = reg ? reg(0, OnDllNotification, nullptr, &cookie) : -1;
    Log("dll-load notification %s (0x%08lX)", st == 0 ? "registered" : "FAILED", (unsigned long)st);
}

DWORD WINAPI InitThread(LPVOID) {
    if (ScanOwnModulesForAntiCheat()) {
        char name[256] = {0}; WideCharToMultiByte(CP_UTF8, 0, g_acModule, -1, name, sizeof(name) - 1, nullptr, nullptr);
        Log("REFUSED: anti-cheat module loaded (%s, %s). vinput is for offline games only: no hooks, no pipe.", g_acProduct.load(), name);
        return 0;
    }
    g_capEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MH_STATUS s = VIMH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log("MinHook init failed %s", VIMH_StatusToString(s)); }
    RegisterDllNotification();
    InstallUser32Hooks();
    // GL / Vulkan before DXGI: their drivers may present through an internal DXGI swapchain, which is only recognised
    // as foreign once the GL/VK hook is in. Vulkan before the pipe exists: a target that waits for the pipe and then
    // creates its VkDevice is covered.
    TryHookGL();
    TryHookVk();
    InstallDxgiHooks();
    TryHookDInput(); TryHookDInputLegacy(); TryHookXInput();
    FindRawDevices();
    g_realFg = ComputeRealFg();
    HANDLE t = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
    for (int i = 0; i < 600 && !g_hwnd; i++) { HWND h = FindMainWindow(); if (h) { Retarget(h); break; } Sleep(200); }
    Log("hooks ok: %s", g_hooksOk.c_str());
    if (!g_hooksFail.empty()) Log("hooks FAILED: %s", g_hooksFail.c_str());
    t = CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
    return 0;
}

}  // namespace

extern "C" const char* VInput_Version(void) { return VI_VERSION; }

extern "C" int VInput_Init(void) {
    static volatile LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) != 0) return 0;
    g_pid = GetCurrentProcessId();
    wchar_t mx[64]; swprintf_s(mx, L"Local\\vinput_instance_%lu", g_pid);
    HANDLE m = CreateMutexW(nullptr, FALSE, mx);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(m); return 0; }  // another copy (DLL vs compiled-in) owns this process
    g_t0 = GetTickCount();
    { HANDLE tok = nullptr; DWORD isAc = 0, n = 0; if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) { if (GetTokenInformation(tok, TokenIsAppContainer, &isAc, sizeof(isAc), &n)) g_appContainer = isAc != 0; CloseHandle(tok); } }
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&VInput_Init, &g_self);
    wchar_t dir[MAX_PATH] = {0};
    if (GetModuleFileNameW(g_self, dir, MAX_PATH)) {
        wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
        wchar_t logs[MAX_PATH]; swprintf_s(logs, L"%s\\logs", dir); CreateDirectoryW(logs, nullptr);
        swprintf_s(g_logPath, L"%s\\vinput_%lu.log", logs, g_pid);
    }
    Log("%s starting in pid %lu", VI_VERSION, g_pid);
    HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    return 1;
}
