// Capture-only derivative of tracked test/vi_char_spies.h (paired-safety commit).
// Ordinary MinHook is separate from the injected DLL's renamed MinHook instance.
// No Probe/entry-replacement helper is included. Default write spies are terminal;
// a typed SetWindowPos override may admit only proved own offscreen geometry.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include "MinHook.h"
namespace capture_spy {
inline std::atomic<unsigned long long> writes{0};
struct Hook { void* entry; void* detour; void* original; };
inline std::vector<Hook> hooks;
inline BOOL WINAPI Position(int, int) { ++writes; return TRUE; }
inline BOOL WINAPI Clip(const RECT*) { ++writes; return TRUE; }
inline UINT WINAPI Send(UINT count, LPINPUT, int) { ++writes; return count; }
inline void WINAPI Mouse(DWORD, DWORD, DWORD, DWORD, ULONG_PTR) { ++writes; }
inline BOOL WINAPI Foreground(HWND) { ++writes; return FALSE; }
inline HWND WINAPI Focus(HWND) { ++writes; return nullptr; }
inline void WINAPI Switch(HWND, BOOL) { ++writes; }
inline BOOL WINAPI Attach(DWORD, DWORD, BOOL) { ++writes; return FALSE; }
inline HWND WINAPI Capture(HWND) { ++writes; return nullptr; }
inline BOOL WINAPI Release() { ++writes; return TRUE; }
inline BOOL WINAPI Permission(DWORD) { ++writes; return FALSE; }
inline BOOL WINAPI LockPermission(UINT) { ++writes; return FALSE; }
inline BOOL WINAPI WindowPos(HWND, HWND, int, int, int, int, UINT) { ++writes; return TRUE; }
inline BOOL WINAPI Show(HWND, int) { ++writes; return TRUE; }
inline bool Read(const void* pointer, void* output, SIZE_T size) { SIZE_T got = 0; return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, output, size, &got) && got == size; }
inline bool Reaches(void* entry, void* detour) {
    auto next = reinterpret_cast<std::uintptr_t>(entry);
    for (int n = 0; n < 6 && next; ++n) {
        if (next == reinterpret_cast<std::uintptr_t>(detour)) return true;
        unsigned char bytes[6]{}; if (!Read(reinterpret_cast<void*>(next), bytes, sizeof(bytes))) return false;
        if (bytes[0] == 0xE9) { std::int32_t offset = 0; memcpy(&offset, bytes + 1, 4); next = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(next) + 5 + offset); continue; }
        if (bytes[0] == 0xEB) { next = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(next) + 2 + static_cast<std::int8_t>(bytes[1])); continue; }
        if (bytes[0] == 0xFF && bytes[1] == 0x25) {
            std::int32_t offset = 0; memcpy(&offset, bytes + 2, 4);
#ifdef _WIN64
            const auto slot = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(next) + 6 + offset);
#else
            const auto slot = static_cast<std::uintptr_t>(static_cast<std::uint32_t>(offset));
#endif
            if (!Read(reinterpret_cast<void*>(slot), &next, sizeof(next))) return false;
            continue;
        }
        return false;
    }
    return false;
}
inline bool Add(const char* name, void* detour) {
    void* const entry = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name));
    if (!entry) return false;
    for (const auto& prior : hooks) if (prior.entry == entry) return prior.detour == detour && Reaches(entry, detour);
    void* original = nullptr;
    if (MH_CreateHook(entry, detour, &original) != MH_OK || !original || MH_EnableHook(entry) != MH_OK || !Reaches(entry, detour)) return false;
    hooks.push_back({entry, detour, original}); return true;
}
using WindowPosition=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);
using WindowVisibility=BOOL(WINAPI*)(HWND,int);
// The optional fixture dispatcher must refuse all but proved own, nonactivating
// offscreen geometry. Optional visibility dispatchers must similarly prove an
// explicitly requested exact own HWND/UI-thread/lifetime, offscreen pre/post
// rect and SW_SHOWNA-only call under a no-activation CBT barrier. Defaults stay
// terminal, including ShowWindow and ShowWindowAsync.
inline bool Install(WindowPosition position=&WindowPos,WindowVisibility show=&Show,WindowVisibility showAsync=&Show) {
    if(!position||!show||!showAsync)return false;
    if (MH_Initialize() != MH_OK) return false;
    const char* names[] = {"SetCursorPos", "SetPhysicalCursorPos", "ClipCursor", "SendInput", "mouse_event", "SetForegroundWindow", "BringWindowToTop", "SetActiveWindow", "SetFocus", "SwitchToThisWindow", "AttachThreadInput", "SetCapture", "ReleaseCapture", "AllowSetForegroundWindow", "LockSetForegroundWindow", "SetWindowPos", "ShowWindow", "ShowWindowAsync"};
    void* detours[] = {reinterpret_cast<void*>(&Position), reinterpret_cast<void*>(&Position), reinterpret_cast<void*>(&Clip), reinterpret_cast<void*>(&Send), reinterpret_cast<void*>(&Mouse), reinterpret_cast<void*>(&Foreground), reinterpret_cast<void*>(&Foreground), reinterpret_cast<void*>(&Focus), reinterpret_cast<void*>(&Focus), reinterpret_cast<void*>(&Switch), reinterpret_cast<void*>(&Attach), reinterpret_cast<void*>(&Capture), reinterpret_cast<void*>(&Release), reinterpret_cast<void*>(&Permission), reinterpret_cast<void*>(&LockPermission), reinterpret_cast<void*>(position), reinterpret_cast<void*>(show), reinterpret_cast<void*>(showAsync)};
    for (unsigned n = 0; n < sizeof(names) / sizeof(names[0]); ++n) if (!Add(names[n], detours[n])) return false;
    return true;
}
} // Keep these hooks alive through all cleanup; never MH_Uninitialize.
