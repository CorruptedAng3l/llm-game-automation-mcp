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
#include <ddraw.h>
#include <d3d9.h>
#include <wincodec.h>
#include <atomic>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdarg>
#include "vi_mh_rename.h"
#include "MinHook.h"
#include "vinput.h"
#include "vi_keys.h"
#include "vi_trace.h"
#include "vi_vision.h"
#include "vi_image_encode.h"
#include "vi_paths.h"
#include "vi_user_intent.h"
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
    std::atomic<bool> autoactivate{true}; // bounded guarded application-active refresh
    std::atomic<bool> isolate{true};      // while NOT really foreground: hide real kb/mouse from the game
    std::atomic<bool> blockfocus{true};   // while NOT really foreground: game cannot steal focus
    std::atomic<bool> nofullscreen{true}; // keep native DXGI windowed; emulate fullscreen by default
    std::atomic<bool> keepvisible{true};  // while NOT really foreground: refuse/undo minimise (minimised games stop presenting)
    std::atomic<int>  cursor{0};          // 0 auto (virtual when not fg), 1 always virtual, 2 always real
    std::atomic<int>  edgeMargin{8};      // client pixels; 0 disables pointer path protection
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
std::atomic<int> g_appActive{-1},g_synthLeft{3};
std::atomic<DWORD> g_lastSynthAct{0};
std::mutex g_synthMx;
bool    g_appContainer = false;   // UWP / Xbox app games: pipe in the container namespace, CoreWindow target
std::atomic<ULONGLONG> g_fgLeaseUntil{0};
std::atomic<bool> g_fgGuardArmed{false};
std::atomic<long> g_fgGained{0}, g_fgRestored{0}, g_fgRestoreFailed{0};
std::atomic<HWND> g_fgRestoreWindow{nullptr};
vi_user_intent::Detector g_userIntent;
// Owner (human) input that arrives while the game is the REAL foreground. Real messages are the ones that did not
// come from our own posted-message ring. g_ownerSeq / g_agentSeq order that input against vinput's own injection:
// the virtual cursor stands down while the owner's input is newer than the agent's last injected input.
std::atomic<unsigned long long> g_activitySeq{0}, g_ownerSeq{0}, g_agentSeq{0};
std::atomic<ULONGLONG> g_ownerHoldAt{0};       // deliberate owner input (button / key / wheel / activation); guard stands down
std::atomic<bool> g_ownerHolding{false};
std::atomic<long> g_ownerInputCount{0}, g_ownerStanddowns{0};
std::atomic<int> g_testRealFg{-1};             // test hook: -1 off, 0/1 force the "is the game the real foreground" predicate
bool TestHooksEnabled() {
    static const bool on = [] { char v[8] = {}; return GetEnvironmentVariableA("VINPUT_TEST_HOOKS", v, sizeof(v)) && v[0] == '1'; }();
    return on;
}
void NoteAgentInput() { g_agentSeq = ++g_activitySeq; }
void NoteOwnerInput(bool deliberate) {
    g_ownerSeq = ++g_activitySeq; ++g_ownerInputCount;
    if (deliberate) { g_ownerHoldAt = GetTickCount64(); g_ownerHolding = true; }
}
std::atomic<bool> g_adoptionActivationSafe{false};
UINT g_adoptionActivationMessage=0;
std::atomic<ULONG_PTR> g_adoptionActivationSequence{0};
const ULONG_PTR kAdoptionActivationCookie=(ULONG_PTR)&g_adoptionActivationSequence ^ 0x56494143;

std::atomic<uint8_t> g_vk[256];
std::atomic<uint8_t> g_vkPressed[256];
KeyTrace g_keyTrace[256];
std::recursive_mutex g_inputMx;std::mutex g_padStateMx;thread_local bool g_jobApplying=false;
void JobsOverrideKey(int);void JobsOverridePad();void JobsOverrideCursor(const char*);void JobsCancelAll(const char*);void JobsEpochChange();void JobsFrame();
extern std::atomic<unsigned long long> g_jobClaimEpoch;extern std::atomic<ULONGLONG> g_jobClaimExpiry;
extern thread_local unsigned long long g_admittedJobEpoch;extern thread_local bool g_admittedJobClaimed;
ULONGLONG g_jobIdentity=0;std::atomic<unsigned> g_jobRetargetDepth{0};
// Interrupt time is monotonic, includes system sleep, and is independent of QPC/tick scaling.
using InterruptClock=void(WINAPI*)(PULONGLONG);
InterruptClock RealInterruptClock(){static InterruptClock clock=(InterruptClock)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"QueryInterruptTime");return clock;}
ULONGLONG HousekeepingNow(){ULONGLONG value=0;if(auto clock=RealInterruptClock()){clock(&value);return value/10000;}return GetTickCount64();}
const int kDiMouseVk[5]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
struct DiMouseObservation {
    std::atomic<unsigned long long> press{0},state_down{0},buffer_down{0},release{0},state_up{0},buffer_up{0};
    std::atomic<ULONGLONG> state_poll_at{0};
};
DiMouseObservation g_diMouseObservation[5];
std::atomic<ULONGLONG> g_diMouseBufferPollAt{0};
void ObserveDiMouseDown(int button,unsigned long long generation,bool buffered) {
    auto& observation=g_diMouseObservation[button];
    if(!generation || observation.press!=generation || !g_vk[kDiMouseVk[button]])return;
    auto& seen=buffered?observation.buffer_down:observation.state_down;
    auto prior=seen.load();while(prior<generation&&!seen.compare_exchange_weak(prior,generation)){}
}
void ObserveDiMouseUp(int button,unsigned long long generation,bool buffered) {
    auto& observation=g_diMouseObservation[button];
    if(!generation||observation.release!=generation||g_vk[kDiMouseVk[button]])return;
    auto& seen=buffered?observation.buffer_up:observation.state_up;
    auto prior=seen.load();while(prior<generation&&!seen.compare_exchange_weak(prior,generation)){}
}
std::atomic<LONG> g_vcurX{0}, g_vcurY{0};
std::atomic<bool> g_vcurTouched{false};
std::atomic<unsigned long long> g_pointerIssued{0},g_pointerSeen{0};std::atomic<int> g_pointerReader{0};
std::atomic<long> g_mouseMovesDelivered{0};
std::atomic<long long> g_diDX{0},g_diDY{0};std::atomic<LONG> g_diDZ{0};
bool AddDiDelta(std::atomic<long long>& pending,long long delta){auto prior=pending.load();for(;;){if((delta>0&&prior>(std::numeric_limits<long long>::max)()-delta)||(delta<0&&prior<(std::numeric_limits<long long>::min)()-delta))return false;if(pending.compare_exchange_weak(prior,prior+delta))return true;}}
LONG ReadDiDelta(std::atomic<long long>& pending,LONG native){auto prior=pending.load();for(;;){const auto take=(std::max)(-2147483648LL-native,(std::min)(2147483647LL-native,prior));if(pending.compare_exchange_weak(prior,prior-take))return (LONG)((long long)native+take);}}

std::atomic<bool> g_padActive{false};
std::atomic<WORD> g_padButtons{0};
std::atomic<int>  g_padLT{0}, g_padRT{0}, g_padLX{0}, g_padLY{0}, g_padRX{0}, g_padRY{0};
std::atomic<DWORD> g_padPacket{1};

struct Counters {
    std::atomic<long> gaks{0}, gks{0}, gkbs{0}, gcur{0}, scur{0}, clip{0}, gfg{0}, setfg{0}, setfg_blocked{0};
    std::atomic<long> rid{0}, rid_magic{0}, rib{0}, rib_injected{0}, reg_raw{0};
    std::atomic<long> reg_raw_ok{0}, reg_raw_failed{0}, reg_raw_removed{0}, raw_post_failed{0}, raw_target_rejected{0}, raw_queue_dropped{0}, raw_real_denied{0};
    std::atomic<long> di_kbd{0}, di_mouse{0}, di_data_kbd{0}, di_data_mouse{0}, di_acq{0}, di_fake{0};
    std::atomic<long> xinput{0};
    std::atomic<long> wm_key{0}, wm_char{0}, wm_mouse{0}, wm_input{0}, wm_input_dropped{0}, wm_mouse_dropped{0};
    std::atomic<long> min_blocked{0}, min_restored{0}, create_visible_deferred{0};
    std::atomic<long> act_swallowed{0}, act_synth{0}, act_leaked{0}, act_refresh_posted{0}, act_refresh_delivered{0}, act_refresh_cancelled{0}, act_refresh_failed{0}, act_cbt_blocked{0}, act_cbt_installed{0}, act_cbt_failed{0}, act_cbt_unhook_failed{0}, present{0}, present_ours{0}, captures{0};
    std::atomic<long> posted{0}, raw_posted{0}, raw_queued{0}, post_target_rejected{0}, last_post_target_error{0};
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
using PFN_Switch = void(WINAPI*)(HWND, BOOL);
using PFN_AllowFG = BOOL(WINAPI*)(DWORD);
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
PFN_SW o_ShowWindowAsync = nullptr;
PFN_Switch o_SwitchToThisWindow = nullptr;
PFN_AllowFG o_AllowSetForegroundWindow = nullptr;
PFN_WFP o_WindowFromPoint = nullptr;
using PFN_CreateExW=HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
using PFN_CreateExA=HWND(WINAPI*)(DWORD,LPCSTR,LPCSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
PFN_CreateExW o_CreateWindowExW=nullptr;
PFN_CreateExA o_CreateWindowExA=nullptr;

HWND RealForeground() { return o_GetForegroundWindow ? o_GetForegroundWindow() : GetForegroundWindow(); }
bool ForegroundBelongsToTarget(HWND window) {
    if (!window || !IsWindow(window)) return false;
    DWORD p = 0; GetWindowThreadProcessId(window, &p); if (p == g_pid) return true;
    // UWP: the foreground window is ApplicationFrameHost's frame that hosts our CoreWindow
    HWND target=g_hwnd;DWORD targetOwner=0;
    if(target)GetWindowThreadProcessId(target,&targetOwner);
    HWND root=targetOwner==g_pid ? GetAncestor(target,GA_ROOT) : nullptr;
    return root && root==GetAncestor(window,GA_ROOT);
}
// The single "is the game the real foreground" predicate. The test hook (VINPUT_TEST_HOOKS=1 in the target's
// environment plus config test_realfg) replaces only the answer, never the OS state.
bool TestRealFgOverride(bool actual) { int t = g_testRealFg.load(); return t >= 0 && TestHooksEnabled() ? t == 1 : actual; }
bool ComputeRealFg() { return TestRealFgOverride(ForegroundBelongsToTarget(RealForeground())); }
inline bool RealFg() { return g_realFg.load(std::memory_order_relaxed); }
// The owner is using the game: it is the real foreground and real (non-injected) input arrived after the agent's last
// injected input. Until the agent's next command the virtual cursor must not override GetCursorPos / raw input.
inline bool OwnerCursorActive() { return RealFg() && g_ownerSeq.load() > g_agentSeq.load(); }
inline bool Isolating() { return g_cfg.isolate && !RealFg(); }
inline bool Faking() { return g_cfg.fakefocus && !RealFg(); }
bool UseVirtualCursor() {
    if (!g_hwnd) return false;
    if (OwnerCursorActive()) return false;
    int c = g_cfg.cursor; if (c == 1) return true; if (c == 2) return false;
    return !RealFg();
}
bool IsOurWindow(HWND h) { if (!h) return false; DWORD p = 0; GetWindowThreadProcessId(h, &p); return p == g_pid; }
// Both the command worker and watchdog may relinquish a hosted CoreWindow whose
// actual foreground queue belongs to an external host. Initialize our queue before
// the short input-queue attachment; no input is synthesized and no newer foreground
// choice is replaced after the initial attempt.
bool RelinquishForeground(HWND destination) {
    HWND foreground=RealForeground();
    if(!ForegroundBelongsToTarget(foreground))return true;
    if(!IsWindow(destination) || ForegroundBelongsToTarget(destination))return false;
    MSG queue;PeekMessageW(&queue,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    // Queue initialization must not widen the stale-snapshot interval: respect
    // a newer foreground choice before the first OS hand-back as well as retries.
    if(RealForeground()!=foreground)return !ComputeRealFg();
    SetLastError(0);BOOL first=o_SetForegroundWindow(destination);DWORD firstError=GetLastError();
    BOOL attached=FALSE,second=FALSE,top=FALSE;DWORD attachError=0;
    if(RealForeground()==foreground && ComputeRealFg()) {
        DWORD owner=GetWindowThreadProcessId(foreground,nullptr),me=GetCurrentThreadId();
        SetLastError(0);attached=owner && owner!=me && AttachThreadInput(me,owner,TRUE);attachError=GetLastError();
        if(attached) {
            if(RealForeground()==foreground)second=o_SetForegroundWindow(destination);
            if(RealForeground()==foreground && o_BringWindowToTop)top=o_BringWindowToTop(destination);
            AttachThreadInput(me,owner,FALSE);
        }
    }
    Log("foreground hand-back from=%p to=%p first=%d error=%lu attached=%d attach_error=%lu second=%d top=%d actual=%p",foreground,destination,(int)first,firstError,(int)attached,attachError,(int)second,(int)top,RealForeground());
    return !ComputeRealFg();
}
bool ForegroundLeased() { return GetTickCount64() < g_fgLeaseUntil.load(); }
bool BlockForegroundActivation() { return g_cfg.blockfocus && !ForegroundLeased() && !ComputeRealFg(); }

// ----------------------------------------------------------------------------------------------
// posted-mouse-message ring: lets the subclass tell OUR posted mouse messages from real ones
// ----------------------------------------------------------------------------------------------
WNDPROC CurrentWindowProcedure(HWND h) {
    // The opposite A/W getter can return a call-proc thunk even in our own process.
    return (WNDPROC)(IsWindowUnicode(h) ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC));
}
struct PostedMsg { UINT m; WPARAM w; LPARAM l; DWORD t; };
SRWLOCK g_ringLock = SRWLOCK_INIT;
PostedMsg g_ring[512];
int g_ringPos = 0;
int RingAdd(UINT m, WPARAM w, LPARAM l) {
    AcquireSRWLockExclusive(&g_ringLock);
    int slot = g_ringPos;
    g_ring[g_ringPos] = {m, w, l, GetTickCount()}; g_ringPos = (g_ringPos + 1) % 512;
    ReleaseSRWLockExclusive(&g_ringLock);
    return slot;
}
bool RingConsume(UINT m, WPARAM w, LPARAM l) {
    bool hit = false;
    AcquireSRWLockExclusive(&g_ringLock);
    for (int i = 0; i < 512; i++) if (g_ring[i].m == m && g_ring[i].w == w && g_ring[i].l == l && g_ring[i].t) { g_ring[i].t = 0; g_ring[i].m = 0; hit = true; break; }
    ReleaseSRWLockExclusive(&g_ringLock);
    return hit;
}
int MessageKey(UINT m, WPARAM w, bool& down) {
    down = m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_XBUTTONDOWN;
    if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_KEYUP || m == WM_SYSKEYUP) return w > 0 && w < 256 ? (int)w : 0;
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) return VK_LBUTTON;
    if (m == WM_RBUTTONDOWN || m == WM_RBUTTONUP) return VK_RBUTTON;
    if (m == WM_MBUTTONDOWN || m == WM_MBUTTONUP) return VK_MBUTTON;
    if (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP) return HIWORD(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    return 0;
}
bool PostOurs(HWND h, UINT m, WPARAM w, LPARAM l) {
    // A missing window is not an attempted (and certainly not an accepted) post.
    DWORD targetOwner=0;if(!h||!GetWindowThreadProcessId(h,&targetOwner)||targetOwner!=g_pid){DWORD error=targetOwner?ERROR_ACCESS_DENIED:ERROR_INVALID_WINDOW_HANDLE;++C.post_target_rejected;C.last_post_target_error=error;SetLastError(error);return false;}
    bool down = false; int vk = MessageKey(m, w, down);
    if (vk) g_keyTrace[vk].posting(down);
    int slot = -1;
    if ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST)) slot = RingAdd(m, w, l);
    BOOL accepted = PostMessageW(h, m, w, l); DWORD error = accepted ? 0 : GetLastError(); C.posted++;
    if (vk) g_keyTrace[vk].post_result(down, accepted != FALSE, error);
    if (!accepted && slot >= 0) {
        AcquireSRWLockExclusive(&g_ringLock);
        if (g_ring[slot].m == m && g_ring[slot].w == w && g_ring[slot].l == l) g_ring[slot] = {};
        ReleaseSRWLockExclusive(&g_ringLock);
    }
    return accepted != FALSE;
}

// Posted after coordinate hover, acknowledged only if it traverses the existing WndProc chain.
// A sent WM_NULL would jump ahead of queued mouse messages and cannot establish queue progress.
UINT g_hoverBarrierMessage = 0;
ULONG_PTR g_hoverBarrierCookie = 0;
std::atomic<ULONG_PTR> g_hoverBarrierSequence{0}, g_hoverBarrierExpected{0}, g_hoverBarrierAck{0};

// ----------------------------------------------------------------------------------------------
// raw input synthesis
// ----------------------------------------------------------------------------------------------
const ULONG_PTR kMagicBase = 0x56490000;  // fake HRAWINPUT values: 0x5649xxxx
RAWINPUT g_rawRing[4096];
std::atomic<bool> g_rawRingUnread[4096]{};std::mutex g_rawRingMx;std::atomic<unsigned long long> g_rawRingOverwrites{0};
std::atomic<unsigned> g_rawRingPos{0};
std::mutex g_rawQMx;
std::deque<RAWINPUT> g_rawQ;          // for GetRawInputBuffer consumers
std::atomic<DWORD> g_lastRIB{0};      // tick of last GetRawInputBuffer call
inline bool IsMagic(HRAWINPUT h) { ULONG_PTR v = (ULONG_PTR)h; return (v & ~(ULONG_PTR)0xFFF) == kMagicBase; }

std::atomic<unsigned long long> g_rawLastRegistrationResult{0}; // observed/success/error, one coherent snapshot
std::atomic<int> g_rawLastBackend{0}; // none/post/buffer/off/rejected/post_failed/queue_full
struct RawReg {
    bool kbd=false,mouse=false,kbdNoLegacy=false,mouseNoLegacy=false,queryOk=false;
    HWND kbdTarget=nullptr,mouseTarget=nullptr;DWORD queryError=0;UINT count=0;
};
RawReg QueryRawReg(){
    RawReg r;UINT count=0;SetLastError(0);UINT first=GetRegisteredRawInputDevices(nullptr,&count,sizeof(RAWINPUTDEVICE));DWORD error=GetLastError();
    if(!count){r.queryOk=first!=(UINT)-1||error==ERROR_INSUFFICIENT_BUFFER;r.queryError=r.queryOk?0:(error?error:ERROR_GEN_FAILURE);return r;}
    for(int attempt=0;attempt<3;attempt++){
        if(count>1024){r.queryError=ERROR_BUFFER_OVERFLOW;return r;}
        std::vector<RAWINPUTDEVICE> devices(count);UINT capacity=count;SetLastError(0);
        UINT written=GetRegisteredRawInputDevices(devices.data(),&capacity,sizeof(RAWINPUTDEVICE));error=GetLastError();
        if(written==(UINT)-1){if(error==ERROR_INSUFFICIENT_BUFFER&&capacity>count){count=capacity;continue;}r.queryError=error?error:ERROR_GEN_FAILURE;return r;}
        if(written>devices.size()){r.queryError=ERROR_INVALID_DATA;return r;}
        r.queryOk=true;r.count=written;
        for(UINT i=0;i<written;i++){const auto& d=devices[i];if(d.usUsagePage!=1)continue;
            if(d.usUsage==6){r.kbd=true;r.kbdNoLegacy=(d.dwFlags&RIDEV_NOLEGACY)!=0;r.kbdTarget=d.hwndTarget;}
            if(d.usUsage==2){r.mouse=true;r.mouseNoLegacy=(d.dwFlags&RIDEV_NOLEGACY)!=0;r.mouseTarget=d.hwndTarget;}
        }
        return r;
    }
    r.queryError=ERROR_RETRY;return r;
}
bool RawOwnTarget(HWND window){DWORD pid=0;return window&&IsWindow(window)&&GetWindowThreadProcessId(window,&pid)&&pid==g_pid;}
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
int EmitRaw(const RAWINPUT& ri,HWND target,bool* overwritten=nullptr){
    int mode=g_cfg.rawmode;if(mode==3){g_rawLastBackend=3;return 3;}
#ifdef _WIN64
    bool useBuffer=mode==2||(mode==0&&g_lastRIB&&GetTickCount()-g_lastRIB<3000);
#else
    bool useBuffer=false; // this build cannot append correctly aligned WOW64 buffer records
#endif
    if(useBuffer){std::lock_guard<std::mutex> lock(g_rawQMx);if(g_rawQ.size()<1024){g_rawQ.push_back(ri);C.raw_queued++;g_rawLastBackend=2;return 2;}C.raw_queue_dropped++;g_rawLastBackend=6;return 6;}
    HWND destination=target?target:g_hwnd;
    // A fake HRAWINPUT is meaningful only in this process, where the decoder is hooked.
    if(!RawOwnTarget(destination)){C.raw_target_rejected++;g_rawLastBackend=4;return 4;}
    unsigned index=(g_rawRingPos++)&0xFFF;
    {std::lock_guard<std::mutex> lock(g_rawRingMx);bool lost=g_rawRingUnread[index].exchange(true);if(lost){++g_rawRingOverwrites;if(overwritten)*overwritten=true;}g_rawRing[index]=ri;}
    if(PostMessageW(destination,WM_INPUT,RIM_INPUT,(LPARAM)(kMagicBase|index))){C.raw_posted++;g_rawLastBackend=1;return 1;}
    g_rawRingUnread[index]=false;C.raw_post_failed++;g_rawLastBackend=5;return 5;
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
int RawMouse(LONG dx, LONG dy, USHORT btnFlags, SHORT wheel, HWND target,bool* overwritten=nullptr) {
    RAWINPUT ri = {};
    ri.header.dwType = RIM_TYPEMOUSE;
    ri.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    ri.header.hDevice = g_rawMouseDev;
    ri.header.wParam = RIM_INPUT;
    ri.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    ri.data.mouse.usButtonFlags = btnFlags;
    ri.data.mouse.usButtonData = (USHORT)wheel;
    ri.data.mouse.lLastX = dx; ri.data.mouse.lLastY = dy;
    return EmitRaw(ri, target,overwritten);
}

// ----------------------------------------------------------------------------------------------
// DirectInput synthesis
// ----------------------------------------------------------------------------------------------
std::mutex g_diMx;
struct DiQueuedEvent { DWORD first,second;unsigned long long generation=0; };
std::deque<DiQueuedEvent> g_diKbdQ, g_diMouseQ;  // (ofs, data, mouse-button press generation)
std::atomic<DWORD> g_diSeq{1};
int DiPush(bool kbd, DWORD ofs, DWORD data,unsigned long long generation=0) {
    if (kbd ? C.di_data_kbd == 0 : C.di_data_mouse == 0) return 0;  // nobody reads buffered data
    std::lock_guard<std::mutex> lk(g_diMx);
    auto& q = kbd ? g_diKbdQ : g_diMouseQ;
    if (q.size() < 512) {q.push_back({ofs, data,generation});return 1;}return -1;
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
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);NoteAgentInput();if(!g_jobApplying)JobsOverrideKey(vk);
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

// Snapshot and validate the cursor before exposing a button-down to any channel.
// Reuse this position for the posted message rather than rereading a game-written
// cursor after setting the key state. Concurrent application reads remain unconfirmed.
bool MouseButtonChecked(int vk, bool down, const POINT* expected, POINT* used) {
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);NoteAgentInput();
    POINT position{g_vcurX,g_vcurY};
    if(used)*used=position;
    if(down && expected && (position.x!=expected->x || position.y!=expected->y))return false;
    if(!g_jobApplying)JobsOverrideKey(vk);
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
    const unsigned long long generation=down?++g_diMouseObservation[dib].press:++g_diMouseObservation[dib].release;
    DiPush(false, DIMOFS_BUTTON0 + dib, down ? 0x80 : 0,generation);
    if (RawWanted(false, rr)) RawMouse(0, 0, bf, 0, rr.mouseTarget);
    if (MsgsWanted(false, rr) && g_hwnd) PostOurs(g_hwnd, msg, MouseKeyFlags() | extra, MAKELPARAM((SHORT)position.x,(SHORT)position.y));
    return true;
}
void MouseButton(int vk, bool down) { MouseButtonChecked(vk,down,nullptr,nullptr); }

void ClampToClient(LONG& x, LONG& y) {
    RECT rc{}; if (!g_hwnd || !GetClientRect(g_hwnd, &rc)) return;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x > rc.right - 1) x = rc.right - 1; if (y > rc.bottom - 1) y = rc.bottom - 1;
}
struct PointerPath {
    HWND window=nullptr; LONG width=0,height=0,mx=0,my=0;unsigned allowed=0;
    bool changed=false,bypassed=false;
    unsigned Contact(LONG x,LONG y)const{return (mx&&x<mx?1u:0u)|(mx&&x>=width-mx?2u:0u)|(my&&y<my?4u:0u)|(my&&y>=height-my?8u:0u);}
    bool Init(LONG x,LONG y,bool bypass=false){RECT r{};DWORD pid=0;window=g_hwnd;GetWindowThreadProcessId(window,&pid);if(pid!=g_pid||!GetClientRect(window,&r)||r.right<1||r.bottom<1)return false;
        width=r.right;height=r.bottom;mx=(std::min)((LONG)g_cfg.edgeMargin.load(),(width-1)/2);my=(std::min)((LONG)g_cfg.edgeMargin.load(),(height-1)/2);bypassed=bypass;
        x=(std::max)(0L,(std::min)(width-1,x));y=(std::max)(0L,(std::min)(height-1,y));allowed=Contact(x,y);return true;}
    bool Valid()const{RECT r{};DWORD pid=0;GetWindowThreadProcessId(window,&pid);return window==g_hwnd&&pid==g_pid&&GetClientRect(window,&r)&&r.right==width&&r.bottom==height;}
    void Guard(LONG& x,LONG& y){if(bypassed)return;LONG ox=x,oy=y;if(!(allowed&1))x=(std::max)(mx,x);if(!(allowed&2))x=(std::min)(width-1-mx,x);if(!(allowed&4))y=(std::max)(my,y);if(!(allowed&8))y=(std::min)(height-1-my,y);changed=changed||x!=ox||y!=oy;}
};
struct MotionAccounting {
    long long device_dx=0,device_dy=0,di_state_dx=0,di_state_dy=0,di_buffer_dx=0,di_buffer_dy=0,raw_posted_dx=0,raw_posted_dy=0,raw_queued_dx=0,raw_queued_dy=0,raw_failed_dx=0,raw_failed_dy=0;
    unsigned di_buffer_drops=0,raw_attempts=0,raw_failures=0,raw_overwrites=0;int raw_backend=0;
    bool cursor_clamped=false,cursor_saturated=false,di_state_rejected=false,raw_query_failed=false;
    void Add(const MotionAccounting& a){device_dx+=a.device_dx;device_dy+=a.device_dy;di_state_dx+=a.di_state_dx;di_state_dy+=a.di_state_dy;di_buffer_dx+=a.di_buffer_dx;di_buffer_dy+=a.di_buffer_dy;raw_posted_dx+=a.raw_posted_dx;raw_posted_dy+=a.raw_posted_dy;raw_queued_dx+=a.raw_queued_dx;raw_queued_dy+=a.raw_queued_dy;raw_failed_dx+=a.raw_failed_dx;raw_failed_dy+=a.raw_failed_dy;di_buffer_drops+=a.di_buffer_drops;raw_attempts+=a.raw_attempts;raw_failures+=a.raw_failures;raw_overwrites+=a.raw_overwrites;raw_backend=a.raw_backend;cursor_clamped=cursor_clamped||a.cursor_clamped;cursor_saturated=cursor_saturated||a.cursor_saturated;di_state_rejected=di_state_rejected||a.di_state_rejected;raw_query_failed=raw_query_failed||a.raw_query_failed;}
};
struct AppliedMouse { long long dx=0, dy=0; bool message_attempted=false, message_accepted=false;MotionAccounting accounting; };
AppliedMouse MouseMoveTo(LONG x, LONG y, bool relativeOrigin, LONG rdx, LONG rdy, bool unbounded=false,PointerPath* path=nullptr,const POINT* pathPoint=nullptr) {
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);NoteAgentInput();
    if(!g_jobApplying)JobsOverrideCursor("manual_cursor_override");
    LONG ox = g_vcurX, oy = g_vcurY;
    long long dx = relativeOrigin ? rdx : (long long)x-ox, dy = relativeOrigin ? rdy : (long long)y-oy;
    long long wideX = relativeOrigin ? (long long)ox + rdx : x, wideY = relativeOrigin ? (long long)oy + rdy : y;
    LONG nx = (LONG)(std::max)(-2147483648LL, (std::min)(2147483647LL, wideX));
    LONG ny = (LONG)(std::max)(-2147483648LL, (std::min)(2147483647LL, wideY));
    if(pathPoint){nx=pathPoint->x;ny=pathPoint->y;}
    AppliedMouse a;a.accounting.cursor_saturated=wideX<-2147483648LL||wideX>2147483647LL||wideY<-2147483648LL||wideY>2147483647LL;
    if (!unbounded) {LONG beforeX=nx,beforeY=ny;ClampToClient(nx, ny);a.accounting.cursor_clamped=nx!=beforeX||ny!=beforeY;}
    if(path&&!unbounded){path->Guard(nx,ny);if(path->changed){dx=(long long)nx-ox;dy=(long long)ny-oy;}}
    g_vcurX = nx; g_vcurY = ny; g_vcurTouched = true;
    ++g_pointerIssued;
    a.accounting.device_dx=dx;a.accounting.device_dy=dy;
    if(AddDiDelta(g_diDX,dx))a.accounting.di_state_dx=dx;else a.accounting.di_state_rejected=true;
    if(AddDiDelta(g_diDY,dy))a.accounting.di_state_dy=dy;else a.accounting.di_state_rejected=true;
    RawReg rr = QueryRawReg();
    a.accounting.raw_query_failed=!rr.queryOk;const bool raw=RawWanted(false,rr);if(g_cfg.rawmode==3)a.accounting.raw_backend=3;
    long long leftX=dx,leftY=dy;
    do {LONG chunkX=(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,leftX)),chunkY=(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,leftY));
        int queuedX=DiPush(false,DIMOFS_X,(DWORD)chunkX),queuedY=DiPush(false,DIMOFS_Y,(DWORD)chunkY);
        if(queuedX>0)a.accounting.di_buffer_dx+=chunkX;else if(queuedX<0)++a.accounting.di_buffer_drops;
        if(queuedY>0)a.accounting.di_buffer_dy+=chunkY;else if(queuedY<0)++a.accounting.di_buffer_drops;
        if(raw&&(chunkX||chunkY)){bool overwritten=false;int backend=RawMouse(chunkX,chunkY,0,0,rr.mouseTarget,&overwritten);++a.accounting.raw_attempts;a.accounting.raw_backend=backend;if(overwritten)++a.accounting.raw_overwrites;
            if(backend==1){a.accounting.raw_posted_dx+=chunkX;a.accounting.raw_posted_dy+=chunkY;}else if(backend==2){a.accounting.raw_queued_dx+=chunkX;a.accounting.raw_queued_dy+=chunkY;}else{++a.accounting.raw_failures;a.accounting.raw_failed_dx+=chunkX;a.accounting.raw_failed_dy+=chunkY;}}
        leftX-=chunkX;leftY-=chunkY;
    }while(leftX||leftY);
    bool attempted = MsgsWanted(false, rr) && g_hwnd;
    bool accepted = attempted && PostOurs(g_hwnd, WM_MOUSEMOVE, MouseKeyFlags(), ClientLParam());
    a.dx=(long long)nx-ox;a.dy=(long long)ny-oy;a.message_attempted=attempted;a.message_accepted=accepted;return a;
}
void MouseWheel(int delta, bool horizontal) {
    NoteAgentInput();
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
    if (BlockForegroundActivation()) { C.setfg_blocked++; return TRUE; }
    return o_SetForegroundWindow(h);
}
using PFN_SetFocus = HWND(WINAPI*)(HWND);
PFN_SetFocus o_SetFocus = nullptr, o_SetActiveWindow = nullptr;
std::atomic<long> g_setFocusCalls{0}, g_setActiveCalls{0};
HWND WINAPI h_SetActiveWindow(HWND h) {
    g_setActiveCalls++;
    if (BlockForegroundActivation()) { Log("SetActiveWindow(%p) while in background -> suppressed", h); C.setfg_blocked++; return o_GetActiveWindow ? o_GetActiveWindow() : nullptr; }
    return o_SetActiveWindow(h);
}
HWND WINAPI h_SetFocus(HWND h) {
    g_setFocusCalls++;
    if (!RealFg()) Log("trace SetFocus(%p) in background", h);
    // SetFocus on a window whose top-level is not active ACTIVATES it (and can take the foreground when the
    // system has no foreground window). While in the background only allow it inside the already-active tree.
    if (BlockForegroundActivation() && h) {
        HWND act = o_GetActiveWindow ? o_GetActiveWindow() : nullptr;
        if (!act || GetAncestor(h, GA_ROOT) != act) { C.setfg_blocked++; return o_GetFocus ? o_GetFocus() : nullptr; }
    }
    return o_SetFocus(h);
}
BOOL WINAPI h_BringWindowToTop(HWND h) {
    if (BlockForegroundActivation()) { C.setfg_blocked++; return TRUE; }
    return o_BringWindowToTop(h);
}
BOOL WINAPI h_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT f) {
    if (!(f & SWP_NOACTIVATE) && IsOurWindow(h) && !GetParent(h)) Log("trace SetWindowPos(%p, after=%p, flags=0x%X) fg=%d", h, after, f, (int)RealFg());
    if (BlockForegroundActivation() && IsOurWindow(h)) {
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
    if (BlockForegroundActivation() && IsOurWindow(h)) {
        if (cmd == SW_SHOW) cmd = SW_SHOWNA; else if (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE || cmd == SW_SHOWDEFAULT || cmd == SW_SHOWMAXIMIZED) cmd = SW_SHOWNOACTIVATE;
    }
    return o_ShowWindow(h, cmd);
}
BOOL WINAPI h_ShowWindowAsync(HWND h, int cmd) {
    if (g_cfg.keepvisible && !ComputeRealFg() && h == g_hwnd &&
        (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE)) {
        C.min_blocked++; return TRUE;
    }
    if (BlockForegroundActivation() && IsOurWindow(h)) {
        if (cmd == SW_SHOW) cmd = SW_SHOWNA;
        else if (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE || cmd == SW_SHOWDEFAULT || cmd == SW_SHOWMAXIMIZED) cmd = SW_SHOWNOACTIVATE;
        C.setfg_blocked++;
    }
    return o_ShowWindowAsync(h,cmd);
}
bool DeferVisibleWindow(DWORD style,HWND parent) {
    return o_ShowWindow && (style&WS_VISIBLE) && !(style&WS_CHILD) && parent!=HWND_MESSAGE && BlockForegroundActivation();
}
HWND WINAPI h_CreateWindowExW(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE module,LPVOID param) {
    bool defer=DeferVisibleWindow(style,parent);
    HWND result=o_CreateWindowExW(ex,cls,title,defer?(style&~WS_VISIBLE):style,x,y,w,h,parent,menu,module,param);
    if(result && defer) {
        DWORD error=GetLastError();++C.create_visible_deferred;
        // Avoid the kernel's implicit activation during creation, then expose the
        // same window normally. Never persist WS_EX_NOACTIVATE: a human can select it.
        o_ShowWindow(result,SW_SHOWNOACTIVATE);
        Log("CreateWindowExW deferred WS_VISIBLE hwnd=%p; shown without activation",result);SetLastError(error);
    }
    return result;
}
HWND WINAPI h_CreateWindowExA(DWORD ex,LPCSTR cls,LPCSTR title,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE module,LPVOID param) {
    bool defer=DeferVisibleWindow(style,parent);
    HWND result=o_CreateWindowExA(ex,cls,title,defer?(style&~WS_VISIBLE):style,x,y,w,h,parent,menu,module,param);
    if(result && defer) {
        DWORD error=GetLastError();++C.create_visible_deferred;o_ShowWindow(result,SW_SHOWNOACTIVATE);
        Log("CreateWindowExA deferred WS_VISIBLE hwnd=%p; shown without activation",result);SetLastError(error);
    }
    return result;
}
void WINAPI h_SwitchToThisWindow(HWND h, BOOL altTab) {
    if (BlockForegroundActivation()) { C.setfg_blocked++; Log("SwitchToThisWindow(%p) while background -> suppressed",h); return; }
    o_SwitchToThisWindow(h,altTab);
}
BOOL WINAPI h_AllowSetForegroundWindow(DWORD pid) {
    if (BlockForegroundActivation()) { C.setfg_blocked++; Log("AllowSetForegroundWindow(%lu) while background -> suppressed",pid); return TRUE; }
    return o_AllowSetForegroundWindow(pid);
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
using PFN_TM = BOOL(WINAPI*)(const MSG*);
PFN_TM o_TranslateMessage = nullptr;
// TranslateMessage derives WM_CHAR from the thread's REAL keyboard state, which a posted key message never changes. With a
// virtual Ctrl or Shift held, a posted WM_KEYDOWN for 'a' therefore became a plain 'a' character, not the Ctrl+A code
// (measured: key ctrl+a typed an "a" into a text field). Translate those messages from vinput's own key state instead.
BOOL WINAPI h_TranslateMessage(const MSG* msg) {
    if (!msg || !g_hwnd || msg->hwnd != g_hwnd || (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) ||
        !(g_vk[VK_CONTROL] || g_vk[VK_SHIFT]) || msg->wParam == 0 || msg->wParam > 255)
        return o_TranslateMessage(msg);
    BYTE state[256] = {};
    for (int i = 1; i < 256; i++) if (g_vk[i]) state[i] = 0x80;
    state[VK_CAPITAL] = (BYTE)((o_GetKeyState ? o_GetKeyState(VK_CAPITAL) : GetKeyState(VK_CAPITAL)) & 1);
    wchar_t buf[8] = {};
    HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(msg->hwnd, nullptr));
    int n = ToUnicodeEx((UINT)msg->wParam, (UINT)((msg->lParam >> 16) & 0xFF), state, buf, 8, 4, layout);
    const bool sys = msg->message == WM_SYSKEYDOWN;
    if (n < 0) { PostMessageW(msg->hwnd, sys ? WM_SYSDEADCHAR : WM_DEADCHAR, (WPARAM)buf[0], msg->lParam); return TRUE; }
    for (int i = 0; i < n; i++) PostMessageW(msg->hwnd, sys ? WM_SYSCHAR : WM_CHAR, (WPARAM)buf[i], msg->lParam);
    return n > 0 ? TRUE : FALSE;
}
BOOL WINAPI h_GetCursorPos(LPPOINT p) {
    C.gcur++;
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);
    if (p && UseVirtualCursor()) { POINT c{g_vcurX, g_vcurY}; if(!ClientToScreen(g_hwnd, &c))return FALSE;*p = c;g_pointerSeen=g_pointerIssued.load();g_pointerReader=1;return TRUE; }
    return o_GetCursorPos(p);
}
BOOL WINAPI h_GetPhysicalCursorPos(LPPOINT p) {
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);
    if (p && UseVirtualCursor()) { POINT c{g_vcurX, g_vcurY}; if(!ClientToScreen(g_hwnd, &c))return FALSE;*p = c;g_pointerSeen=g_pointerIssued.load();g_pointerReader=2;return TRUE; }
    return o_GetPhysicalCursorPos(p);
}
BOOL WINAPI h_SetCursorPos(int x, int y) {
    C.scur++;
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);
    if (UseVirtualCursor()) { POINT p{x, y};if(!ScreenToClient(g_hwnd, &p))return FALSE;if(p.x!=g_vcurX||p.y!=g_vcurY){JobsOverrideCursor("game_cursor_drift");g_vcurX=p.x;g_vcurY=p.y;++g_pointerIssued;}return TRUE; }
    return o_SetCursorPos(x, y);
}
BOOL WINAPI h_SetPhysicalCursorPos(int x, int y) {
    std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);
    if (UseVirtualCursor()) { POINT p{x, y};if(!ScreenToClient(g_hwnd, &p))return FALSE;if(p.x!=g_vcurX||p.y!=g_vcurY){JobsOverrideCursor("game_cursor_drift");g_vcurX=p.x;g_vcurY=p.y;++g_pointerIssued;}return TRUE; }
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
        std::lock_guard<std::mutex> ringLock(g_rawRingMx);
        const RAWINPUT& r = g_rawRing[(ULONG_PTR)h & 0xFFF];
        UINT need = (cmd == RID_HEADER) ? (UINT)sizeof(RAWINPUTHEADER) : r.header.dwSize;
        if (!pcb) return (UINT)-1;
        if (!data) { *pcb = need; return 0; }
        if (*pcb < need) { *pcb = need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        memcpy(data, &r, need);
        if(cmd==RID_INPUT)g_rawRingUnread[(ULONG_PTR)h&0xFFF]=false;
        if (cmd == RID_INPUT && r.header.dwType == RIM_TYPEKEYBOARD && r.data.keyboard.VKey < 256)
            g_keyTrace[r.data.keyboard.VKey].raw_data.record(r.data.keyboard.Flags, !(r.data.keyboard.Flags & RI_KEY_BREAK));
        return need;
    }
    if(Isolating()){C.raw_real_denied++;SetLastError(ERROR_ACCESS_DENIED);return (UINT)-1;}
    UINT result = o_GetRawInputData(h, cmd, data, pcb, hdr);
    if (result != (UINT)-1 && cmd == RID_INPUT && data && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
        const RAWINPUT& r = *(const RAWINPUT*)data;
        if (r.header.dwType == RIM_TYPEKEYBOARD && r.data.keyboard.VKey < 256)
            g_keyTrace[r.data.keyboard.VKey].raw_data.record(r.data.keyboard.Flags, !(r.data.keyboard.Flags & RI_KEY_BREAK));
    }
    return result;
}
UINT WINAPI h_GetRawInputBuffer(PRAWINPUT data,PUINT pcb,UINT hdr){
    C.rib++;UINT capacity=pcb?*pcb:0;
    UINT count=o_GetRawInputBuffer(data,pcb,hdr);DWORD nativeError=GetLastError();
    if(count!=(UINT)-1&&Isolating())count=0; // same visible-count isolation policy on both architectures
#ifdef _WIN64
    if(count!=(UINT)-1)g_lastRIB=GetTickCount();
    UINT pendingNeed=0;{std::lock_guard<std::mutex> lock(g_rawQMx);if(!g_rawQ.empty())pendingNeed=g_rawQ.front().header.dwSize;}
    if(!data){if(count!=(UINT)-1&&pcb&&pendingNeed&&*pcb<pendingNeed)*pcb=pendingNeed;SetLastError(nativeError);return count;}
    if(count==(UINT)-1){SetLastError(nativeError);return count;} // never turn invalid arguments/native failure into success
    if(!pendingNeed||!pcb){SetLastError(nativeError);return count;}
    BYTE* base=(BYTE*)data;size_t used=0;
    // Native success can rewrite *pcb to zero. The original caller capacity is
    // authoritative for our append, and every native block is bounded/aligned.
    for(UINT i=0;i<count;i++){
        if(capacity-used<sizeof(RAWINPUTHEADER)){SetLastError(ERROR_INVALID_DATA);return (UINT)-1;}
        UINT bytes=((RAWINPUT*)(base+used))->header.dwSize;
        if(bytes<sizeof(RAWINPUTHEADER)||bytes>capacity-used){SetLastError(ERROR_INVALID_DATA);return (UINT)-1;}
        size_t span=((size_t)bytes+7)&~(size_t)7;
        if(span>capacity-used){if(i+1<count){SetLastError(ERROR_INVALID_DATA);return (UINT)-1;}used=capacity;break;}
        used+=span;
    }
    std::lock_guard<std::mutex> lock(g_rawQMx);
    while(!g_rawQ.empty()){
        const RAWINPUT& input=g_rawQ.front();UINT bytes=input.header.dwSize;
        if(bytes<sizeof(RAWINPUTHEADER)||bytes>sizeof(input)){SetLastError(ERROR_INVALID_DATA);return (UINT)-1;}
        if(bytes>capacity-used){if(!count){*pcb=bytes;SetLastError(ERROR_INSUFFICIENT_BUFFER);return (UINT)-1;}break;}
        memcpy(base+used,&input,bytes);size_t span=((size_t)bytes+7)&~(size_t)7;used+=span<=capacity-used?span:capacity-used;
        g_rawQ.pop_front();count++;C.rib_injected++;
    }
    size_t offset=0;
    for(UINT i=0;i<count;i++){
        const RAWINPUT& input=*(const RAWINPUT*)(base+offset);
        if(input.header.dwType==RIM_TYPEKEYBOARD&&input.data.keyboard.VKey<256)g_keyTrace[input.data.keyboard.VKey].raw_buffer.record(input.data.keyboard.Flags,!(input.data.keyboard.Flags&RI_KEY_BREAK));
        offset+=((size_t)input.header.dwSize+7)&~(size_t)7;
    }
#else
    (void)capacity;
#endif
    SetLastError(nativeError);return count;
}
BOOL WINAPI h_RegisterRawInputDevices(PCRAWINPUTDEVICE devices,UINT count,UINT size){
    C.reg_raw++;BOOL result=o_RegisterRawInputDevices(devices,count,size);DWORD nativeError=GetLastError();
    g_rawLastRegistrationResult=(1ULL<<63)|(result?(1ULL<<32):0)|(unsigned long long)(result?0:nativeError);
    if(result){C.reg_raw_ok++;if(devices&&size==sizeof(RAWINPUTDEVICE)&&count<=1024){for(UINT i=0;i<count;i++){const auto& d=devices[i];if(d.dwFlags&RIDEV_REMOVE)C.reg_raw_removed++;Log("RegisterRawInputDevices success page=%u usage=%u flags=0x%X target=%p",d.usUsagePage,d.usUsage,d.dwFlags,d.hwndTarget);}}}
    else{C.reg_raw_failed++;Log("RegisterRawInputDevices failed error=%lu count=%u size=%u",nativeError,count,size);}
    SetLastError(nativeError);return result; // logging must not erase the caller's native error
}

// ----------------------------------------------------------------------------------------------
// window subclass: fake focus + isolation of real mouse/raw input in the background
// ----------------------------------------------------------------------------------------------
LRESULT CallOrig(HWND h, UINT m, WPARAM w, LPARAM l) {
    return g_hwndUnicode ? CallWindowProcW(g_origWndProc, h, m, w, l) : CallWindowProcA(g_origWndProc, h, m, w, l);
}
bool AdoptionHumanSelectionPending() {
    return g_userIntent.Active() || g_userIntent.Provisional() || g_userIntent.EvaluatingAltTab();
}

thread_local unsigned g_adoptionBarrierDepth=0;
LRESULT CALLBACK AdoptionActivationCbt(int code,WPARAM window,LPARAM detail) {
    if(code>=0 && g_adoptionBarrierDepth && (code==HCBT_ACTIVATE || code==HCBT_SETFOCUS) &&
       IsOurWindow((HWND)window) && g_cfg.blockfocus && !ForegroundLeased() &&
       !ComputeRealFg() && !AdoptionHumanSelectionPending()) {
        // A real mouse activation is the human's choice. The thread hook never
        // monitors mouse/keyboard events or affects another process's windows.
        if(code==HCBT_ACTIVATE && detail && ((CBTACTIVATESTRUCT*)detail)->fMouse)
            return CallNextHookEx(nullptr,code,window,detail);
        ++C.act_cbt_blocked;return 1;
    }
    return CallNextHookEx(nullptr,code,window,detail);
}
struct ScopedAdoptionActivationBarrier {
    HHOOK hook=nullptr;
    ScopedAdoptionActivationBarrier() {
        hook=SetWindowsHookExW(WH_CBT,AdoptionActivationCbt,nullptr,GetCurrentThreadId());
        if(hook){++g_adoptionBarrierDepth;++C.act_cbt_installed;}
        else {++C.act_cbt_failed;Log("virtual activation adoption barrier install FAILED error=%lu",GetLastError());}
    }
    ~ScopedAdoptionActivationBarrier() {
        if(hook){--g_adoptionBarrierDepth;if(!UnhookWindowsHookEx(hook)){++C.act_cbt_unhook_failed;g_adoptionActivationSafe=false;Log("virtual activation adoption barrier unhook FAILED error=%lu",GetLastError());}}
    }
};
bool CanRefreshAdoptedWindow(HWND h) {
    return g_adoptionActivationSafe && g_cfg.autoactivate && g_cfg.blockfocus && !ForegroundLeased() &&
        g_cfg.fakefocus && !AdoptionHumanSelectionPending() && h==g_hwnd && IsOurWindow(h) && IsWindow(h) && GetAncestor(h,GA_ROOT)==h && !ComputeRealFg();
}
void MaybeSynthActivate() {
    std::lock_guard<std::mutex> lock(g_synthMx);
    HWND h=g_hwnd;
    if(!g_origWndProc || !g_adoptionActivationMessage || !CanRefreshAdoptedWindow(h) || IsIconic(h) || g_appActive==1 || g_synthLeft<=0)return;
    DWORD now=GetTickCount(),last=g_lastSynthAct.load();
    if(last && now-last<2000)return;
    g_lastSynthAct=now?now:1;--g_synthLeft;++C.act_synth;
    ULONG_PTR sequence=++g_adoptionActivationSequence;
    if(PostMessageW(h,g_adoptionActivationMessage,(WPARAM)sequence,(LPARAM)kAdoptionActivationCookie)){
        ++C.act_refresh_posted;Log("guarded application-active refresh queued hwnd=%p attempts_left=%d",h,g_synthLeft.load());
    }else{++C.act_refresh_failed;Log("guarded application-active refresh post failed hwnd=%p error=%lu",h,GetLastError());}
}
LRESULT CALLBACK h_WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if(m==g_adoptionActivationMessage && (ULONG_PTR)l==kAdoptionActivationCookie) {
        if(w!=g_adoptionActivationSequence || !CanRefreshAdoptedWindow(h) || !g_origWndProc) {
            ++C.act_refresh_cancelled;Log("virtual activation adoption refresh cancelled hwnd=%p",h);return 0;
        }
        // Execute application callbacks on its own UI thread. This private queued
        // request is never a synchronous worker SendMessage and expires on retarget,
        // config changes, a lease, or actual foreground. Preserve the original proc
        // snapshot in case application activation code creates another main window.
        WNDPROC original=g_origWndProc,chain=CurrentWindowProcedure(h);bool unicode=g_hwndUnicode;
        auto current=[&]() {return chain && w==g_adoptionActivationSequence && CanRefreshAdoptedWindow(h) &&
            GetWindowThreadProcessId(h,nullptr)==GetCurrentThreadId() && original==g_origWndProc &&
            unicode==g_hwndUnicode && (IsWindowUnicode(h)!=FALSE)==unicode && CurrentWindowProcedure(h)==chain;};
        ScopedAdoptionActivationBarrier barrier;
        if(!barrier.hook || !current()){++C.act_refresh_cancelled;return 0;}
        ++C.act_refresh_delivered;
        if(unicode)CallWindowProcW(original,h,WM_ACTIVATEAPP,TRUE,0);else CallWindowProcA(original,h,WM_ACTIVATEAPP,TRUE,0);
        // APP reached this window even if its callback replaces the outer chain.
        // Never retry into a changed chain merely because the second replay cancelled.
        if(h==g_hwnd && w==g_adoptionActivationSequence)g_appActive=1;
        if(current()) {
            ++C.act_refresh_delivered;
            if(unicode)CallWindowProcW(original,h,WM_ACTIVATE,WA_ACTIVE,0);else CallWindowProcA(original,h,WM_ACTIVATE,WA_ACTIVE,0);
        } else ++C.act_refresh_cancelled;
        Log("virtual activation adoption refresh dispatched hwnd=%p actual_fg=%d",h,(int)ComputeRealFg());return 0;
    }
    if (m == g_hoverBarrierMessage && g_hoverBarrierMessage && h == g_hwnd && (ULONG_PTR)l == g_hoverBarrierCookie && w == g_hoverBarrierExpected.load()) {
        g_hoverBarrierAck = w; return 0;
    }
    bool fg = ComputeRealFg();
    g_realFg = fg;
    if(m==WM_MOUSEACTIVATE){g_userIntent.MouseActivate(h,g_pid,l,RealForeground());NoteOwnerInput(true);}  // only a real mouse click produces this
    if(!fg && (m==WM_KILLFOCUS || (m==WM_ACTIVATEAPP && !w) || (m==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE)))g_userIntent.Lost(h,GetTickCount());
    if (g_cfg.fakefocus) {
        switch (m) {
        case WM_ACTIVATEAPP: if (!w) { C.act_swallowed++; return 0; } break;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !IsOurWindow((HWND)l)) { C.act_swallowed++; return 0; } break;
        case WM_NCACTIVATE: if (!w && !fg) { C.act_swallowed++; return TRUE; } break;
        case WM_KILLFOCUS: if (!IsOurWindow((HWND)w)) { C.act_swallowed++; return 0; } break;
        }
    }
    if(h==g_hwnd && m==WM_ACTIVATEAPP){g_appActive=w?1:0;if(!w)g_synthLeft=3;}
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
        // Not WM_CHAR: the type command posts characters outside the ring, and TranslateMessage derives them from keys.
        if (!ours && fg && m != WM_CHAR && m != WM_SYSCHAR) NoteOwnerInput(true);
        if (ours && w > 0 && w < 256 && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_KEYUP || m == WM_SYSKEYUP))
            g_keyTrace[w].delivery(m == WM_KEYDOWN || m == WM_SYSKEYDOWN);
    }
    if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) {
        C.wm_mouse++;
        bool ours = RingConsume(m, w, l);
        if (!ours && g_cfg.isolate && !fg) { C.wm_mouse_dropped++; return 0; }
        if (!ours && fg) NoteOwnerInput(m != WM_MOUSEMOVE);
        if (ours && m == WM_MOUSEMOVE) g_mouseMovesDelivered++;
        bool down = false; int vk = MessageKey(m, w, down);
        if (ours && vk) g_keyTrace[vk].delivery(down);
    }
    if (m == WM_INPUT) {
        C.wm_input++;
        if (!IsMagic((HRAWINPUT)l) && fg) NoteOwnerInput(false);
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
    // Count only deactivation actually observed and forwarded by this subclass.
    // Losses before adoption are not retrospectively inferred from the app state.
    if(!fg && ((m==WM_ACTIVATEAPP && !w) || (m==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) || m==WM_KILLFOCUS)) {
        ++C.act_leaked;Log("deactivation leaked to %p msg=0x%X w=0x%llX fakefocus=%d",h,m,(unsigned long long)w,(int)g_cfg.fakefocus.load());
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
// Legacy dinput.dll (DirectInput 1-7) device implementations can differ by class.
constexpr int kDiLegacySets = 2;
PFN_DIGDS o_DILGetDeviceState[kDiLegacySets] = {};
PFN_DIGDD o_DILGetDeviceData[kDiLegacySets] = {};
PFN_DIACQ o_DILAcquire[kDiLegacySets] = {};
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
    if (FAILED(chr)) { caps = {}; caps.dwSize = 24; chr = gc(self, &caps); } // DX3 DIDEVCAPS
    if (SUCCEEDED(chr)) { BYTE dt = GET_DIDEVICE_TYPE(caps.dwDevType); t = (dt == DI8DEVTYPE_KEYBOARD || dt == 3) ? 1 : (dt == DI8DEVTYPE_MOUSE || dt == 2) ? 2 : 0; }
    AcquireSRWLockExclusive(&g_devLock); g_devType[self] = t; ReleaseSRWLockExclusive(&g_devLock);
    return t;
}
HRESULT DIAcquireImpl(PFN_DIACQ orig, void* self) {
    C.di_acq++;
    HRESULT hr = orig(self);
    if (FAILED(hr) && Faking()) { C.di_fake++; return DI_OK; }
    return hr;
}
// DirectInput polls all 249 VKs every frame. Resolve once per polling thread/layout,
// rather than calling MapVirtualKeyW for every trace slot on every GetDeviceState.
// A thread can switch its keyboard layout at runtime, so an immutable process-wide
// startup table would retain the wrong mapping after a layout change.
const BYTE* DiScanMap() {
    struct Cache { HKL layout = nullptr; BYTE scan[256] = {}; };
    thread_local Cache cache;
    HKL layout = GetKeyboardLayout(0);
    if (cache.layout != layout) {
        for (int vk = 1; vk < 256; ++vk) {
            UINT scan = vk == VK_RSHIFT ? 0x36 : MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout) & 0xff;
            scan |= IsExtendedVk(vk) ? 0x80 : 0;
            cache.scan[vk] = scan < 256 ? (BYTE)scan : 0;
        }
        cache.layout = layout;
    }
    return cache.scan;
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
        const BYTE* scan = DiScanMap();
        for (int vk = 1; vk < 256; vk++) if (g_vk[vk] && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk > 6) {
            UINT sc = scan[vk]; if (sc) k[sc] = 0x80;
        }
        for (int vk = 7; vk < 256; ++vk) {
            UINT sc = scan[vk];
            if (sc) g_keyTrace[vk].di_state.record(k[sc]);
        }
    } else {
        C.di_mouse++;
        std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);
        DIMOUSESTATE2* m = (DIMOUSESTATE2*)data;
        m->lX=ReadDiDelta(g_diDX,m->lX);m->lY=ReadDiDelta(g_diDY,m->lY);m->lZ+=g_diDZ.exchange(0);
        const bool mouseDevice=DevType(self)==2;
        const int buttons=cb==sizeof(DIMOUSESTATE2)?5:4;
        for(int button=0;button<buttons;button++) {
            auto& observation=g_diMouseObservation[button];const auto generation=observation.press.load(),released=observation.release.load();const bool virtualDown=g_vk[kDiMouseVk[button]]!=0;
            if(virtualDown)m->rgbButtons[button]=0x80;
            if(mouseDevice){g_keyTrace[kDiMouseVk[button]].di_state.record(m->rgbButtons[button]);observation.state_poll_at=GetTickCount64();
                if(virtualDown&&(m->rgbButtons[button]&0x80))ObserveDiMouseDown(button,generation,false);
                else if(!virtualDown&&!(m->rgbButtons[button]&0x80))ObserveDiMouseUp(button,released,false);}
        }
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
    if(t==2 && req)g_diMouseBufferPollAt=GetTickCount64();
    if(t==2)for(DWORD i=0;i<have && i<req;i++) {
        auto* event=(DIDEVICEOBJECTDATA*)((BYTE*)rg+(size_t)i*cbObj);
        if(event->dwOfs>=DIMOFS_BUTTON0 && event->dwOfs<=DIMOFS_BUTTON4)g_keyTrace[kDiMouseVk[event->dwOfs-DIMOFS_BUTTON0]].di_buffer.record((int)event->dwData);
    }
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
        } else if(d->dwOfs>=DIMOFS_BUTTON0 && d->dwOfs<=DIMOFS_BUTTON4) {
            const int button=(int)(d->dwOfs-DIMOFS_BUTTON0);g_keyTrace[kDiMouseVk[button]].di_buffer.record((int)d->dwData);
            if(d->dwData&0x80)ObserveDiMouseDown(button,q.front().generation,true);
            else ObserveDiMouseUp(button,q.front().generation,true);
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
    if (user != 0 || !s) return r;
    std::lock_guard<std::mutex> stateLock(g_padStateMx);if(!g_padActive)return r;
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
#include "vi_fullscreen.h"

std::atomic<LONG> g_capReq{0};  // 0 idle, 1 requested, 2 in progress
HANDLE g_capEvent = nullptr;
std::vector<BYTE> g_capPix; UINT g_capW = 0, g_capH = 0; std::string g_capErr; DXGI_FORMAT g_capFmt = DXGI_FORMAT_UNKNOWN;
// Backend scratch is never the coordinate map. Only a successfully encoded single image commits this snapshot.
struct SavedCaptureMap { bool valid=false;UINT w=0,h=0;double ox=0,oy=0,cw=0,ch=0; };
SavedCaptureMap g_savedCaptureMap; // read/written only by commands under execMx
std::atomic<UINT> g_capturePixelLimit{0}; // optional transforms/bursts; zero preserves legacy PNG limits
bool CaptureDimensionsAllowed(std::uint64_t w,std::uint64_t h) {
    UINT limit=g_capturePixelLimit;
    if(!limit)return true;
    if(!w||!h||w>16384||h>16384||w>(std::uint64_t)limit/h){g_capErr="capture source exceeds optional-output 16 million pixel budget";return false;}
    return true;
}
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
enum { API_NONE = 0, API_GL = 1, API_VK = 2, API_PRINTWINDOW = 3, API_DDRAW = 4, API_D3D11 = 11, API_D3D12 = 12, API_FOREIGN = -1 };
const char* ApiName(int a) {
    switch (a) { case API_D3D11: return "d3d11"; case API_D3D12: return "d3d12"; case API_DDRAW: return "ddraw"; case API_GL: return "gl"; case API_VK: return "vk"; case API_PRINTWINDOW: return "printwindow"; case API_FOREIGN: return "foreign"; default: return "none"; }
}
std::atomic<int> g_frameApi{0};        // API of the frames counted in present_ours (last one seen)
std::atomic<int> g_capApi{0};          // API that served the last capture request (set with g_capReq -> done)
std::atomic<int> g_lastCaptureApi{0};  // last successful request, including PrintWindow; failed attempts do not change it
std::atomic<int> g_capPending{0};      // API whose GPU readback the requester must finish (12 / vk), 0 = pixels ready
std::atomic<bool> g_capRetryable{false}; // the hook failed for a transient reason (queue hook still installing)
std::atomic<long> g_frameDd{0}, g_frameGl{0}, g_frameVk{0}, g_frame11{0}, g_frame12{0}, g_frameForeign{0};
bool HookAddr(const char* name, void* target, void* det, void** orig);
void OnFrame(int api) {
    C.present_ours++;
    g_frameApi = api;
    JobsFrame();
    switch (api) { case API_DDRAW: g_frameDd++; break; case API_GL: g_frameGl++; break; case API_VK: g_frameVk++; break; case API_D3D11: g_frame11++; break; case API_D3D12: g_frame12++; break; }
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
#include "vi_ddraw_proxy.h"
#include "vi_d3d9windowed.h"

void CaptureFromD3D11(IDXGISwapChain* sc, ID3D11Device* dev) {
    dev->AddRef();
    ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
    ID3D11Texture2D* bb = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr) || !bb) { g_capErr = "GetBuffer failed"; ctx->Release(); dev->Release(); return; }
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    if(!CaptureDimensionsAllowed(td.Width,td.Height)){bb->Release();ctx->Release();dev->Release();return;}
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

#include "vi_overlaycapture.h"
bool SavePng(const std::wstring& path, const std::vector<BYTE>& px, UINT w, UINT h, std::string& err) {
    if (!vi_paths::CaptureParent(path, err)) return false;
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
    bool focusSafe=true;
    focusSafe=HookApi(u, "GetForegroundWindow", &h_GetForegroundWindow, &o_GetForegroundWindow) && focusSafe;
    focusSafe=HookApi(u, "GetActiveWindow", &h_GetActiveWindow, &o_GetActiveWindow) && focusSafe;
    focusSafe=HookApi(u, "GetFocus", &h_GetFocus, &o_GetFocus) && focusSafe;
    focusSafe=HookApi(u, "SetForegroundWindow", &h_SetForegroundWindow, &o_SetForegroundWindow) && focusSafe;
    focusSafe=HookApi(u, "BringWindowToTop", &h_BringWindowToTop, &o_BringWindowToTop) && focusSafe;
    focusSafe=HookApi(u, "SetWindowPos", &h_SetWindowPos, &o_SetWindowPos) && focusSafe;
    focusSafe=HookApi(u, "ShowWindow", &h_ShowWindow, &o_ShowWindow) && focusSafe;
    focusSafe=HookApi(u, "ShowWindowAsync", &h_ShowWindowAsync, &o_ShowWindowAsync) && focusSafe;
    focusSafe=HookApi(u, "SwitchToThisWindow", &h_SwitchToThisWindow, &o_SwitchToThisWindow) && focusSafe;
    focusSafe=HookApi(u, "AllowSetForegroundWindow", &h_AllowSetForegroundWindow, &o_AllowSetForegroundWindow) && focusSafe;
    focusSafe=HookApi(u, "SetFocus", &h_SetFocus, &o_SetFocus) && focusSafe;
    focusSafe=HookApi(u, "SetActiveWindow", &h_SetActiveWindow, &o_SetActiveWindow) && focusSafe;
    focusSafe=HookApi(u,"CreateWindowExW",&h_CreateWindowExW,&o_CreateWindowExW) && focusSafe;
    focusSafe=HookApi(u,"CreateWindowExA",&h_CreateWindowExA,&o_CreateWindowExA) && focusSafe;
    g_adoptionActivationMessage=focusSafe?RegisterWindowMessageW(L"vinput.virtual.activation.adoption.v1"):0;
    g_adoptionActivationSafe=focusSafe && g_adoptionActivationMessage!=0;
    HookApi(u, "GetAsyncKeyState", &h_GetAsyncKeyState, &o_GetAsyncKeyState);
    HookApi(u, "GetKeyState", &h_GetKeyState, &o_GetKeyState);
    HookApi(u, "GetKeyboardState", &h_GetKeyboardState, &o_GetKeyboardState);
    HookApi(u, "TranslateMessage", &h_TranslateMessage, &o_TranslateMessage);
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
        HookAddr("IDXGISwapChain::GetFullscreenState", vt[11], (void*)&h_GetFullscreenState, (void**)&o_GetFullscreenState);
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
    struct AdoptionScope{AdoptionScope(){++g_jobRetargetDepth;}~AdoptionScope(){--g_jobRetargetDepth;}} adoptionScope;
    JobsCancelAll("target_changed");
    if (g_hwnd && IsWindow(g_hwnd) && g_origWndProc && CurrentWindowProcedure(g_hwnd) == h_WndProc) {
        if (g_hwndUnicode) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        else SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
    }
    g_hwndUnicode = IsWindowUnicode(h) != FALSE;
    WNDPROC prev = g_hwndUnicode ? (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)h_WndProc) : (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)h_WndProc);
    g_origWndProc = prev;
    RECT rc; GetClientRect(h, &rc);
    {std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);if (!g_vcurTouched) { g_vcurX = rc.right / 2; g_vcurY = rc.bottom / 2;++g_pointerIssued; }g_hwnd = h;}
    {std::lock_guard<std::mutex> lock(g_synthMx);g_appActive=-1;g_lastSynthAct=0;g_synthLeft=3;}
    if(prev)MaybeSynthActivate();
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
void EdgeFields(JOut& o,const PointerPath* path=nullptr){std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);PointerPath live;const PointerPath* p=path;if(!p&&live.Init(g_vcurX,g_vcurY))p=&live;
    o.num("edge_margin",g_cfg.edgeMargin);o.bol("edge_contact",p&&p->Contact(g_vcurX,g_vcurY)!=0);o.num("edge_sides",p?p->Contact(g_vcurX,g_vcurY):0);
    o.bol("edge_guard_applied",p&&p->changed);o.bol("edge_guard_bypassed",p&&p->bypassed);if(p){o.num("edge_margin_x",p->mx);o.num("edge_margin_y",p->my);}}
void AccountingFields(JOut& out,const MotionAccounting& a,long long requestedX,long long requestedY,unsigned steps){
    out.num("relative_accounting_version",1);out.num("requested_dx",(double)requestedX);out.num("requested_dy",(double)requestedY);out.num("device_dx",(double)a.device_dx);out.num("device_dy",(double)a.device_dy);out.num("steps_published",steps);
    out.bol("cursor_clamped",a.cursor_clamped);out.bol("cursor_saturated",a.cursor_saturated);
    JOut delivery;delivery.num("di_state_accumulated_dx",(double)a.di_state_dx);delivery.num("di_state_accumulated_dy",(double)a.di_state_dy);delivery.bol("di_state_rejected",a.di_state_rejected);delivery.num("di_buffer_queued_dx",(double)a.di_buffer_dx);delivery.num("di_buffer_queued_dy",(double)a.di_buffer_dy);delivery.num("di_buffer_dropped_events",a.di_buffer_drops);
    delivery.num("raw_posted_dx",(double)a.raw_posted_dx);delivery.num("raw_posted_dy",(double)a.raw_posted_dy);delivery.num("raw_queued_dx",(double)a.raw_queued_dx);delivery.num("raw_queued_dy",(double)a.raw_queued_dy);delivery.num("raw_failed_dx",(double)a.raw_failed_dx);delivery.num("raw_failed_dy",(double)a.raw_failed_dy);delivery.num("raw_attempts",a.raw_attempts);delivery.num("raw_failures",a.raw_failures);delivery.num("raw_unread_overwrites",a.raw_overwrites);delivery.bol("raw_registration_query_failed",a.raw_query_failed);
    static const char* names[]={"none","post","buffer","off","target_rejected","post_failed","queue_full"};delivery.str("last_raw_backend",names[a.raw_backend>=0&&a.raw_backend<7?a.raw_backend:0]);
    delivery.bol("raw_submission_exact",a.raw_attempts&&!a.raw_failures&&!a.raw_overwrites&&a.raw_posted_dx+a.raw_queued_dx==a.device_dx&&a.raw_posted_dy+a.raw_queued_dy==a.device_dy);delivery.bol("consumption_confirmed",false);out.raw("relative_delivery",delivery.done());
    if(a.raw_failures||a.raw_overwrites||a.di_buffer_drops||a.di_state_rejected||a.raw_query_failed)out.str("relative_delivery_warning","one or more input channels could not retain all requested data; inspect per-channel accounting; posts/queues are not application consumption; no input was retried");
}
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
// Anti-cheat refusal (vi_acguard.h): set when a known anti-cheat module is (or becomes) loaded in this process.
// From then on every command except ping/status/log is refused, input jobs are cancelled and held input is released.
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

// ----------------------------------------------------------------------------------------------
// commands
// ----------------------------------------------------------------------------------------------
void ReleaseAll() {
    std::lock_guard<std::recursive_mutex> lock(g_inputMx);JobsCancelAll("release_all");
    for (int vk = 1; vk < 256; vk++) if (g_vk[vk]) KeyEvent(vk, false);
    std::lock_guard<std::mutex> padLock(g_padStateMx);g_padButtons = 0; g_padSetMask = 0; g_padLT = g_padRT = g_padLX = g_padLY = g_padRX = g_padRY = 0; g_padPacket++;
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
// First time a detection is seen by a command or the monitor thread: cancel jobs, release held input, log once.
void EnforceAntiCheat() {
    if (g_acProduct.load() && !g_acLogged.exchange(true)) { JobsCancelAll("anticheat_detected"); ReleaseAll(); Log("anti-cheat module detected (%s): input and capture disabled", g_acProduct.load()); }
}

std::string KeyTraceJson(int vk) {
    if (vk <= 0 || vk > 255) return ErrJ("trace needs a valid key");
    const KeyTrace& t = g_keyTrace[vk];
    JOut o; o.bol("ok", true); o.num("vk", vk); o.str("name", vi_keys::Name(vk));
    o.bol("virtual_down", g_vk[vk] != 0);
    o.num("injected_down", (double)t.injected_down.load()); o.num("injected_up", (double)t.injected_up.load());
    o.num("message_down", (double)t.delivered_down.load()); o.num("message_up", (double)t.delivered_up.load());
    o.num("post_attempt_down", (double)t.post_attempt_down.load()); o.num("post_attempt_up", (double)t.post_attempt_up.load());
    o.num("post_accepted_down", (double)t.post_accepted_down.load()); o.num("post_accepted_up", (double)t.post_accepted_up.load());
    o.num("post_failed_down", (double)t.post_failed_down.load()); o.num("post_failed_up", (double)t.post_failed_up.load());
    o.num("last_post_error", t.last_post_error.load());
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
    o.str("meaning", "post_attempt/accepted/failed describe PostMessage calls, not dispatch or consumption; message_down/up count actual vinput subclass dispatch; an outer hook (overlay or mod) may consume posts first; API reads do not imply application action; last_post_error is the last failed PostMessage error");
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
    ULONGLONG now = HousekeepingNow(); bool active = ClaimActiveLocked(now);
    JOut c; c.bol("active", active); c.str("owner", active ? g_claimOwner : std::string());
    c.num("expires_in_ms", active ? (double)(g_claimExpiry - now) : 0); c.num("ttl_ms", active ? g_claimTtlMs : 0);
    c.num("held_ms", active ? (double)(now - g_claimSince) : 0); c.num("refused", (double)g_claimRefused.load());
    o.raw("claim", c.done());
}
// Commands that neither drive input nor change state: always allowed, and they bypass the exec mutex so an
// agent can read status while a long hover / --after-frames / hold is running on the same target.
bool IsOpenCommand(const std::string& cmd) {
    return cmd == "ping" || cmd == "status" || cmd == "log" || cmd == "trace" || cmd == "claim" || cmd == "unclaim" || cmd == "restore-foreground" || cmd == "jobs" || cmd == "job-status";
}
std::string ClaimRefusal(const char* why) {
    // caller holds g_claimMx
    ULONGLONG now = HousekeepingNow();
    JOut o; o.bol("ok", false);
    o.str("error", std::string(why) + " - target is claimed by '" + g_claimOwner + "' (pass that --owner, wait for expiry, or --force-claim)");
    o.str("claim_owner", g_claimOwner); o.num("claim_expires_in_ms", (double)(g_claimExpiry - now)); o.bol("claim_refused", true);
    return o.done();
}
// Returns an error reply when the command must be refused, empty when it may run.
std::string ClaimGate(const std::string& cmd, const JObj& j) {
    std::string owner = JStr(j, "owner"); bool force = JBool(j, "force_claim", false);
    std::lock_guard<std::mutex> lk(g_claimMx);
    ULONGLONG now = HousekeepingNow(); bool active = ClaimActiveLocked(now);
    auto admit=[&](){g_admittedJobEpoch=g_jobClaimEpoch;g_admittedJobClaimed=active;};admit();
    if (cmd == "claim") {
        if (owner.empty()) return ErrJ("claim needs an owner (vinput --owner NAME ... or VINPUT_OWNER)");
        double ttl = JNum(j, "ttl", 300);
        if (!(ttl >= 1 && ttl <= 86400)) return ErrJ("claim ttl must be 1..86400 s");
        if (active && g_claimOwner != owner && !force) { g_claimRefused++; return ClaimRefusal("claim refused"); }
        if (!active || g_claimOwner != owner) { Log("CLAIM %s by '%s' (ttl %g s)%s", active ? "TAKEN OVER" : "set", owner.c_str(), ttl, active ? (" from '" + g_claimOwner + "'").c_str() : ""); g_claimSince = now; }
        if(!active||g_claimOwner!=owner)JobsEpochChange();
        g_claimOwner = owner; g_claimTtlMs = (DWORD)(ttl * 1000); g_claimExpiry = now + g_claimTtlMs;g_jobClaimExpiry=g_claimExpiry;
        return std::string();
    }
    if (cmd == "unclaim") {
        if (!active) { JobsEpochChange();g_jobClaimExpiry=0;g_claimOwner.clear(); return std::string(); }
        if (g_claimOwner != owner && !force) { g_claimRefused++; return ClaimRefusal("unclaim refused"); }
        JobsEpochChange();g_jobClaimExpiry=0;Log("CLAIM released by '%s'", owner.c_str()); g_claimOwner.clear(); g_claimExpiry = 0;
        return std::string();
    }
    if (IsOpenCommand(cmd) || !active) return std::string();
    if (g_claimOwner == owner) { g_claimExpiry = now + g_claimTtlMs;g_jobClaimExpiry=g_claimExpiry; return std::string(); }
    if (force) { Log("CLAIM bypassed by '%s' (--force-claim) for '%s'", owner.c_str(), cmd.c_str()); return std::string(); }
    g_claimRefused++;
    return ClaimRefusal(("'" + cmd + "' refused").c_str());
}

bool ResolveXY(const JObj&,LONG&,LONG&);
#include "vi_jobs.inl"

std::string StatusJson() {
    JOut o; o.bol("ok", true); o.str("version", VI_VERSION); o.num("pid", g_pid);
#ifdef _WIN64
    o.num("bits", 64);
#else
    o.num("bits", 32);
#endif
    o.bol("app_container", g_appContainer);
    o.bol("anticheat_disabled", g_acProduct.load() != nullptr);
    o.str("log_path", W2U(g_logPath));
    char hb[32]; sprintf_s(hb, "0x%llX", (unsigned long long)(ULONG_PTR)g_hwnd); o.str("hwnd", hb);
    if (g_hwnd) {
        wchar_t cls[128] = {0}, title[256] = {0}; GetClassNameW(g_hwnd, cls, 127); GetWindowTextW(g_hwnd, title, 255);
        RECT rc; GetClientRect(g_hwnd, &rc);
        o.str("class", W2U(cls)); o.str("title", W2U(title)); o.num("client_w", rc.right); o.num("client_h", rc.bottom);
        o.bol("subclassed", CurrentWindowProcedure(g_hwnd) == h_WndProc);
    }
    bool fg = ComputeRealFg(); o.bol("really_foreground", fg);
    {
        JOut guard; guard.bol("enabled",g_cfg.blockfocus); guard.bol("armed",g_fgGuardArmed);
        guard.num("gained_count",g_fgGained); guard.num("restored_count",g_fgRestored); guard.num("failed_count",g_fgRestoreFailed);
        guard.bol("user_took_foreground",g_userIntent.Active());guard.num("user_takeover_count",g_userIntent.Count());
        guard.str("user_takeover_reason",g_userIntent.Reason()==1?"physical_mouse":g_userIntent.Reason()>=2?"physical_alt_tab":"none");
        guard.str("user_takeover_evidence",g_userIntent.Reason()==1?"target_mouse_activation":g_userIntent.Reason()==2?"os_switch_end":g_userIntent.Reason()==3?"physical_chord_target_transition_alt_release":"none");
        guard.bol("user_selection_pending",g_userIntent.Provisional());
        guard.bol("alt_tab_events_available",g_userIntent.SwitchEventsAvailable());
        guard.num("alt_tab_target_starts",g_userIntent.SwitchStarts());guard.num("alt_tab_target_ends",g_userIntent.SwitchEnds());
        ULONGLONG now=GetTickCount64(), until=g_fgLeaseUntil;
        guard.num("lease_remaining_ms",until>now ? (double)(until-now) : 0);
        char h[32];sprintf_s(h,"0x%llX",(unsigned long long)(ULONG_PTR)g_fgRestoreWindow.load());guard.str("restore_hwnd",h);
        o.raw("foreground_guard",guard.done());
    }
    o.bol("fakefocus", g_cfg.fakefocus); o.bol("autoactivate",g_cfg.autoactivate);o.num("app_active",g_appActive);o.num("autoactivate_attempts_left",g_synthLeft); o.bol("isolate", g_cfg.isolate); o.bol("blockfocus", g_cfg.blockfocus); o.bol("nofullscreen", g_cfg.nofullscreen); o.bol("keepvisible", g_cfg.keepvisible); if (g_hwnd) o.bol("minimized", IsIconic(g_hwnd) != FALSE);
    { JOut fs;fs.str("policy",g_cfg.nofullscreen?(g_fullscreenPolicy==1?"refuse":"borderless"):"native");fs.num("requests",g_fsRequests);fs.num("emulated",g_fsEmulated);fs.num("refused",g_fsRefused);fs.num("failures",g_fsFailed);fs.num("passthrough",g_fsPassthrough);fs.num("states_live",g_fsStates);fs.num("output_refs",g_fsOutputs);fs.num("last_hresult",(double)(unsigned long)g_fsLastResult.load());fs.bol("blocked_before_first_present",FullscreenBlockedBeforePresent());o.raw("fullscreen",fs.done()); }
    o.num("cursor_mode", g_cfg.cursor);o.str("cursor_mode_name",g_cfg.cursor==1?"virtual":g_cfg.cursor==2?"real":"auto");o.str("effective_cursor",UseVirtualCursor()?"virtual":"real");o.str("cursor_effective",UseVirtualCursor()?"virtual":"real");o.bol("owner_active",OwnerCursorActive());o.bol("owner_holding",g_ownerHolding);o.num("owner_input_count",g_ownerInputCount);o.num("owner_guard_standdowns",g_ownerStanddowns);if(TestHooksEnabled())o.num("test_realfg",g_testRealFg);o.bol("user_took_foreground",g_userIntent.Active()); o.num("rawmode", g_cfg.rawmode); o.num("msgmode", g_cfg.msgmode);
    o.num("vcur_x", g_vcurX); o.num("vcur_y", g_vcurY);
    EdgeFields(o);o.raw("input_precision","{\"edge_guard\":true,\"release_poll\":true,\"placement_drag\":true}");
    o.num("relative_accounting_version",1);o.str("di_pending_dx",std::to_string(g_diDX.load()));o.str("di_pending_dy",std::to_string(g_diDY.load()));o.num("raw_unread_overwrites",(double)g_rawRingOverwrites.load());
    RawReg rr = QueryRawReg();
    o.bol("raw_kbd", rr.kbd); o.bol("raw_kbd_nolegacy", rr.kbdNoLegacy); o.bol("raw_mouse", rr.mouse); o.bol("raw_mouse_nolegacy", rr.mouseNoLegacy);
    {JOut raw;raw.bol("query_ok",rr.queryOk);raw.num("query_error",rr.queryError);raw.num("count",rr.count);char k[32],m[32];sprintf_s(k,"0x%llX",(unsigned long long)(ULONG_PTR)rr.kbdTarget);sprintf_s(m,"0x%llX",(unsigned long long)(ULONG_PTR)rr.mouseTarget);raw.str("keyboard_target",k);raw.str("mouse_target",m);raw.bol("keyboard_target_owned",RawOwnTarget(rr.kbdTarget));raw.bol("mouse_target_owned",RawOwnTarget(rr.mouseTarget));char fallback[32];sprintf_s(fallback,"0x%llX",(unsigned long long)(ULONG_PTR)g_hwnd);raw.str("fallback_target",fallback);raw.bol("fallback_target_owned",RawOwnTarget(g_hwnd));unsigned long long last=g_rawLastRegistrationResult;raw.bol("last_registration_observed",(last&(1ULL<<63))!=0);raw.bol("last_registration_ok",(last&(1ULL<<32))!=0);raw.num("last_registration_error",(DWORD)last);o.raw("raw_registration",raw.done());}
    {JOut delivery;static const char* names[]={"none","post","buffer","off","target_rejected","post_failed","queue_full"};int backend=g_rawLastBackend;delivery.str("last_backend",names[backend>=0&&backend<7?backend:0]);
#ifdef _WIN64
    delivery.bol("buffer_supported",true);delivery.bol("recent_buffer_read",g_lastRIB&&GetTickCount()-g_lastRIB<3000);
#else
    delivery.bol("buffer_supported",false);delivery.bol("recent_buffer_read",false);
#endif
    o.raw("raw_delivery",delivery.done());}

    o.num("swapchain_w", g_scW); o.num("swapchain_h", g_scH); o.num("swapchain_fmt", g_scFmt); o.num("swapchain_api", g_scApi);
    {   // native capture: which API the counted frames come from, and what served the last capture
        o.str("frame_api", ApiName(g_frameApi)); o.num("frames", (double)C.present_ours.load());
        o.str("capture_path", ApiName(g_lastCaptureApi));
        o.num("capture_output_version",1);
        JOut fa; fa.num("d3d11", (double)g_frame11.load()); fa.num("d3d12", (double)g_frame12.load());
        fa.num("ddraw",(double)g_frameDd.load()); fa.num("gl", (double)g_frameGl.load()); fa.num("vk", (double)g_frameVk.load()); fa.num("foreign_dxgi", (double)g_frameForeign.load());
        o.raw("frames_by_api", fa.done());
        JOut d12; d12.num("queue_source", g_c12QueueSource); d12.bol("ecl_hook", o_ExecuteCommandLists != nullptr);
        d12.num("submits", (double)g_c12.submits); d12.num("releases", (double)g_c12.releases); d12.num("leaks", (double)g_c12.leaks); d12.num("resizes", (double)g_resizes.load());
        o.raw("d3d12", d12.done());
        o.bol("gl_hooked", o_wglSwapBuffers != nullptr);
        {JOut d;d.bol("hooked",o_D9Factory||o_D9FactoryEx);d.num("requests",g_d9Requests);d.num("emulated",g_d9Emulated);d.num("refused",g_d9Refused);d.num("failed",g_d9Failed);d.bol("blocked_before_first_present",D9BlockedBeforePresent());d.num("last_hresult",(double)(unsigned long)g_d9LastResult.load());o.raw("d3d9",d.done());}
        {JOut d;d.bol("hooked",o_DdCreate||o_DdCreateEx);d.num("cooperative_requests",g_ddCoop);d.num("mode_requests",g_ddModes);d.num("emulated",g_ddEmulated);d.num("refused",g_ddRefused);d.num("failed",g_ddFailed);d.num("presented",g_ddPresented);d.num("deferred_for_palette",g_ddDeferredPalette);d.num("objects",g_ddRecordCount);d.num("versions",g_ddVersionMask);d.bol("blocked",g_ddBlocked);d.bol("blocked_before_first_present",DdBlockedBeforePresent());d.num("last_hresult",(double)(unsigned long)g_ddLastResult.load());
         std::shared_ptr<DdDevice> device;{std::lock_guard<std::mutex> lock(g_ddLastMx);device=g_ddLastDevice.lock();}auto mode=DdSnapshot(device);d.bol("mode_active",mode.active);d.num("width",mode.width);d.num("height",mode.height);d.num("bpp",mode.bpp);d.num("refresh",mode.refresh);o.raw("ddraw",d.done());}
        {
            std::lock_guard<std::mutex> lk(g_vkMx);
            JOut v; v.bol("hooked", o_vkQueuePresentKHR != nullptr); v.bol("enabled", g_vkEnabled.load());
            v.num("devices", (double)g_vkDevs.size()); v.num("queues", (double)g_vkQueues.size()); v.num("swapchains", (double)g_vkScs.size());
            v.num("usage_retries",g_vkUsageRetries.load());v.num("create_errors_propagated",g_vkCreateErrorsPropagated.load());v.num("create_first_result",g_vkCreateFirstResult.load());v.num("create_final_result",g_vkCreateFinalResult.load());
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
    CN(rid); CN(rid_magic); CN(rib); CN(rib_injected); CN(reg_raw); CN(reg_raw_ok); CN(reg_raw_failed); CN(reg_raw_removed); CN(raw_post_failed); CN(raw_target_rejected); CN(raw_queue_dropped); CN(raw_real_denied); CN(di_kbd); CN(di_mouse); CN(di_data_kbd); CN(di_data_mouse); CN(di_acq); CN(di_fake); CN(xinput);
    CN(wm_key); CN(wm_char); CN(wm_mouse); CN(wm_input); CN(wm_input_dropped); CN(wm_mouse_dropped); CN(act_swallowed); CN(act_synth); CN(act_leaked); CN(act_refresh_posted); CN(act_refresh_delivered); CN(act_refresh_cancelled); CN(act_refresh_failed); CN(act_cbt_blocked); CN(act_cbt_installed); CN(act_cbt_failed); CN(act_cbt_unhook_failed); CN(min_blocked); CN(min_restored); CN(create_visible_deferred); CN(present); CN(present_ours); CN(captures);
    CN(posted); CN(raw_posted); CN(raw_queued); CN(post_target_rejected); CN(last_post_target_error);
#undef CN
    c.num("fs_refused", (double)(g_fsRefused.load() + g_fsFailed.load()));   // public alias of fullscreen.refused + fullscreen.failures
    o.raw("counters", c.done());
    { std::lock_guard<std::mutex> lk(g_hookStrMx); o.str("hooks_ok", g_hooksOk); o.str("hooks_failed", g_hooksFail); }
    o.num("uptime_ms", GetTickCount() - g_t0);
    ClaimFields(o);
    return o.done();
}

std::string DoCapture(const JObj& j) {
    std::string path = JStr(j, "path");
    bool hashOnly = JBool(j, "hash_only", false);
    if (path.empty() && !hashOnly) return ErrJ("capture needs path");
    const bool preserveMap=JBool(j,"preserve_capture_space",false);
    const bool outputOptions=JHas(j,"scale")||JHas(j,"max_width")||JHas(j,"format")||JHas(j,"quality");
    if(hashOnly&&outputOptions)return ErrJ("frame-hash does not accept image output options");
    if(JHas(j,"scale")&&JHas(j,"max_width"))return ErrJ("use scale or max-width, not both");
    auto number=[&](const char* name,double fallback,double& value){auto item=j.find(name);if(item==j.end()){value=fallback;return true;}if(item->second.t!=2||!std::isfinite(item->second.n))return false;value=item->second.n;return true;};
    double scale=1,maxWidthValue=0,qualityValue=85;
    if(!number("scale",1,scale)||scale<0.05||scale>1)return ErrJ("capture scale must be a finite number 0.05..1");
    if(!number("max_width",0,maxWidthValue)||(JHas(j,"max_width")&&(maxWidthValue<1||maxWidthValue>8192||maxWidthValue!=std::floor(maxWidthValue))))return ErrJ("max-width must be a whole number 1..8192");
    std::string format=JStr(j,"format","png");
    if(JHas(j,"format")&&j.find("format")->second.t!=1)return ErrJ("capture format must be the string png or jpeg");
    if(format!="png"&&format!="jpeg")return ErrJ("capture format must be png or jpeg");
    if(JHas(j,"quality")&&format!="jpeg")return ErrJ("quality applies only to JPEG captures");
    if(!number("quality",85,qualityValue)||qualityValue<1||qualityValue>100||qualityValue!=std::floor(qualityValue))return ErrJ("JPEG quality must be a whole number 1..100");
    g_capturePixelLimit=(outputOptions||preserveMap)?16u*1024u*1024u:0;
    std::string method = JStr(j, "method", "auto");
    if (method == "native") method = "swapchain";
    if (method != "auto" && method != "swapchain" && method != "printwindow") return ErrJ("unknown capture method");
    if(JHas(j,"overlay")&&j.at("overlay").t!=2)return ErrJ("overlay must be an exact nonnegative integer HWND");
    double overlayValue=JNum(j,"overlay",0);
    if(!std::isfinite(overlayValue)||overlayValue<0||overlayValue!=std::floor(overlayValue)||overlayValue>9007199254740991.0||overlayValue>(double)(std::numeric_limits<ULONG_PTR>::max)())return ErrJ("overlay must be an exact nonnegative integer HWND");
    g_overlayHint=(HWND)(ULONG_PTR)(unsigned long long)overlayValue;
    int timeout = (int)JNum(j, "timeout_ms", 3000);
    double requestedFrames = JNum(j, "after_frames", 0);
    if (timeout < 1 || timeout > 120000 || requestedFrames < 0 || requestedFrames > 3600 || requestedFrames != std::floor(requestedFrames)) return ErrJ("capture timeout/after-frames out of range");
    if(method!="printwindow"&&(FullscreenBlockedBeforePresent()||DdBlockedBeforePresent()||D9BlockedBeforePresent())){JOut blocked;blocked.bol("ok",false);blocked.str("error","fullscreen negotiation blocked before the first target Present; inspect status.fullscreen or use config fullscreen=borderless (native exclusive remains disabled)");blocked.str("reason","fullscreen_blocked_before_first_present");return blocked.done();}
    long frameBefore = C.present_ours;
    DWORD frameStart = GetTickCount();
    while (C.present_ours - frameBefore < requestedFrames && GetTickCount() - frameStart < (DWORD)timeout) Sleep(2);
    if (C.present_ours - frameBefore < requestedFrames) return ErrJ("not enough target Present calls before capture timeout");
    std::string used, err, path_ = "printwindow";
    bool got = false;
    DWORD capStart = GetTickCount();
    auto native = [&]() -> bool {   // one request to whichever native frame hook fires next for our window
        ResetEvent(g_capEvent);
        g_capReq = 1;
        bool fired = WaitForSingleObject(g_capEvent, timeout) == WAIT_OBJECT_0;
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
    // Installing the DXGI hook proves nothing about this window's renderer: the
    // dummy device installs it even in GDI/D3D9 processes. Skip a doomed 3 s wait
    // for auto captures until this target has actually produced a native frame.
    // Explicit native requests retain their wait/error behavior, as do after-frames.
    if (method == "swapchain" || (method == "auto" && C.present_ours > 0)) {
        if (o_Present || o_wglSwapBuffers || o_vkQueuePresentKHR || g_ddHooked) {
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
        if (CaptureViaPrintWindow(e2)) { got = true; used = "printwindow"; path_ = g_capViaOverlay?"printwindow_overlay":"printwindow"; } else err += (err.empty() ? "" : "; ") + e2;
    }
    if (!got) return ErrJ(err);
    if (used=="swapchain" && g_capApi==API_DDRAW && DdGeometryMismatch()) return ErrJ("ddraw: client dimensions no longer match virtual display mode");
    RECT rc{}; if (g_hwnd) GetClientRect(g_hwnd, &rc);
    double mapOx=0,mapOy=0,mapCw=rc.right,mapCh=rc.bottom;
    g_captureOriginX = g_captureOriginY = 0; g_captureClientW = rc.right; g_captureClientH = rc.bottom;
    if (JHas(j, "region")) {
        int x=0, y=0, w=0, h=0, consumed=0;
        std::string region = JStr(j, "region");
        if (sscanf_s(region.c_str(), "%d,%d,%d,%d%n", &x, &y, &w, &h, &consumed) != 4 || consumed != (int)region.size() ||
            x < 0 || y < 0 || w <= 0 || h <= 0 || (long long)x+w > rc.right || (long long)y+h > rc.bottom || !rc.right || !rc.bottom)
            return ErrJ("region must be x,y,w,h in client pixels inside the window");
        int left = (int)((long long)x*g_capW/rc.right), top = (int)((long long)y*g_capH/rc.bottom);
        int right = (int)((long long)(x+w)*g_capW/rc.right), bottom = (int)((long long)(y+h)*g_capH/rc.bottom);
        mapOx=(double)left*rc.right/g_capW;mapOy=(double)top*rc.bottom/g_capH;
        mapCw=(double)(right-left)*rc.right/g_capW;mapCh=(double)(bottom-top)*rc.bottom/g_capH;
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
    UINT outputW=g_capW,outputH=g_capH;
    if(outputOptions){int w=0,h=0;if(!vi_image::OutputSize((int)g_capW,(int)g_capH,scale,(int)maxWidthValue,w,h,e3))return ErrJ(e3);outputW=w;outputH=h;}
    if(!hashOnly){
        if(outputOptions){
            vi_vision::Image output{(int)g_capW,(int)g_capH,std::move(g_capPix)};
            if(outputW!=g_capW||outputH!=g_capH){vi_vision::Image resized;if(!vi_image::Resize(output,outputW,outputH,resized,e3))return ErrJ(e3);output=std::move(resized);}
            if(!vi_image::Save(wp,output,format,(int)qualityValue,e3))return ErrJ(e3);
        }else if(!SavePng(wp,g_capPix,g_capW,g_capH,e3))return ErrJ(e3);
        if(!preserveMap)g_savedCaptureMap={true,outputW,outputH,mapOx,mapOy,mapCw,mapCh};
    }
    g_lastCaptureApi = used == "printwindow" ? API_PRINTWINDOW : g_capApi.load();
    C.captures++;
    JOut o; o.bol("ok", true); o.str("path", path); o.str("method", used); o.str("capture_path", path_); o.num("w", outputW); o.num("h", outputH);
    o.num("source_w",g_capW);o.num("source_h",g_capH);o.str("format",format);o.str("hash_scope","source_region_bgra");o.bol("capture_space_updated",!hashOnly&&!preserveMap);
    if(format=="jpeg")o.num("quality",qualityValue);
    o.num("frame", used == "printwindow" ? (double)C.present_ours.load() : (double)g_capturedPresent.load());
    o.str("hash", hashText); o.num("present_before", frameBefore); o.num("captured_present", g_capturedPresent);
    o.num("after_frames", requestedFrames); o.num("origin_x", mapOx); o.num("origin_y", mapOy);
    o.num("region_client_w", mapCw); o.num("region_client_h", mapCh);
    if (!err.empty()) o.str("note", err);
    o.num("client_w", rc.right); o.num("client_h", rc.bottom);
    return o.done();
}

void SleepMs(double ms) { if (ms > 0) Sleep((DWORD)ms); }

// coordinates: client pixels; "norm":true => fractions of the client size; "space":"capture" => pixels of the last capture image
bool ResolveXY(const JObj& j, LONG& x, LONG& y) {
    auto number=[&](const char* name,double fallback,double& value){auto it=j.find(name);if(it==j.end()){value=fallback;return true;}if(it->second.t==2){value=it->second.n;return std::isfinite(value);}if(it->second.t==1){const auto& text=it->second.s;char* end=nullptr;value=strtod(text.c_str(),&end);return end&&end!=text.c_str()&&end==text.c_str()+text.size()&&std::isfinite(value);}return false;};
    double fx=0,fy=0;if(!number("x",g_vcurX,fx)||!number("y",g_vcurY,fy))return false;
    RECT rc{}; if (!g_hwnd||!GetClientRect(g_hwnd, &rc)||rc.right<1||rc.bottom<1)return false;
    if (JBool(j, "norm", false)) { fx *= rc.right; fy *= rc.bottom; }
    else if (JStr(j, "space") == "capture") {if(!g_savedCaptureMap.valid)return false;fx = g_savedCaptureMap.ox + fx * g_savedCaptureMap.cw / g_savedCaptureMap.w; fy = g_savedCaptureMap.oy + fy * g_savedCaptureMap.ch / g_savedCaptureMap.h; }
    if(!std::isfinite(fx)||!std::isfinite(fy)||fx<-2147483648.0||fx>2147483647.0||fy<-2147483648.0||fy>2147483647.0)return false;
    x=(LONG)std::llround(fx);y=(LONG)std::llround(fy);return true;
}

std::string Exec(const std::string& line) {
    JObj j;
    if (!ParseJson(line, j)) return ErrJ("bad json");
    std::string cmd = JStr(j, "cmd");
    if(line.size()>65536&&(cmd=="key"||cmd=="hold"||cmd=="pulse"||cmd=="pad"||cmd=="pad-ramp"||cmd=="pad-timeline"))return ErrJ("input job request exceeds64KiB; no input was injected");
    if((JHas(j,"async")||JHas(j,"replace"))&&cmd!="placement-drag"&&cmd!="key"&&cmd!="hold"&&cmd!="pulse"&&cmd!="pad"&&cmd!="pad-ramp"&&cmd!="pad-timeline"&&cmd!="up"&&cmd!="mouseup"&&cmd!="release")return ErrJ("async/replace only apply to timed input jobs; no input was injected");
    { std::string refused = ClaimGate(cmd, j); if (!refused.empty()) return refused; }
    if (g_acProduct.load() && cmd != "ping" && cmd != "status" && cmd != "log") { EnforceAntiCheat(); return AntiCheatRefusal(); }
    if(cmd=="jobs"||cmd=="job-status"||cmd=="job-cancel")return JobCommand(cmd,j);
    if((cmd=="mouse"||cmd=="click"||cmd=="mousedown")&&(JHas(j,"x")||JHas(j,"y"))&&JStr(j,"space")=="capture"&&!g_savedCaptureMap.valid)return ErrJ("capture-space requires a successful saved single-frame capture; bursts/hash-only do not set it");
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
        if(JHas(j,"edge_margin")){double n=JNum(j,"edge_margin",-1);if(!(n>=0&&n<=4096&&n==std::floor(n))||j.find("edge_margin")->second.t==3)return ErrJ("edge_margin must be integer0..4096 client pixels; no configuration changed");}
        if(JHas(j,"hwnd")){DWORD windowOwner=0;HWND requested=(HWND)(ULONG_PTR)strtoull(JStr(j,"hwnd").c_str(),nullptr,0);if(!IsWindow(requested)||!GetWindowThreadProcessId(requested,&windowOwner)||windowOwner!=g_pid)return ErrJ("hwnd must belong to this target process; no configuration changed");}
#ifndef _WIN64
        if(JHas(j,"raw")&&JStr(j,"raw")=="buffer")return ErrJ("raw=buffer is unsupported for 32-bit/x86 synthetic records; use raw=auto or raw=post");
#endif

        if (JHas(j, "autoactivate")) g_cfg.autoactivate = JBool(j, "autoactivate", true);
        if (JHas(j, "fakefocus")) g_cfg.fakefocus = JBool(j, "fakefocus", true);
        if (JHas(j, "isolate")) g_cfg.isolate = JBool(j, "isolate", true);
        if (JHas(j, "blockfocus")) g_cfg.blockfocus = JBool(j, "blockfocus", true);
        if (JHas(j, "fullscreen")) { std::string policy=JStr(j,"fullscreen");if(policy!="borderless"&&policy!="refuse")return ErrJ("fullscreen must be borderless or refuse (nofullscreen=false explicitly allows native passthrough)");g_fullscreenPolicy=policy=="refuse"?1:0; }
        if (JHas(j, "nofullscreen")) g_cfg.nofullscreen = JBool(j, "nofullscreen", true);
        if (JHas(j, "keepvisible")) g_cfg.keepvisible = JBool(j, "keepvisible", true);
        if (JHas(j, "vk")) g_vkEnabled = JBool(j, "vk", true);   // vk=off: Vulkan hooks pass through (no usage change, no capture)
        if (JHas(j, "test_realfg")) { if (!TestHooksEnabled()) return ErrJ("test_realfg needs VINPUT_TEST_HOOKS=1 in the target's environment; no configuration changed"); std::string t = JStr(j, "test_realfg"); g_testRealFg = t == "1" ? 1 : t == "0" ? 0 : -1; }
        if (JHas(j, "cursor")) { std::string c = JStr(j, "cursor"); g_cfg.cursor = c == "virtual" ? 1 : c == "real" ? 2 : 0; }
        if (JHas(j,"edge_margin"))g_cfg.edgeMargin=(int)JNum(j,"edge_margin",8);
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
    if (cmd == "foreground-lease") {
        double ms=JNum(j,"ms",30000);
        if (!(ms>=0 && ms<=300000))return ErrJ("foreground lease must be 0..300000 ms; hold the shared LIVE lock for real foreground use");
        g_fgLeaseUntil=GetTickCount64()+(ULONGLONG)ms;
        Log("foreground lease granted for %g ms",ms);
        JOut o;o.bol("ok",true);o.num("lease_ms",ms);return o.done();
    }
    if (cmd == "restore-foreground") {
        // Hand-back ends intentional foreground access even when another window has
        // already become foreground. Otherwise a released LIVE lock leaves a live lease.
        g_fgLeaseUntil=0;
        // Only relinquishes a real foreground already owned by this process.
        // It cannot grant foreground to a background game or generate system input.
        HWND previous = (HWND)(ULONG_PTR)strtoull(JStr(j, "hwnd").c_str(), nullptr, 0);
        if (ForegroundBelongsToTarget(previous)) return ErrJ("restore needs a previous window outside the target");
        if (!ComputeRealFg()) { JOut o;o.bol("ok",true);o.bol("foreground_restored",true);return o.done(); }
        if (!IsWindow(previous)) return ErrJ("restore needs a previous window outside the target");
        bool restored = RelinquishForeground(previous);
        JOut o; o.bol("ok", restored); o.bol("foreground_restored", restored); return o.done();
    }
    if (cmd == "release") { ReleaseAll(); return OkJ(); }
    if (cmd == "restore") { if (!g_hwnd) return ErrJ("no window"); ShowWindowAsync(g_hwnd, SW_SHOWNOACTIVATE); Sleep(300); return StatusJson(); }
    if(cmd=="placement-drag")return SchedulePlacement(j);
    if(cmd=="pulse"||cmd=="key"||cmd=="hold")return ScheduleKeyInput(j,cmd=="pulse");
    if (cmd == "down" || cmd == "up") {
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
        double stepsValue=JNum(j,"steps",1);if(!(stepsValue>=1&&stepsValue<=10000&&stepsValue==std::floor(stepsValue)))return ErrJ("steps must be integer1..10000");int steps=(int)stepsValue;
        double stepms = JNum(j, "step_ms", 10);
        if (!(stepms >= 0 && stepms <= 1000) || stepms*(steps-1) > 120000) return ErrJ("mouse timing exceeds bounds");
        bool requestedPacing = JBool(j, "frame_paced", false);
        bool framePaced = requestedPacing && C.present_ours > 0;
        double frameTimeout = JNum(j, "frame_timeout_ms", 1000);
        if (!(frameTimeout >= 1 && frameTimeout <= 5000)) return ErrJ("frame timeout must be 1..5000 ms");
        DWORD motionStart = GetTickCount();
        auto afterStep = [&](int step) {
            long frame = C.present_ours;
            DWORD waitStart = GetTickCount();
            if (step < steps) SleepMs(requestedPacing && !framePaced ? (std::max)(20.0, stepms) : stepms);
            // Two Present boundaries guarantee a complete sampling interval after
            // the cursor update, including a race with a Present already in flight.
            if (framePaced) {
                while (C.present_ours - frame < 2 && GetTickCount() - waitStart < (DWORD)frameTimeout && GetTickCount() - motionStart < 120000) Sleep(1);
                return C.present_ours - frame >= 2;
            }
            return true;
        };
        bool unbounded = JBool(j, "raw", false);
        if (unbounded && cmd != "move") return ErrJ("--raw is a relative move mode");
        long long appliedX=0, appliedY=0;
        LONG rawX=0, rawY=0;
        PointerPath path;
        MotionAccounting accounting;long long requestedX=0,requestedY=0;unsigned published=0;
        auto motionError=[&](const char* error){JOut out;out.bol("ok",false);out.str("error",error);out.num("x",g_vcurX);out.num("y",g_vcurY);out.num("applied_dx",(double)appliedX);out.num("applied_dy",(double)appliedY);out.num("raw_dx",rawX);out.num("raw_dy",rawY);AccountingFields(out,accounting,requestedX,requestedY,published);EdgeFields(out,&path);return out.done();};
        if (JHas(j, "dx") || JHas(j, "dy")) {
            double dxValue=JNum(j,"dx",0), dyValue=JNum(j,"dy",0);
            if (!(dxValue>=-1000000 && dxValue<=1000000 && dyValue>=-1000000 && dyValue<=1000000&&dxValue==std::floor(dxValue)&&dyValue==std::floor(dyValue))||(JHas(j,"dx")&&j.at("dx").t!=2)||(JHas(j,"dy")&&j.at("dy").t!=2))return ErrJ("relative delta must be integer within +/-1000000; no movement was injected");
            LONG dx=(LONG)dxValue, dy=(LONG)dyValue; LONG sx=0, sy=0; rawX=dx; rawY=dy;
            requestedX=dx;requestedY=dy;
            LONG ox=g_vcurX,oy=g_vcurY;
            LONG destX=(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,(long long)ox+dx)),destY=(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,(long long)oy+dy));
            if(!path.Init(destX,destY,unbounded))return ErrJ("pointer target is unavailable");
            if(unbounded)g_cfg.cursor=1;
            if(!unbounded&&g_cfg.edgeMargin>0)g_cfg.cursor=1;
            for (int s = 1; s <= steps; s++) {
                if(!path.Valid())return motionError("pointer target changed or resized during move");
                LONG tx=(LONG)((long long)dx*s/steps), ty=(LONG)((long long)dy*s/steps);
                POINT point{(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,(long long)ox+tx)),(LONG)(std::max)(-2147483648LL,(std::min)(2147483647LL,(long long)oy+ty))};
                AppliedMouse a=MouseMoveTo(0,0,true,tx-sx,ty-sy,unbounded,&path,unbounded?nullptr:&point); appliedX+=a.dx; appliedY+=a.dy;
                accounting.Add(a.accounting);++published;
                sx=tx; sy=ty;
                if (!afterStep(s)) return motionError("frame-paced mouse move: target stopped presenting before the next step; button release is required");
            }
        } else {
            LONG x, y;if(!ResolveXY(j,x,y))return ErrJ("coordinates must be numeric and resolve within signed32-bit client range; no movement was injected");LONG ox = g_vcurX, oy = g_vcurY;
            requestedX=(long long)x-ox;requestedY=(long long)y-oy;
            if(!path.Init(x,y))return ErrJ("pointer target is unavailable");
            if(g_cfg.edgeMargin>0)g_cfg.cursor=1;
            for (int s=1; s<=steps; s++) {
                if(!path.Valid())return motionError("pointer target changed or resized during move");
                AppliedMouse a=MouseMoveTo((LONG)(ox+((long long)x-ox)*s/steps),(LONG)(oy+((long long)y-oy)*s/steps),false,0,0,false,&path);
                appliedX+=a.dx; appliedY+=a.dy;
                accounting.Add(a.accounting);++published;
                if (!afterStep(s)) return motionError("frame-paced mouse move: target stopped presenting before the next step; button release is required");
            }
        }
        JOut o; o.bol("ok", true); o.num("x", g_vcurX); o.num("y", g_vcurY);
        o.num("applied_dx", (double)appliedX); o.num("applied_dy", (double)appliedY);
        o.num("raw_dx", rawX); o.num("raw_dy", rawY); o.bol("unbounded", unbounded);
        o.str("cursor", UseVirtualCursor() ? "virtual" : "real");
        EdgeFields(o,&path);
        AccountingFields(o,accounting,requestedX,requestedY,published);
        o.str("pacing", framePaced ? "frames" : requestedPacing ? "time_fallback" : "time");
        o.num("frames_per_step", framePaced ? 2 : 0); return o.done();
    }
    if (cmd == "click" || cmd == "mousedown" || cmd == "mouseup") {
        std::string b = JStr(j, "button", "left");
        if (b != "left" && b != "right" && b != "middle" && b != "x1" && b != "x2") return ErrJ("unknown mouse button: " + b);
        int vk = b == "right" ? VK_RBUTTON : b == "middle" ? VK_MBUTTON : b == "x1" ? VK_XBUTTON1 : b == "x2" ? VK_XBUTTON2 : VK_LBUTTON;
        const int releaseIndex=vk==VK_RBUTTON?1:vk==VK_MBUTTON?2:vk==VK_XBUTTON1?3:vk==VK_XBUTTON2?4:0;
        struct MouseUpCleanup{int key,index;unsigned long long generation;bool active;void Run(){if(!active)return;std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_diMouseObservation[index].press==generation&&g_vk[key])MouseButton(key,false);active=false;}~MouseUpCleanup(){Run();}} upCleanup{vk,releaseIndex,g_diMouseObservation[releaseIndex].press.load(),cmd=="mouseup"};
        if(!g_hwnd)return ErrJ("no window");
        int previousCursor = g_cfg.cursor;
        // Keep virtual mode after the command: applications may poll GetCursorPos on a later frame.
        // A scoped override would report success then race the game's input consumer.
        g_cfg.cursor = 1;
        long hoverFrames = 0, hoverRequested = 0; bool hoverUnconfirmed = false, queueConfirmed = false; HWND preparedWindow = g_hwnd; WNDPROC qualifiedOuter = nullptr;
        const bool positioned=JHas(j,"x") || JHas(j,"y");
        if(cmd=="mouseup"&&positioned&&JStr(j,"space")=="capture"&&!g_savedCaptureMap.valid){upCleanup.Run();JOut out;out.bol("ok",false);out.str("error","capture-space requires a successful saved single-frame capture; mouse button cleanup was attempted without moving the cursor");out.num("button_down_count",0);out.bol("button_released",!g_vk[vk]);return out.done();}
        DWORD hoverStart=0,hoverWaitMs=0,hoverIdleBudget=0,hoverAbsoluteBudget=0;bool hoverAdaptive=false,hoverWaitFinished=false;
        const char* hoverWaitReason="not_requested";
        auto hoverFields=[&](JOut& o) {
            o.num("hover_frames",hoverFrames);o.num("hover_frames_requested",hoverRequested);o.num("hover_wait_ms",hoverWaitMs);
            o.num("hover_idle_budget_ms",hoverIdleBudget);o.num("hover_absolute_budget_ms",hoverAbsoluteBudget);o.bol("hover_adaptive",hoverAdaptive);o.str("hover_wait_reason",hoverWaitReason);
        };
        auto hoverError=[&](const char* message,const char* reason) {
            if(!hoverWaitFinished && positioned && hoverAbsoluteBudget)hoverWaitMs=GetTickCount()-hoverStart;
            upCleanup.Run();hoverWaitReason=reason;JOut o;o.bol("ok",false);o.str("error",message);o.num("button_down_count",0);if(cmd=="mouseup")o.bol("button_released",!g_vk[vk]);hoverFields(o);return o.done();
        };
        double requestedX=JNum(j,"x",g_vcurX),requestedY=JNum(j,"y",g_vcurY);
        POINT resolved{g_vcurX,g_vcurY},expected=resolved,applied=resolved,lastPress=resolved;
        int presses=0;
        if (positioned) {
            double timeoutValue = JNum(j, "hover_timeout_ms", 1500), framesValue = JNum(j, "hover_frames", 3), settleValue = JNum(j, "settle_ms", -1);
            if (!(timeoutValue >= 1 && timeoutValue <= 120000 && timeoutValue == std::floor(timeoutValue)) ||
                !(framesValue >= 0 && framesValue <= 3600 && framesValue == std::floor(framesValue)) ||
                !((!JHas(j, "settle_ms") && settleValue == -1) || (settleValue >= 0 && settleValue <= 60000))) return hoverError("hover timeout/frames/settle timing out of range","invalid_hover_timing");
            // Explicit timeouts keep their original total-cap semantics. Only the
            // omitted default adapts to real Present progress, within a nominal 5s cap.
            hoverAdaptive=!JHas(j,"hover_timeout_ms");hoverRequested=(long)framesValue;
            hoverIdleBudget=hoverAdaptive?1500:0;hoverAbsoluteBudget=hoverAdaptive?5000:(DWORD)timeoutValue;
            hoverStart=GetTickCount();
            LONG x, y;if(!ResolveXY(j,x,y))return hoverError("coordinates must be numeric and resolve within signed32-bit client range; no button was pressed","invalid_coordinates");
            resolved={x,y};ClampToClient(x,y);expected={x,y};
            long delivered = g_mouseMovesDelivered;
            HWND window = g_hwnd;
            WNDPROC outer = CurrentWindowProcedure(window);
            const bool external = outer && outer != h_WndProc;
            AppliedMouse hover = MouseMoveTo(x, y, false, 0, 0);
            DWORD timeout = (DWORD)timeoutValue;
            if (hover.message_attempted) {
                if (!hover.message_accepted) return hoverError("virtual hover PostMessage failed; no button was pressed","hover_post_failed");
                ULONG_PTR ticket = 0;
                if (external && g_hoverBarrierMessage) {
                    ticket = ++g_hoverBarrierSequence; if (!ticket) ticket = ++g_hoverBarrierSequence;
                    g_hoverBarrierExpected = ticket;
                    if (!PostOurs(window, g_hoverBarrierMessage, ticket, (LPARAM)g_hoverBarrierCookie)) return hoverError("virtual hover queue barrier post failed; no button was pressed","barrier_post_failed");
                }
                while (g_mouseMovesDelivered == delivered && (!ticket || g_hoverBarrierAck != ticket) && GetTickCount() - hoverStart < timeout) Sleep(2);
                if (g_mouseMovesDelivered == delivered) {
                    if (!ticket || g_hoverBarrierAck != ticket || g_hwnd != window || CurrentWindowProcedure(window) != outer)
                        return hoverError("virtual hover not dispatched and no responsive outer WndProc queue barrier before timeout; no button was pressed","queue_timeout");
                    hoverUnconfirmed = true; queueConfirmed = true; qualifiedOuter = outer;
                }
            }
            long frame = C.present_ours, requested = (long)framesValue;
            if (frame > 0 && requested > 0) {
                long lastFrame=frame;DWORD lastProgress=GetTickCount();
                for(;;) {
                    DWORD now=GetTickCount();long current=C.present_ours;
                    if(current>lastFrame){lastProgress=now;lastFrame=current;}
                    hoverFrames=current-frame;
                    if(now-hoverStart>=hoverAbsoluteBudget)
                        return hoverError("virtual hover not rendered before timeout (absolute budget); --hover-frames 0 skips only the render wait (it does not confirm application acceptance)","render_absolute_timeout");
                    if(hoverFrames>=requested){hoverWaitReason="frames_ready";break;}
                    if(hoverAdaptive && now-lastProgress>=hoverIdleBudget)
                        return hoverError("virtual hover not rendered before timeout (no Present progress); --hover-frames 0 skips only the render wait (it does not confirm application acceptance)","render_idle_timeout");
                    Sleep(2);
                }
            } else hoverWaitReason=requested==0?"render_wait_skipped":"no_native_present";
            hoverWaitMs=GetTickCount()-hoverStart;hoverWaitFinished=true;
            SleepMs(settleValue >= 0 ? settleValue : frame > 0 ? 0 : 100);
        }
        if (cmd != "mouseup" && (g_hwnd != preparedWindow || !IsWindow(preparedWindow))) return hoverError("target window changed during hover; no button was pressed","target_changed");
        if (cmd != "mouseup" && hoverUnconfirmed && CurrentWindowProcedure(preparedWindow) != qualifiedOuter) return hoverError("outer WndProc changed during hover preparation; no button was pressed","outer_changed");
        const double countValue=JNum(j,"count",1),ms=cmd=="click"?JNum(j,"ms",60):0,gap=JNum(j,"gap",80),pollCapValue=JNum(j,"di_poll_timeout_ms",1000);
        if(cmd=="click" && (!(ms>=0&&ms<=120000)||!(gap>=0&&gap<=60000)||!(countValue>=1&&countValue<=1000&&countValue==std::floor(countValue))))return hoverError("click duration/count/gap out of range; no button was pressed","invalid_click_timing");
        if(JHas(j,"di_poll_timeout_ms") && (cmd!="click" || j.find("di_poll_timeout_ms")->second.t!=2 || !(pollCapValue>=0&&pollCapValue<=5000&&pollCapValue==std::floor(pollCapValue)))) {
            if(cmd=="mouseup"){MouseButton(vk,false);JOut o;o.bol("ok",false);o.str("error","button released; di_poll_timeout_ms applies only to timed click");o.bol("button_released",!g_vk[vk]);o.num("button_down_count",0);return o.done();}
            return hoverError("di_poll_timeout_ms must be integer0..5000 for timed click only; no button was pressed","invalid_poll_timing");
        }
        const int count=cmd=="click"?(int)countValue:1,button=vk==VK_RBUTTON?1:vk==VK_MBUTTON?2:vk==VK_XBUTTON1?3:vk==VK_XBUTTON2?4:0;
        const bool pollExplicit=JHas(j,"di_poll_timeout_ms");const DWORD pollCap=(DWORD)pollCapValue;
        const double releaseCapValue=JNum(j,"di_release_timeout_ms",pollCapValue);
        if(JHas(j,"di_release_timeout_ms")&&(cmd!="click"||j.find("di_release_timeout_ms")->second.t!=2||!(releaseCapValue>=0&&releaseCapValue<=5000&&releaseCapValue==std::floor(releaseCapValue))))return hoverError("di_release_timeout_ms must be integer0..5000 for timed click only; mouse-up cleanup remains enabled","invalid_release_poll_timing");
        const DWORD releaseCap=(DWORD)releaseCapValue;const bool releaseExplicit=JHas(j,"di_release_timeout_ms"),releaseRequired=(releaseExplicit&&releaseCap>0)||(pollExplicit&&pollCap>0);
        bool pollTimedOut=false,lastPollConfirmed=false,lastPollWaited=false;int pollsWaited=0,pollsConfirmed=0;
        bool releaseTimedOut=false,lastReleaseConfirmed=false,releaseWaited=false;int releasesConfirmed=0;unsigned long long releaseGeneration=0;double releaseWaitMs=0;const char* releaseChannel="none";
        unsigned long long pressGeneration=0;ULONGLONG consumerAge=0;bool consumerSeen=false;double lastHoldMs=0;
        const char* pollChannel="none";
        auto pollFields=[&](JOut& o){o.str("di_poll_mode",cmd!="click"?"not_timed":pollExplicit?(pollCap?"required":"bypass"):"auto");
            o.bol("di_poll_waited",pollsWaited>0);o.bol("di_poll_confirmed",presses>0&&pollsConfirmed==presses);o.bol("di_poll_timed_out",pollTimedOut);
            o.num("di_poll_presses_waited",pollsWaited);o.num("di_poll_presses_confirmed",pollsConfirmed);o.num("di_poll_timeout_ms",pollCap);o.num("di_poll_press_generation",(double)pressGeneration);
            o.str("di_poll_channel",pollChannel);o.bol("di_poll_consumer_recent",consumerSeen);if(consumerSeen)o.num("di_poll_consumer_age_ms",(double)consumerAge);else o.raw("di_poll_consumer_age_ms","null");
            o.num("button_ms_requested",ms);o.num("button_hold_ms",lastHoldMs);o.bol("button_released",!g_vk[vk]);
            o.bol("di_release_waited",releaseWaited);o.bol("di_release_confirmed",presses>0&&releasesConfirmed==presses);o.bol("di_release_timed_out",releaseTimedOut);o.num("di_release_releases_confirmed",releasesConfirmed);o.num("di_release_generation",(double)releaseGeneration);o.num("di_release_timeout_ms",releaseCap);o.num("di_release_wait_ms",releaseWaitMs);o.str("di_release_channel",releaseChannel);
            o.bol("di_press_release_confirmed",presses>0&&pollsConfirmed==presses&&releasesConfirmed==presses);
            if(releaseTimedOut)o.str("di_release_warning","the virtual button is up but its current DirectInput release read was not confirmed; application acceptance remains unknown; no input was retried");
            if(pollTimedOut)o.str("di_poll_warning","the requested press was injected and released but its DirectInput mouse down read was not confirmed; application acceptance remains unknown; no input was retried");};
        auto coordinates=[&](JOut& o) {
            EdgeFields(o);
            if(positioned) { o.num("requested_x",requestedX);o.num("requested_y",requestedY);o.num("resolved_x",resolved.x);o.num("resolved_y",resolved.y);o.num("clamped_x",expected.x);o.num("clamped_y",expected.y); }
            o.num("applied_x",applied.x);o.num("applied_y",applied.y);o.num("button_down_count",presses);
            if(presses){o.num("last_press_x",lastPress.x);o.num("last_press_y",lastPress.y);}
            o.num("current_x",g_vcurX);o.num("current_y",g_vcurY);
        };
        std::string pressFailure="virtual cursor changed from the requested client point before button-down; no further press was sent; pause the game's camera/menu recentering and inspect the frame before trying again";
        auto press=[&]() {
            if(g_hwnd!=preparedWindow || !IsWindow(preparedWindow)) { applied={g_vcurX,g_vcurY};pressFailure="target window changed before button-down; no further press was sent";return false; }
            if(hoverUnconfirmed && CurrentWindowProcedure(preparedWindow)!=qualifiedOuter) { applied={g_vcurX,g_vcurY};pressFailure="outer WndProc changed before button-down; no further press was sent";return false; }
            if(!MouseButtonChecked(vk,true,positioned?&expected:nullptr,&applied))return false;
            ++presses;lastPress=applied;return true;
        };
        auto driftError=[&]() { JOut o;o.bol("ok",false);o.str("error",pressFailure);coordinates(o);hoverFields(o);pollFields(o);return o.done(); };
        if (cmd == "mousedown") { if(!press())return driftError(); }
        else if (cmd == "mouseup") {MouseButton(vk,false);upCleanup.active=false;}
        else for (int r = 0; r < count; r++) {
            auto& observation=g_diMouseObservation[button];const ULONGLONG seenAt=(std::max)(observation.state_poll_at.load(),g_diMouseBufferPollAt.load()),now=GetTickCount64();
            consumerAge=seenAt&&now>=seenAt?now-seenAt:0;consumerSeen=seenAt&&consumerAge<=2000;
            lastPollWaited=pollCap && (pollExplicit||consumerSeen);lastPollConfirmed=false;pollChannel="none";
            if(lastPollWaited)++pollsWaited;
            bool timedOut=false,clockFailed=false,superseded=false,releaseOwned=false;
            {
                struct Release {int vk,index;unsigned long long generation=0,token=0;bool active=false,owned=false;void Run(){if(!active)return;std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& observation=g_diMouseObservation[index];if(observation.press==generation&&g_vk[vk]){MouseButton(vk,false);token=observation.release;owned=true;}active=false;}~Release(){Run();}} release{vk,button};
                LARGE_INTEGER frequency{},start{},sample{};
                if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0){release.active=false;pressFailure="real hold clock unavailable; no button was pressed";return driftError();}
                if(!press()){release.active=false;return driftError();}
                pressGeneration=observation.press;
                release.generation=pressGeneration;release.active=true;
                if(!QueryPerformanceCounter(&start))clockFailed=true;
                // Current APIs are real/unscaled. A future time hook must route these
                // housekeeping reads to its original trampoline, not a virtual clock.
                while(!clockFailed){if(!QueryPerformanceCounter(&sample)){clockFailed=true;break;}lastHoldMs=1000.0*(double)(sample.QuadPart-start.QuadPart)/(double)frequency.QuadPart;
                    if(observation.press!=pressGeneration||!g_vk[vk]){superseded=true;break;}
                    const bool state=observation.state_down==pressGeneration,buffer=observation.buffer_down==pressGeneration;
                    lastPollConfirmed=state||buffer;pollChannel=state?"state":buffer?"buffer":"none";
                    if(lastHoldMs>=ms && (!lastPollWaited||lastPollConfirmed))break;
                    if(lastHoldMs>=ms+pollCap){timedOut=true;break;}Sleep(1);
                }
                release.Run();releaseGeneration=release.token;releaseOwned=release.owned;
            } // Every timed press releases, including exceptions and required-observation failure.
            if(lastPollConfirmed)++pollsConfirmed;
            pollTimedOut=pollTimedOut||timedOut;
            lastReleaseConfirmed=false;releaseChannel="none";releaseWaitMs=0;
            const bool waitUp=releaseOwned&&releaseCap&&(lastPollWaited||releaseExplicit);releaseWaited=releaseWaited||waitUp;
            if(waitUp){const auto releaseStart=HousekeepingNow();for(;;){releaseWaitMs=(double)(HousekeepingNow()-releaseStart);
                if(observation.press!=pressGeneration||observation.release!=releaseGeneration||g_vk[vk]){superseded=true;break;}
                const bool state=observation.state_up==releaseGeneration,buffer=observation.buffer_up==releaseGeneration;
                lastReleaseConfirmed=state||buffer;releaseChannel=state?"state":buffer?"buffer":"none";
                if(lastReleaseConfirmed)break;if(releaseWaitMs>=releaseCap){releaseTimedOut=true;break;}Sleep(1);
            }}
            if(lastReleaseConfirmed)++releasesConfirmed;
            if(clockFailed||superseded||(timedOut&&pollExplicit)||(releaseTimedOut&&releaseRequired)){JOut o;o.bol("ok",false);o.str("error",clockFailed?"real hold clock failed; the press was released, not retried":superseded?"mouse press/release was superseded; newer input was not released or retried":timedOut?"DirectInput mouse down was not observed before the extra wait cap; the press was released, not retried":"DirectInput mouse release was not observed before its wait cap; the virtual button remains up, not retried");coordinates(o);hoverFields(o);pollFields(o);return o.done();}
            if(r+1<count)SleepMs(gap);
        }
        JOut o; o.bol("ok", true); o.num("x",presses?lastPress.x:g_vcurX.load()); o.num("y",presses?lastPress.y:g_vcurY.load());coordinates(o);
        o.str("cursor", "virtual"); o.num("previous_cursor_mode", previousCursor);hoverFields(o);
        pollFields(o);
        o.bol("hover_unconfirmed", hoverUnconfirmed); o.bol("queue_progress_confirmed", queueConfirmed);
        if (hoverUnconfirmed) o.str("delivery_warning", "an outer WndProc consumed or bypassed hover dispatch; its posted queue barrier progressed, but hover and application acceptance are unconfirmed; only requested presses were injected, without retry");
        o.str("delivery", "injected; application response is not implied"); return o.done();
    }
    if (cmd == "wheel") { MouseWheel((int)JNum(j, "delta", 120), JBool(j, "horizontal", false)); return OkJ(); }
    if(cmd=="pad-ramp"||cmd=="pad-timeline")return SchedulePadSteering(j,cmd=="pad-timeline");
    if (cmd == "pad") {
        if (JHas(j, "hold") && j.find("hold")->second.t != 3) return ErrJ("pad hold must be a JSON boolean true or false; numeric durations belong in ms");
        const double ms = JNum(j, "ms", 100);
        const bool hold = JBool(j, "hold", false);
        if(hold&&(JHas(j,"async")||JHas(j,"replace")))return ErrJ("indefinite pad hold cannot use async/replace; use a bounded duration");
        if (hold && JHas(j, "ms")) return ErrJ("pad hold is indefinite and cannot include ms; omit hold for a timed command");
        if (!(ms >= 0 && ms <= 120000)) return ErrJ("pad duration must be 0..120000 ms");
        std::string names = JStr(j, "buttons");
        std::string lower = names; for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
        if (lower == "off") {   // stop overriding: pad 0 is the real pad again (or "not connected" when there is none)
            std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);JobsOverridePad();std::lock_guard<std::mutex> stateLock(g_padStateMx);g_padActive = false; g_padButtons = 0; g_padSetMask = 0; g_padPacket++;
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
        if(!hold)return SchedulePadInput(j,b,mask,v,ms,clamped);
        {std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);JobsOverridePad();JobPadPublish(b,mask,v);}
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
            g_waitJob.reset();JObj peek;const bool parsed=ParseJson(line,peek);const std::string peekCmd=JStr(peek,"cmd");bool open=parsed&&(IsOpenCommand(peekCmd)||peekCmd=="job-cancel"||peekCmd=="release"||peekCmd=="up"||(peekCmd=="pad"&&JStr(peek,"buttons")=="off"));
            if (open) { try { resp = Exec(line); } catch (...) { resp = ErrJ("exception"); } }
            else { std::lock_guard<std::mutex> lk(execMx); try { resp = Exec(line); } catch (...) { resp = ErrJ("exception"); } }
            if(g_waitJob){auto pending=g_waitJob;g_waitJob.reset();WaitForSingleObject(pending->completion,INFINITE);resp=JobReply(pending);}
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
    // Normal targets admit only SYSTEM and their process token's user. AppContainer
    // targets additionally need their package groups and low label for the dual access check.
    // Never create a pipe with a fallback/default descriptor after a security setup failure.
    HANDLE token = nullptr; DWORD needed = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { Log("pipe security: OpenProcessToken failed %lu; no pipe created", GetLastError()); return ERROR_ACCESS_DENIED; }
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed < sizeof(TOKEN_USER) || needed > 65536) {
        Log("pipe security: TokenUser size failed %lu; no pipe created", GetLastError()); CloseHandle(token); return ERROR_ACCESS_DENIED;
    }
    std::vector<BYTE> user(needed);
    if (!GetTokenInformation(token, TokenUser, user.data(), needed, &needed)) {
        Log("pipe security: TokenUser failed %lu; no pipe created", GetLastError()); CloseHandle(token); return ERROR_ACCESS_DENIED;
    }
    CloseHandle(token);
    PSID sid = ((TOKEN_USER*)user.data())->User.Sid; LPWSTR owner = nullptr;
    if (!IsValidSid(sid) || !ConvertSidToStringSidW(sid, &owner)) {
        Log("pipe security: owner SID failed %lu; no pipe created", GetLastError()); return ERROR_ACCESS_DENIED;
    }
    std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;" + std::wstring(owner) + L")"; LocalFree(owner);
    if (g_appContainer) sddl += L"(A;;GA;;;AC)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr) || !sd) {
        Log("pipe security: descriptor failed %lu; no pipe created", GetLastError()); return ERROR_ACCESS_DENIED;
    }
    SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};
    Log("pipe %s", W2U(name).c_str());
    for (;;) {
        HANDLE h = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, &sa);
        if (h == INVALID_HANDLE_VALUE) { Log("CreateNamedPipe failed %lu", GetLastError()); Sleep(1000); continue; }
        BOOL c = ConnectNamedPipe(h, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!c) { CloseHandle(h); continue; }
        HANDLE t = CreateThread(nullptr, 0, ClientThread, h, 0, nullptr);
        if (t) CloseHandle(t); else CloseHandle(h);
    }
}

DWORD WINAPI MonitorThread(LPVOID) {
    bool lastFg = ComputeRealFg();
    DWORD lastSlow = 0;
    HWND lastFgWnd = RealForeground();
    for (;;) {
        EnforceAntiCheat();   // a module that loaded after start: stop held input and running jobs without waiting for a command
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
            TryHookDInput(); TryHookDInputLegacy(); TryHookXInput(); TryHookEcl(); TryHookGL(); TryHookVk(); TryHookDDraw(); TryHookD9();
            if (!g_hwndForced) {
                HWND best = FindMainWindow();
                if (!g_hwnd || !IsWindow(g_hwnd)) { if (best) Retarget(best); }
                else if (best && best != g_hwnd && now - g_t0 < 180000) Retarget(best);
            }
        }
        Sleep(15);
    }
}

// OS activation (e.g. a Steam dialog closing, an unhooked runtime path or a new modal window)
// can bypass every target-side API detour. A separate thread keeps this policy responsive even
// while the hook-discovery monitor, a capture, or the game's UI thread is blocked.
DWORD WINAPI ForegroundGuardThread(LPVOID) {
    MSG queue; PeekMessageW(&queue,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    g_userIntent.InstallEvents();
    struct Destination { HWND hwnd; DWORD pid; };
    std::vector<Destination> history;
    bool wasOurs=false;
    ULONGLONG lastAttempt=0;
    for (;;) {
        g_userIntent.SetTarget(g_hwnd,g_pid);g_userIntent.Pump();
        HWND fg=RealForeground(); DWORD pid=0; if(fg)GetWindowThreadProcessId(fg,&pid);
        bool oursActual=ForegroundBelongsToTarget(fg);
        bool ours=TestRealFgOverride(oursActual);
        g_realFg=ours;
        bool human=g_userIntent.Refresh(fg);
        // Deliberate owner input (a click, key or wheel the game received while it was really foreground) is a human who
        // activated the window themselves. Hold until the game loses the real foreground; drop a stale activation click
        // that never produced a foreground gain.
        if(g_ownerHolding) {
            if(wasOurs && !ours) g_ownerHolding=false;
            else if(!ours && GetTickCount64()-g_ownerHoldAt.load()>1500) g_ownerHolding=false;
        }
        const bool ownerHolds=g_ownerHolding.load();
        if(fg && pid && !oursActual) {
            if(history.empty() || history.back().hwnd!=fg) {
                history.erase(std::remove_if(history.begin(),history.end(),[&](const Destination& d){return d.hwnd==fg;}),history.end());
                history.push_back({fg,pid});if(history.size()>32)history.erase(history.begin());
            }
            g_fgRestoreWindow=fg;g_fgGuardArmed=true;
        }
        if(human)g_fgGuardArmed=false;
        if(ours && !wasOurs) { ++g_fgGained; Log("REAL foreground GAINED (continuous guard, hwnd=%p)",fg); }
        ULONGLONG now=GetTickCount64();
        {
            static thread_local bool stoodDown=false;
            const bool wouldHandBack=ours && ownerHolds && !human && g_cfg.blockfocus && g_fgGuardArmed && !ForegroundLeased();
            if(wouldHandBack && !stoodDown){++g_ownerStanddowns;Log("foreground guard stands down: the owner is using the game window (real input received)");}
            stoodDown=wouldHandBack;
        }
        if(ours && !human && !ownerHolds && !g_userIntent.EvaluatingAltTab() && g_cfg.blockfocus && g_fgGuardArmed && !ForegroundLeased() && now-lastAttempt>=100) {
            lastAttempt=now; HWND dest=nullptr;
            while(!history.empty()) {
                const auto& d=history.back();DWORD owner=0;GetWindowThreadProcessId(d.hwnd,&owner);
                if(IsWindow(d.hwnd) && IsWindowVisible(d.hwnd) && owner==d.pid && owner!=g_pid && !ForegroundBelongsToTarget(d.hwnd)) { dest=d.hwnd;break; }
                history.pop_back();
            }
            if(dest && RealForeground()==fg) {
                g_fgRestoreWindow=dest;
                if(RelinquishForeground(dest)) { g_realFg=false;++g_fgRestored;Log("foreground guard handed back to %p",dest); }
                else { ++g_fgRestoreFailed;Log("foreground guard hand-back FAILED to %p",dest); }
            }
        }
        wasOurs=ours;
        Sleep(10);
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
    else if (BaseNameIs(data->BaseDllName, L"d3d9.dll")) { g_ldrNotified++; TryHookD9((HMODULE)data->DllBase); }
    else if (BaseNameIs(data->BaseDllName, L"ddraw.dll")) { g_ldrNotified++; TryHookDDraw((HMODULE)data->DllBase); }
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
    g_hoverBarrierMessage = RegisterWindowMessageW(L"vinput.mouse.queue_barrier.v1");
    g_hoverBarrierCookie = (ULONG_PTR)&g_hoverBarrierCookie ^ (ULONG_PTR)GetTickCount64() ^ (ULONG_PTR)GetCurrentProcessId();
    g_capEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MH_STATUS s = VIMH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { Log("MinHook init failed %s", VIMH_StatusToString(s)); }
    RegisterDllNotification();
    InstallUser32Hooks();
    g_userIntent.Configure({o_GetAsyncKeyState,o_GetCursorPos,o_WindowFromPoint,o_GetForegroundWindow});
    // GL / Vulkan before DXGI: their drivers may present through an internal DXGI swapchain, which is only recognised
    // as foreign once the GL/VK hook is in. Vulkan before the pipe exists: a target that waits for the pipe and then
    // creates its VkDevice is covered.
    TryHookGL();
    TryHookDDraw(); TryHookD9();
    TryHookVk();
    InstallDxgiHooks();
    TryHookDInput(); TryHookDInputLegacy(); TryHookXInput();
    FindRawDevices();
    g_realFg = ComputeRealFg();
    HANDLE t = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
    for (int i = 0; i < 600 && !g_hwnd; i++) { HWND h = FindMainWindow(); if (h) { Retarget(h); break; } Sleep(200); }
    Log("hooks ok: %s", g_hooksOk.c_str());
    if (!g_hooksFail.empty()) Log("hooks FAILED: %s", g_hooksFail.c_str());
    t = CreateThread(nullptr, 0, ForegroundGuardThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
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
