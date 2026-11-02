// vi_acguard.h - anti-cheat refusal (defence in depth, NOT a guarantee).
//
// vinput is for single-player / offline games. Before it injects anything it refuses - with a clear error and NO
// override flag - when any of these is true:
//   1. the target process has a module loaded whose name belongs to a known anti-cheat (EasyAntiCheat, BattlEye, ...);
//   2. the target's executable folder (or up to 3 parent folders, never a drive root) contains a known anti-cheat
//      component (e.g. an "EasyAntiCheat" or "BattlEye" folder, start_protected_game.exe) - this catches a protected
//      game BEFORE its anti-cheat has loaded, e.g. `launch --early`;
//   3. a known anti-cheat process is running, or a known anti-cheat service / kernel driver is active, anywhere on
//      the machine (kernel anti-cheats watch the whole system, not one game).
// The injected DLL repeats check 1 at start-up and keeps watching: if an anti-cheat module is loaded into the game
// after injection, every input/capture command is refused from then on.
//
// The lists below are best-effort and name-based. A renamed, new or unlisted anti-cheat is NOT detected. Never rely
// on this check to decide whether a game is safe to automate: read DISCLAIMER.md.
#pragma once
#include <windows.h>
#include <wchar.h>

namespace vi_ac {

struct Entry { const char* product; const wchar_t* name; bool prefix; };

// Module base names inside the target (case-insensitive; prefix entries match the start of the name).
inline const Entry* ModuleList(size_t& n) {
    static const Entry k[] = {
        {"EasyAntiCheat", L"easyanticheat", true},          // EasyAntiCheat_x64.dll, EasyAntiCheat_EOS*.dll
        {"EasyAntiCheat", L"eac_", true},
        {"BattlEye", L"beclient", true},                    // BEClient_x64.dll, BEClient.dll
        {"Vanguard", L"vgk", true},
        {"Vanguard", L"vgc", true},
        {"Ricochet", L"randgrid", true},
        {"Ricochet", L"ricochet", true},
        {"FACEIT", L"faceit", true},
        {"ESEA", L"esea", true},
        {"nProtect GameGuard", L"npgg", true},              // npggNT.des, npggnt64.des
        {"nProtect GameGuard", L"gameguard", true},
        {"nProtect GameGuard", L"gamemon", true},
        {"XIGNCODE3", L"x3.xem", false},
        {"XIGNCODE3", L"xcorona", true},                    // xcorona.xem, xcorona_x64.xem
        {"XIGNCODE3", L"xigncode", true},
        {"XIGNCODE3", L"xhunter", true},
        {"ACE (AntiCheatExpert)", L"anticheatexpert", true},
        {"ACE (AntiCheatExpert)", L"ace-", true},           // ACE-Base*.dll, ACE-ATS*.dll ...
        {"ACE (AntiCheatExpert)", L"sguard", true},
        {"PunkBuster", L"pbcl", true},
        {"PunkBuster", L"pbag", true},
        {"PunkBuster", L"pbsv", true},
        {"PunkBuster", L"pnkbstr", true},
        {"mhyprot", L"mhyprot", true},
        {"mhyprot", L"mhypbase", true},
        {"Denuvo Anti-Cheat", L"denuvo-anticheat", true},
        {"Denuvo Anti-Cheat", L"denuvo-anti-cheat", true},
        {"Denuvo Anti-Cheat", L"denuvo_anticheat", true},
    };
    n = sizeof(k) / sizeof(k[0]); return k;
}

// Process image names (exact, case-insensitive) that mean an anti-cheat is running on this machine.
inline const Entry* ProcessList(size_t& n) {
    static const Entry k[] = {
        {"EasyAntiCheat", L"EasyAntiCheat.exe", false}, {"EasyAntiCheat", L"EasyAntiCheat_EOS.exe", false},
        {"EasyAntiCheat", L"EasyAntiCheat_Setup.exe", false}, {"EasyAntiCheat", L"EasyAntiCheat_EOS_Setup.exe", false},
        {"EasyAntiCheat", L"start_protected_game.exe", false},
        {"BattlEye", L"BEService.exe", false}, {"BattlEye", L"BEService_x64.exe", false}, {"BattlEye", L"BELauncher.exe", false},
        {"Vanguard", L"vgc.exe", false}, {"Vanguard", L"vgtray.exe", false},
        {"FACEIT", L"FACEIT.exe", false}, {"FACEIT", L"FACEITService.exe", false}, {"FACEIT", L"faceitclient.exe", false},
        {"ESEA", L"esportsea.exe", false}, {"ESEA", L"ESEADriver2.exe", false},
        {"nProtect GameGuard", L"GameMon.des", false}, {"nProtect GameGuard", L"GameMon64.des", false},
        {"nProtect GameGuard", L"GameGuard.des", false}, {"nProtect GameGuard", L"npggsvc.exe", false},
        {"XIGNCODE3", L"xxd-0.xem", false},
        {"ACE (AntiCheatExpert)", L"SGuard64.exe", false}, {"ACE (AntiCheatExpert)", L"SGuardSvc64.exe", false},
        {"ACE (AntiCheatExpert)", L"ACE-Tray.exe", false}, {"ACE (AntiCheatExpert)", L"AntiCheatExpert.exe", false},
        {"PunkBuster", L"PnkBstrA.exe", false}, {"PunkBuster", L"PnkBstrB.exe", false},
        {"Denuvo Anti-Cheat", L"denuvo-anticheat-update-service.exe", false},
        {"Denuvo Anti-Cheat", L"denuvo-anti-cheat-update-service.exe", false},
    };
    n = sizeof(k) / sizeof(k[0]); return k;
}

// Service / kernel-driver names or display names (exact, case-insensitive) that count when ACTIVE.
inline const Entry* ServiceList(size_t& n) {
    static const Entry k[] = {
        {"EasyAntiCheat", L"EasyAntiCheat", false}, {"EasyAntiCheat", L"EasyAntiCheat_EOS", false},
        {"EasyAntiCheat", L"EasyAntiCheatSys", false},
        {"BattlEye", L"BEService", false}, {"BattlEye", L"BEDaisy", false},
        {"Vanguard", L"vgc", false}, {"Vanguard", L"vgk", false},
        {"Ricochet", L"atvi-randgrid_sr", false}, {"Ricochet", L"atvi-randgrid_mp", false}, {"Ricochet", L"randgrid", false},
        {"FACEIT", L"FACEIT", false}, {"FACEIT", L"FACEITService", false},
        {"ESEA", L"ESEADriver2", false}, {"ESEA", L"ESEADriver3", false},
        {"nProtect GameGuard", L"npggsvc", false}, {"nProtect GameGuard", L"GameGuard", false},
        {"XIGNCODE3", L"xhunter1", false}, {"XIGNCODE3", L"xhunter2", false},
        {"ACE (AntiCheatExpert)", L"ACE-BASE", false}, {"ACE (AntiCheatExpert)", L"ACE-GAME", false},
        {"ACE (AntiCheatExpert)", L"AntiCheatExpert Protection", false}, {"ACE (AntiCheatExpert)", L"AntiCheatExpert Service", false},
        {"ACE (AntiCheatExpert)", L"SGuardSvc", false},
        {"PunkBuster", L"PnkBstrA", false}, {"PunkBuster", L"PnkBstrB", false},
        {"mhyprot", L"mhyprot", false}, {"mhyprot", L"mhyprot2", false}, {"mhyprot", L"mhyprot3", false},
        {"Denuvo Anti-Cheat", L"Denuvo Anti-Cheat", false}, {"Denuvo Anti-Cheat", L"denuvo-anticheat", false},
        {"Denuvo Anti-Cheat", L"Denuvo Anti-Cheat Update Service", false},
    };
    n = sizeof(k) / sizeof(k[0]); return k;
}

// Entries in a game folder that mark an anti-cheat protected install (exact names, case-insensitive).
inline const Entry* FolderMarkerList(size_t& n) {
    static const Entry k[] = {
        {"EasyAntiCheat", L"EasyAntiCheat", false}, {"EasyAntiCheat", L"start_protected_game.exe", false},
        {"EasyAntiCheat", L"EasyAntiCheat_EOS_Setup.exe", false}, {"EasyAntiCheat", L"EasyAntiCheat_Setup.exe", false},
        {"BattlEye", L"BattlEye", false}, {"BattlEye", L"BEService.exe", false}, {"BattlEye", L"BEService_x64.exe", false},
        {"nProtect GameGuard", L"GameGuard", false}, {"nProtect GameGuard", L"GameGuard.des", false},
        {"XIGNCODE3", L"XIGNCODE", false}, {"XIGNCODE3", L"x3.xem", false},
        {"PunkBuster", L"pb", false}, {"PunkBuster", L"pbsvc.exe", false},
        {"mhyprot", L"mhypbase.dll", false},
        {"ACE (AntiCheatExpert)", L"AntiCheatExpert", false},
        {"Denuvo Anti-Cheat", L"denuvo-anticheat", false},
    };
    n = sizeof(k) / sizeof(k[0]); return k;
}

inline const char* MatchIn(const Entry* list, size_t n, const wchar_t* name, size_t len) {
    if (!name) return nullptr;
    for (size_t i = 0; i < n; i++) {
        size_t wl = wcslen(list[i].name);
        if (list[i].prefix ? (len >= wl && !_wcsnicmp(name, list[i].name, wl)) : (len == wl && !_wcsnicmp(name, list[i].name, wl)))
            return list[i].product;
    }
    return nullptr;
}
inline const char* MatchModule(const wchar_t* baseName, size_t len) { size_t n; const Entry* k = ModuleList(n); return MatchIn(k, n, baseName, len); }
inline const char* MatchModule(const wchar_t* baseName) { return baseName ? MatchModule(baseName, wcslen(baseName)) : nullptr; }

}  // namespace vi_ac

// ------------------------------------------------------------------------------------------------------------------
// CLI-side scan (process / service / folder). Not compiled into the injected DLL.
// ------------------------------------------------------------------------------------------------------------------
#ifdef VI_ACGUARD_SCANNER
#include <tlhelp32.h>
#include <string>
#pragma comment(lib, "advapi32.lib")

namespace vi_ac {

struct Finding { bool refused = false; bool modulesChecked = false; std::string product, kind, name, error; };

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
inline bool Hit(Finding& f, const char* product, const char* kind, const std::wstring& name) {
    if (!product) return false;
    f.refused = true; f.product = product; f.kind = kind; f.name = Narrow(name); return true;
}

// 1. modules loaded in the target. suspendedOk: the caller created the process suspended, so an empty / partial
//    module list (loader not initialised yet) is expected; otherwise an unreadable module list is itself a refusal.
inline bool ScanModules(DWORD pid, bool suspendedOk, Finding& f) {
    HANDLE snap = INVALID_HANDLE_VALUE;
    DWORD err = 0;
    for (int i = 0; i < 20; i++) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap != INVALID_HANDLE_VALUE) break;
        err = GetLastError();
        if (err != ERROR_BAD_LENGTH && err != ERROR_PARTIAL_COPY) break;
        if (err == ERROR_PARTIAL_COPY && suspendedOk) break;
        Sleep(25);
    }
    if (snap == INVALID_HANDLE_VALUE) {
        if (suspendedOk) return false;   // folder + system checks still apply
        f.refused = true; f.kind = "unverifiable"; f.product = "unknown";
        f.error = "cannot list the target's modules (win32=" + std::to_string(err) + "); refusing because an anti-cheat cannot be ruled out";
        return true;
    }
    MODULEENTRY32W me = {sizeof(me)};
    bool any = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        any = true;
        if (Hit(f, MatchModule(me.szModule), "module", me.szModule)) { CloseHandle(snap); f.modulesChecked = true; return true; }
    }
    CloseHandle(snap);
    f.modulesChecked = any;
    return false;
}

// 2. anti-cheat components next to the game executable (folder and up to 3 parents, never a drive root).
inline bool ScanFolders(const std::wstring& imagePath, Finding& f) {
    if (imagePath.empty()) return false;
    wchar_t full[MAX_PATH * 2] = {0};
    if (!GetFullPathNameW(imagePath.c_str(), MAX_PATH * 2, full, nullptr)) return false;
    std::wstring dir = full;
    size_t n; const Entry* k = FolderMarkerList(n);
    for (int level = 0; level < 4; level++) {
        size_t sl = dir.find_last_of(L"\\/");
        if (sl == std::wstring::npos) return false;
        dir = dir.substr(0, sl);
        if (dir.size() <= 3 || dir.find_last_of(L"\\/") == std::wstring::npos) return false;   // "C:" or "C:\"
        for (size_t i = 0; i < n; i++) {
            std::wstring p = dir + L"\\" + k[i].name;
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return Hit(f, k[i].product, "game_folder", p);
        }
    }
    return false;
}

// 3. anti-cheat processes and active services / kernel drivers anywhere on the machine.
inline bool ScanSystem(Finding& f) {
    size_t n; const Entry* k = ProcessList(n);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {sizeof(pe)};
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (Hit(f, MatchIn(k, n, pe.szExeFile, wcslen(pe.szExeFile)), "process", pe.szExeFile)) { CloseHandle(snap); return true; }
        CloseHandle(snap);
    }
    k = ServiceList(n);
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return false;
    DWORD need = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &need, &count, &resume, nullptr);
    std::string buf(need + 4096, '\0'); resume = 0;
    bool found = false;
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE, (LPBYTE)&buf[0], (DWORD)buf.size(), &need, &count, &resume, nullptr)) {
        auto* s = (ENUM_SERVICE_STATUS_PROCESSW*)&buf[0];
        for (DWORD i = 0; i < count && !found; i++) {
            const wchar_t* sn = s[i].lpServiceName; const wchar_t* dn = s[i].lpDisplayName;
            found = Hit(f, MatchIn(k, n, sn, sn ? wcslen(sn) : 0), "service", sn ? sn : L"") ||
                    Hit(f, MatchIn(k, n, dn, dn ? wcslen(dn) : 0), "service", dn ? dn : L"");
        }
    }
    CloseServiceHandle(scm);
    return found;
}

inline std::wstring ImagePathOf(DWORD pid) {
    std::wstring path;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return path;
    wchar_t buf[MAX_PATH * 2]; DWORD len = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) path.assign(buf, len);
    CloseHandle(h); return path;
}

// Full check before injecting into a running (or freshly created suspended) process.
inline Finding CheckTarget(DWORD pid, bool suspendedOk) {
    Finding f;
    if (ScanSystem(f)) return f;
    if (ScanFolders(ImagePathOf(pid), f)) return f;
    ScanModules(pid, suspendedOk, f);
    return f;
}
// Check before starting an executable (launch --exe): machine-wide + folder only.
inline Finding CheckExe(const std::wstring& exe) {
    Finding f;
    if (ScanSystem(f)) return f;
    ScanFolders(exe, f);
    return f;
}

}  // namespace vi_ac
#endif
