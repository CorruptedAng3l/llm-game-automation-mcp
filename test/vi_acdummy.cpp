// vi_acdummy.cpp - a harmless, empty DLL. build.bat writes it as dist\acdummy\x64\EasyAntiCheat_x64.dll and
// dist\acdummy\x86\BEClient.dll: only the NAME looks like an anti-cheat module, so verify\run_actest.ps1 can prove
// that vinput refuses such a process. It contains no anti-cheat code and does nothing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" __declspec(dllexport) int vi_acdummy_marker(void) { return 42; }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
