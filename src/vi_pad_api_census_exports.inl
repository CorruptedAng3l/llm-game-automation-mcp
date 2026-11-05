// Official flat-export ABIs only. No Gamepad/GameInput object is created to probe.
#include <Xinput.h>
#include <joystickapi.h>
using PadApiCapabilities=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
using PadApiJoy=MMRESULT(WINAPI*)(UINT,LPJOYINFOEX);
std::atomic<void*> g_padApiOriginal[5]{},g_padApiTarget[5]{};
// 0 not observed loaded,1 attempted,2 installed,3 failed/unknown,4 exact typed alias.
std::atomic<unsigned> g_padApiExportState[5]{},g_padApiExportAlias[5]{};
std::atomic_flag g_padApiInstalling=ATOMIC_FLAG_INIT;
template<unsigned Slot> DWORD WINAPI PadApiCapabilitiesEntry(DWORD user,DWORD flags,XINPUT_CAPABILITIES* output){
 auto original=(PadApiCapabilities)g_padApiOriginal[Slot].load(std::memory_order_acquire);
 auto& calls=g_padApiCapabilities[Slot][user<4?user:4];++calls.entries;
 // Original is published before EnableHook. The fallback is an unarmed error,
 // never a fake connected controller; a missing original cannot be forwarded.
 if(!original)return calls.Finish((DWORD)ERROR_DEVICE_NOT_CONNECTED);
 const DWORD result=original(user,flags,output);return calls.Finish(result);
}
MMRESULT WINAPI PadApiJoyEntry(UINT device,LPJOYINFOEX output){
 auto original=(PadApiJoy)g_padApiOriginal[4].load(std::memory_order_acquire);++g_padApiJoy.entries;
 if(!original)return g_padApiJoy.Finish((MMRESULT)MMSYSERR_NODRIVER);
 const MMRESULT result=original(device,output);return g_padApiJoy.Finish(result);
}
void* const kPadApiExportDetour[5]={(void*)&PadApiCapabilitiesEntry<0>,(void*)&PadApiCapabilitiesEntry<1>,(void*)&PadApiCapabilitiesEntry<2>,(void*)&PadApiCapabilitiesEntry<3>,(void*)&PadApiJoyEntry};
bool PadApiStateAbiConflict(void* target){
 for(unsigned i=0;i<4;i++){
  HMODULE module=GetModuleHandleW(kXinputDlls[i]);if(!module)continue;
  if(target==(void*)GetProcAddress(module,"XInputGetState")||target==(void*)GetProcAddress(module,MAKEINTRESOURCEA(100)))return true;
 }
 return false;
}
void PadApiInstallExport(unsigned slot,void* target){
 g_padApiExportState[slot]=1;g_padApiTarget[slot]=target;
 if(!target||PadApiStateAbiConflict(target)){g_padApiExportState[slot]=3;return;}
 for(unsigned prior=0;prior<5;prior++)if(prior!=slot&&g_padApiTarget[prior].load()==target){
  const unsigned state=g_padApiExportState[prior].load();
  if(slot<4&&prior<4&&(state==2||state==4)&&g_padApiOriginal[prior].load()){
   g_padApiOriginal[slot]=g_padApiOriginal[prior].load();g_padApiExportAlias[slot]=state==4?g_padApiExportAlias[prior].load():prior;g_padApiExportState[slot]=4;return;
  }
  // Cross-ABI or unproved already-created entry never becomes a covered alias.
  g_padApiExportState[slot]=3;return;
 }
 void* original=nullptr;const MH_STATUS created=VIMH_CreateHook(target,kPadApiExportDetour[slot],&original);
 if(created!=MH_OK||!original){g_padApiExportState[slot]=3;return;}
 g_padApiOriginal[slot].store(original,std::memory_order_release);
 const MH_STATUS enabled=VIMH_EnableHook(target);g_padApiExportState[slot]=enabled==MH_OK||enabled==MH_ERROR_ENABLED?2:3;
}
void TryHookPadApiCensus(){
 if(g_padApiInstalling.test_and_set(std::memory_order_acquire))return;
 struct Clear{~Clear(){g_padApiInstalling.clear(std::memory_order_release);}} clear;
 for(unsigned slot=0;slot<5;slot++){
  if(g_padApiExportState[slot].load())continue;
  HMODULE module=GetModuleHandleW(slot<4?kXinputDlls[slot]:L"winmm.dll");if(!module)continue;
  PadApiInstallExport(slot,(void*)GetProcAddress(module,slot<4?"XInputGetCapabilities":"joyGetPosEx"));
 }
}
