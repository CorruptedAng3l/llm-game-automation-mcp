// Cached API pointers in a late-loaded wrapper still enter the guarded export.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void* api[5]{};
extern "C" __declspec(dllexport) BOOL WINAPI ViCursorLateProbe(){
 for(auto pointer:api)if(!pointer)return FALSE;
 ((BOOL(WINAPI*)(int,int))api[0])(43,47);((BOOL(WINAPI*)(int,int))api[1])(53,59);RECT clip{19,23,219,223};((BOOL(WINAPI*)(const RECT*))api[2])(&clip);((BOOL(WINAPI*)(const RECT*))api[2])(nullptr);
 INPUT input{};input.type=INPUT_MOUSE;input.mi.dx=3;input.mi.dwFlags=MOUSEEVENTF_MOVE;((UINT(WINAPI*)(UINT,LPINPUT,int))api[3])(1,&input,sizeof(input));((void(WINAPI*)(DWORD,DWORD,DWORD,DWORD,ULONG_PTR))api[4])(MOUSEEVENTF_MOVE,7,11,0,0);return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);const auto user=GetModuleHandleW(L"user32.dll");const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event"};for(int i=0;i<5;i++)api[i]=user?(void*)GetProcAddress(user,names[i]):nullptr;}return TRUE;}
