// Test-only physical boundary. No write spy ever calls its native trampoline.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "vi_mh_rename.h"
#include "MinHook.h"
namespace vi_cursor_spy {
enum ForegroundMode {NativeForeground=0,FakeUpstreamOnly=1,ModeledOwner=2,ModeledExternal=3};
inline std::atomic<unsigned long long> calls[5]{};
inline std::atomic<int> foregroundMode{0};inline std::atomic<HWND> target{nullptr};
inline constexpr int EntryCount=16;inline void* entries[EntryCount]{};inline void* expected[EntryCount]{};inline void* originals[EntryCount]{};inline bool installed=false;
inline std::atomic<unsigned long long> foregroundEffects{0},queueEffects{0},captureEffects{0},permissionEffects{0};
inline unsigned char absorbingPatch[5][16]{};inline std::atomic<bool> clipQueryMatched{false};
inline BOOL WINAPI SetPosition(int,int){++calls[0];return TRUE;}
inline BOOL WINAPI SetPhysicalPosition(int,int){++calls[1];return TRUE;}
inline BOOL WINAPI Clip(const RECT*){++calls[2];return TRUE;}
inline UINT WINAPI Send(UINT count,LPINPUT,int){++calls[3];return count;}
inline void WINAPI Mouse(DWORD,DWORD,DWORD,DWORD,ULONG_PTR){++calls[4];}
inline BOOL WINAPI ForegroundWrite(HWND){++foregroundEffects;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline HWND WINAPI FocusWrite(HWND){++foregroundEffects;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
inline void WINAPI SwitchWrite(HWND,BOOL){++foregroundEffects;SetLastError(ERROR_ACCESS_DENIED);}
inline BOOL WINAPI AttachWrite(DWORD,DWORD,BOOL){++queueEffects;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline HWND WINAPI CaptureWrite(HWND){++captureEffects;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
inline BOOL WINAPI ReleaseWrite(){++captureEffects;return TRUE;}
inline BOOL WINAPI PermissionWrite(DWORD){++permissionEffects;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline HWND WINAPI Foreground(){const int mode=foregroundMode.load();if(mode==FakeUpstreamOnly||mode==ModeledOwner)return target.load();if(mode==ModeledExternal)return GetDesktopWindow();return ((HWND(WINAPI*)())originals[5])();}
inline BOOL WINAPI Gui(DWORD thread,PGUITHREADINFO output){
 const int mode=foregroundMode.load();if(mode==ModeledOwner||mode==ModeledExternal){if(!output||output->cbSize!=sizeof(*output)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO data{sizeof(data)};data.hwndActive=mode==ModeledOwner?target.load():GetDesktopWindow();*output=data;return TRUE;}
 return ((BOOL(WINAPI*)(DWORD,PGUITHREADINFO))originals[6])(thread,output);
}
inline bool Read(const void* address,void* data,SIZE_T size){SIZE_T actual=0;return address&&ReadProcessMemory(GetCurrentProcess(),address,data,size,&actual)&&actual==size;}
inline bool Reaches(void* entry,void* detour){
 auto next=(std::uintptr_t)entry;for(int count=0;count<6&&next;count++){
  if(next==(std::uintptr_t)detour)return true;unsigned char bytes[6]{};if(!Read((void*)next,bytes,sizeof(bytes)))return false;
  if(bytes[0]==0xE9){std::int32_t offset=0;memcpy(&offset,bytes+1,4);next=(std::uintptr_t)((std::intptr_t)next+5+offset);continue;}
  if(bytes[0]==0xEB){next=(std::uintptr_t)((std::intptr_t)next+2+(std::int8_t)bytes[1]);continue;}
  if(bytes[0]==0xFF&&bytes[1]==0x25){std::int32_t offset=0;memcpy(&offset,bytes+2,4);
#ifdef _WIN64
   const auto slot=(std::uintptr_t)((std::intptr_t)next+6+offset);
#else
   const auto slot=(std::uintptr_t)(std::uint32_t)offset;
#endif
   if(!Read((void*)slot,&next,sizeof(next)))return false;continue;}
  return false;
 }return false;
}
inline bool Install(){
 const MH_STATUS init=VIMH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","GetForegroundWindow","GetGUIThreadInfo","SetForegroundWindow","BringWindowToTop","SetActiveWindow","SetFocus","SwitchToThisWindow","AttachThreadInput","SetCapture","ReleaseCapture","AllowSetForegroundWindow"};
 void* detours[]={(void*)&SetPosition,(void*)&SetPhysicalPosition,(void*)&Clip,(void*)&Send,(void*)&Mouse,(void*)&Foreground,(void*)&Gui,(void*)&ForegroundWrite,(void*)&ForegroundWrite,(void*)&FocusWrite,(void*)&FocusWrite,(void*)&SwitchWrite,(void*)&AttachWrite,(void*)&CaptureWrite,(void*)&ReleaseWrite,(void*)&PermissionWrite};
 const auto module=GetModuleHandleW(L"user32.dll");if(!module)return false;
 for(int i=0;i<EntryCount;i++){
  entries[i]=(void*)GetProcAddress(module,names[i]);if(!entries[i])return false;
  int prior=-1;for(int j=0;j<i;j++)if(entries[j]==entries[i])prior=j;
  if(prior>=0){if(!((i==1&&prior==0)||(detours[i]==detours[prior])))return false;expected[i]=expected[prior];originals[i]=originals[prior];continue;}
  expected[i]=detours[i];MH_STATUS state=VIMH_CreateHook(entries[i],expected[i],&originals[i]);if(state!=MH_OK||!originals[i])return false;
  state=VIMH_EnableHook(entries[i]);if(state!=MH_OK||!Reaches(entries[i],expected[i]))return false;
 }
 for(int i=0;i<5;i++)if(!Read((unsigned char*)entries[i]-5,absorbingPatch[i],sizeof(absorbingPatch[i])))return false;
 installed=true;return true;
}
inline bool VerifyBeforeProduct(){if(!installed)return false;for(int i=0;i<EntryCount;i++)if(!Reaches(entries[i],expected[i]))return false;return true;}
inline void SetForegroundMode(ForegroundMode mode,HWND window){target=window;foregroundMode=mode;}
inline void Reset(){for(auto& count:calls)count=0;}
inline unsigned long long Total(){unsigned long long sum=0;for(auto& count:calls)sum+=count.load();return sum;}
inline bool ReplaceProductEntryWithAbsorber(unsigned index){
 if(!installed||index>=5)return false;auto address=(unsigned char*)entries[index]-5;DWORD protection=0;if(!VirtualProtect(address,sizeof(absorbingPatch[index]),PAGE_EXECUTE_READWRITE,&protection))return false;
 memcpy(address,absorbingPatch[index],sizeof(absorbingPatch[index]));FlushInstructionCache(GetCurrentProcess(),address,sizeof(absorbingPatch[index]));DWORD ignored=0;const bool restored=VirtualProtect(address,sizeof(absorbingPatch[index]),protection,&ignored)!=FALSE;
 return restored&&Reaches(entries[index],expected[index]);
}
// These are documented API entries, always reached only after Install+verification.
inline bool Probe(){if(!installed)return false;
 auto position=(BOOL(WINAPI*)(int,int))entries[0],physical=(BOOL(WINAPI*)(int,int))entries[1];auto clip=(BOOL(WINAPI*)(const RECT*))entries[2];auto send=(UINT(WINAPI*)(UINT,LPINPUT,int))entries[3];auto mouse=(void(WINAPI*)(DWORD,DWORD,DWORD,DWORD,ULONG_PTR))entries[4];
 position(23,29);physical(31,37);RECT rect{11,17,211,217};clip(&rect);RECT observed{};clipQueryMatched=GetClipCursor(&observed)&&EqualRect(&rect,&observed);clip(nullptr);
 INPUT input[2]{};input[0].type=INPUT_MOUSE;input[0].mi.dx=7;input[0].mi.dy=-3;input[0].mi.dwFlags=MOUSEEVENTF_MOVE;input[1].type=INPUT_KEYBOARD;input[1].ki.wVk=VK_F8;
 send(2,input,sizeof(INPUT));mouse(MOUSEEVENTF_MOVE,5,9,0,0);return true;
}
}
