// Official flat-export ABIs only. No Gamepad/GameInput object is created to probe.
#include <Xinput.h>
#include <joystickapi.h>
using PadApiCapabilities=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
using PadApiJoy=MMRESULT(WINAPI*)(UINT,LPJOYINFOEX);
std::atomic<void*> g_padApiOriginal[5]{},g_padApiTarget[5]{};
// 0 unobserved;5 explicitly disabled/unmeasured. Other values are historical only.
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
// Optional flat-export mutation is disabled. Current-pass preflight cannot prove
// that an already patched entry will never be used by a later different ABI.
// This affects observation only; original exports and controller behavior stay native.
void TryHookPadApiCensus(){for(auto& state:g_padApiExportState)state.store(5,std::memory_order_release);}
