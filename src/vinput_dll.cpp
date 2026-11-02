// vinput_dll.cpp - standalone injectable vinput DLL (vinput64.dll / vinput32.dll).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "vinput.h"

extern "C" __declspec(dllexport) const char* vinput_version(void) { return VInput_Version(); }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HMODULE pinned = nullptr;  // hooks point into this module: never let it unload
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&DllMain, &pinned);
        VInput_Init();
    }
    return TRUE;
}
