// Test-only boundary shared by guarded CLI and owned target. Never call Probe().
#pragma once
#include <windows.h>
#include <cstdio>
#include <atomic>
#include "vi_cursor_spies.h"
namespace vi_launch_terminal {
inline std::atomic<unsigned long long> placement{0},visibility{0},permission{0};
inline void* entries[4]{};inline void* detours[4]{};
inline BOOL WINAPI Position(HWND,HWND,int,int,int,int,UINT){++placement;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline BOOL WINAPI Show(HWND,int){++visibility;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline BOOL WINAPI Lock(UINT){++permission;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
inline bool Install(){
 if(!vi_cursor_spy::Install()||!vi_cursor_spy::VerifyBeforeProduct())return false;
 const auto user=GetModuleHandleW(L"user32.dll");
 const char* names[]={"SetWindowPos","ShowWindow","ShowWindowAsync","LockSetForegroundWindow"};
 void* replacements[]={(void*)&Position,(void*)&Show,(void*)&Show,(void*)&Lock};
 for(unsigned i=0;i<4;++i){
  entries[i]=(void*)GetProcAddress(user,names[i]);detours[i]=replacements[i];if(!entries[i])return false;
  int alias=-1;for(unsigned j=0;j<i;++j)if(entries[j]==entries[i])alias=(int)j;
  if(alias>=0){if(detours[alias]!=detours[i]||!vi_cursor_spy::Reaches(entries[i],detours[alias]))return false;continue;}
  for(int j=0;j<vi_cursor_spy::EntryCount;++j)if(entries[i]==vi_cursor_spy::entries[j])return false;
  void* unused=nullptr;if(VIMH_CreateHook(entries[i],detours[i],&unused)!=MH_OK||!unused)return false;
  if(VIMH_EnableHook(entries[i])!=MH_OK||!vi_cursor_spy::Reaches(entries[i],detours[i]))return false;
 }
 return true;
}
inline bool Verify(){if(!vi_cursor_spy::VerifyBeforeProduct())return false;for(unsigned i=0;i<4;++i)if(!vi_cursor_spy::Reaches(entries[i],detours[i]))return false;return true;}
inline void Record(FILE* out,const char* stage,bool verified){
 if(!out)return;
 fprintf(out,"{\"event\":\"terminal_guard\",\"stage\":\"%s\",\"pid\":%lu,\"verified_before_product\":%s,\"native_write_trampolines_called\":false,\"absorbed_cursor_input\":%llu,\"absorbed_foreground\":%llu,\"absorbed_queue\":%llu,\"absorbed_capture\":%llu,\"absorbed_placement\":%llu,\"absorbed_visibility\":%llu,\"absorbed_permission\":%llu}\n",stage,GetCurrentProcessId(),verified?"true":"false",vi_cursor_spy::Total(),vi_cursor_spy::foregroundEffects.load(),vi_cursor_spy::queueEffects.load(),vi_cursor_spy::captureEffects.load(),placement.load(),visibility.load(),permission.load()+vi_cursor_spy::permissionEffects.load());fflush(out);
}
}
