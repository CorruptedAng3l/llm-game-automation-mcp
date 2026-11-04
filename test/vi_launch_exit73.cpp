#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Ordinary injection only, AFTER the guarded target created its witnessed window.
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)ExitProcess(73);return TRUE;}
