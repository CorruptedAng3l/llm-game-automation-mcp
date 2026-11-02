// vi_aclaunch.cpp - starts a program inside an AppContainer (profile "vinput.capture.test", no capabilities),
// the same sandbox a UWP / Xbox app game runs in. Used by verify\run_nativecapture.ps1 to test vinput's
// AppContainer support (DLL/log ACLs, pipe in the container namespace) without a real UWP game.
//   vi_aclaunch.exe <exe> [args...]      -> {"ok":true,"pid":N,"appcontainer_sid":"S-1-15-2-..."}
// The exe and anything it must read/write need ALL APPLICATION PACKAGES (S-1-15-2-1) access (the script grants it).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <string>
#include <cstdio>
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { printf("{\"ok\":false,\"error\":\"usage: vi_aclaunch <exe> [args]\"}\n"); return 2; }
    const wchar_t* name = L"vinput.capture.test";
    PSID sid = nullptr;
    HRESULT hr = CreateAppContainerProfile(name, L"vinput capture test", L"vinput synthetic AppContainer target", nullptr, 0, &sid);
    if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) hr = DeriveAppContainerSidFromAppContainerName(name, &sid);
    if (FAILED(hr) || !sid) { printf("{\"ok\":false,\"error\":\"AppContainer profile hr=0x%08X\"}\n", (unsigned)hr); return 2; }
    SECURITY_CAPABILITIES caps = {}; caps.AppContainerSid = sid;
    SIZE_T sz = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
    std::string buf(sz, '\0');
    LPPROC_THREAD_ATTRIBUTE_LIST al = (LPPROC_THREAD_ATTRIBUTE_LIST)&buf[0];
    if (!InitializeProcThreadAttributeList(al, 1, 0, &sz) ||
        !UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps, sizeof(caps), nullptr, nullptr)) {
        printf("{\"ok\":false,\"error\":\"attribute list %lu\"}\n", GetLastError()); return 2;
    }
    std::wstring cmd = L"\"" + std::wstring(argv[1]) + L"\"";
    for (int i = 2; i < argc; i++) { cmd += L" \""; cmd += argv[i]; cmd += L"\""; }
    STARTUPINFOEXW si = {}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = al;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(argv[1], &cmd[0], nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi)) {
        printf("{\"ok\":false,\"error\":\"CreateProcess %lu\"}\n", GetLastError()); return 2;
    }
    LPWSTR ss = nullptr; ConvertSidToStringSidW(sid, &ss);
    printf("{\"ok\":true,\"pid\":%lu,\"appcontainer_sid\":\"%ls\"}\n", pi.dwProcessId, ss ? ss : L"");
    if (ss) LocalFree(ss);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    DeleteProcThreadAttributeList(al); FreeSid(sid);
    return 0;
}
