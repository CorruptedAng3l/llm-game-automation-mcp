// Optional test-only batch installation. Same exports/sinks as the legacy
// terminal guards, including keybd_event; no product code runs before Verify.
#pragma once
#include "vi_launch_terminal.h"
#include <vector>
namespace vi_launch_terminal_batch {
inline void* keyEntry=nullptr;inline unsigned createCalls=0,enableCalls=0,verifiedExports=0;
inline std::atomic<unsigned long long> keyCalls{0};
inline void WINAPI Key(BYTE,BYTE,DWORD,ULONG_PTR){++keyCalls;}
inline bool Verify(){return vi_launch_terminal::Verify()&&keyEntry&&vi_cursor_spy::Reaches(keyEntry,(void*)&Key);}
inline bool Install(bool batched=true){
 if(!batched){
  if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return false;
  keyEntry=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"keybd_event");void* original=nullptr;
  if(!keyEntry||VIMH_CreateHook(keyEntry,(void*)&Key,&original)!=MH_OK||!original||VIMH_EnableHook(keyEntry)!=MH_OK)return false;
  if(!Verify())return false;verifiedExports=21;return true;
 }
 const MH_STATUS initialized=VIMH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
 struct Pending{void* target;void* detour;void* original;};std::vector<Pending> pending;
 auto create=[&](void* target,void* detour,void*& original){if(!target)return false;for(const auto& p:pending)if(p.target==target){if(p.detour!=detour)return false;original=p.original;return true;}++createCalls;if(VIMH_CreateHook(target,detour,&original)!=MH_OK||!original)return false;pending.push_back({target,detour,original});return true;};
 const auto user=GetModuleHandleW(L"user32.dll");if(!user)return false;
 const char* cursorNames[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","GetForegroundWindow","GetGUIThreadInfo","SetForegroundWindow","BringWindowToTop","SetActiveWindow","SetFocus","SwitchToThisWindow","AttachThreadInput","SetCapture","ReleaseCapture","AllowSetForegroundWindow"};
 void* cursorDetours[]={(void*)&vi_cursor_spy::SetPosition,(void*)&vi_cursor_spy::SetPhysicalPosition,(void*)&vi_cursor_spy::Clip,(void*)&vi_cursor_spy::Send,(void*)&vi_cursor_spy::Mouse,(void*)&vi_cursor_spy::Foreground,(void*)&vi_cursor_spy::Gui,(void*)&vi_cursor_spy::ForegroundWrite,(void*)&vi_cursor_spy::ForegroundWrite,(void*)&vi_cursor_spy::FocusWrite,(void*)&vi_cursor_spy::FocusWrite,(void*)&vi_cursor_spy::SwitchWrite,(void*)&vi_cursor_spy::AttachWrite,(void*)&vi_cursor_spy::CaptureWrite,(void*)&vi_cursor_spy::ReleaseWrite,(void*)&vi_cursor_spy::PermissionWrite};
 for(int i=0;i<vi_cursor_spy::EntryCount;++i){
  auto& entry=vi_cursor_spy::entries[i];entry=(void*)GetProcAddress(user,cursorNames[i]);if(!entry)return false;
  int alias=-1;for(int j=0;j<i;++j)if(entry==vi_cursor_spy::entries[j])alias=j;
  if(alias>=0){if(!((i==1&&alias==0)||cursorDetours[i]==cursorDetours[alias]))return false;vi_cursor_spy::expected[i]=vi_cursor_spy::expected[alias];vi_cursor_spy::originals[i]=vi_cursor_spy::originals[alias];continue;}
  vi_cursor_spy::expected[i]=cursorDetours[i];if(!create(entry,cursorDetours[i],vi_cursor_spy::originals[i]))return false;
 }
 const char* terminalNames[]={"SetWindowPos","ShowWindow","ShowWindowAsync","LockSetForegroundWindow"};void* terminalDetours[]={(void*)&vi_launch_terminal::Position,(void*)&vi_launch_terminal::Show,(void*)&vi_launch_terminal::Show,(void*)&vi_launch_terminal::Lock};
 for(unsigned i=0;i<4;++i){auto& entry=vi_launch_terminal::entries[i];entry=(void*)GetProcAddress(user,terminalNames[i]);vi_launch_terminal::detours[i]=terminalDetours[i];for(auto cursor:vi_cursor_spy::entries)if(entry==cursor)return false;void* unused=nullptr;if(!create(entry,terminalDetours[i],unused))return false;}
 keyEntry=(void*)GetProcAddress(user,"keybd_event");for(const auto& p:pending)if(p.target==keyEntry)return false;void* unused=nullptr;if(!create(keyEntry,(void*)&Key,unused))return false;
 // Private fixture MinHook instance owns only the above disabled hooks.
 ++enableCalls;if(VIMH_EnableHook(MH_ALL_HOOKS)!=MH_OK)return false;
 for(int i=0;i<vi_cursor_spy::EntryCount;++i){if(!vi_cursor_spy::Reaches(vi_cursor_spy::entries[i],vi_cursor_spy::expected[i]))return false;++verifiedExports;}
 for(unsigned i=0;i<4;++i){if(!vi_cursor_spy::Reaches(vi_launch_terminal::entries[i],vi_launch_terminal::detours[i]))return false;++verifiedExports;}
 if(!vi_cursor_spy::Reaches(keyEntry,(void*)&Key))return false;++verifiedExports;
 for(int i=0;i<5;++i)if(!vi_cursor_spy::Read((unsigned char*)vi_cursor_spy::entries[i]-5,vi_cursor_spy::absorbingPatch[i],sizeof(vi_cursor_spy::absorbingPatch[i])))return false;
 vi_cursor_spy::installed=true;return verifiedExports==21&&Verify();
}
}
