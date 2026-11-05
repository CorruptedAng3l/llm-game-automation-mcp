// Finite fabricated module/export/MinHook callbacks. Never call an alias token.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <joystickapi.h>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include <map>
#include <string>
#include "actual_state.h"
const wchar_t* kXinputDlls[4]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll",L"xinputuap.dll"};
bool loaded[5]{};void* modelExports[5]{};void* stateExports[4]{};unsigned modulesQueried=0,exportsQueried=0,createAttempts=0,modelCreated=0,modelEnabled=0;void* preexisting=nullptr;
struct FakeHook{void* detour;bool modelEnabled;bool foreign;};std::map<void*,FakeHook> hooks;
using MH_STATUS=int;constexpr MH_STATUS MH_OK=0,MH_ERROR_ALREADY_CREATED=1,MH_ERROR_ENABLED=2;
HMODULE Module(const wchar_t* name){++modulesQueried;unsigned slot=0;for(;slot<4;slot++)if(!std::wcscmp(name,kXinputDlls[slot]))break;if(slot==4&&std::wcscmp(name,L"winmm.dll"))return nullptr;return loaded[slot]?(HMODULE)(ULONG_PTR)(slot+1):nullptr;}
void* Export(HMODULE module,const char* name){++exportsQueried;const auto token=(ULONG_PTR)module;if(!token||token>5)return nullptr;const unsigned slot=(unsigned)token-1;if(slot<4){if((ULONG_PTR)name==100||!std::strcmp(name,"XInputGetState"))return stateExports[slot];if(!std::strcmp(name,"XInputGetCapabilities"))return modelExports[slot];}else if(!std::strcmp(name,"joyGetPosEx"))return modelExports[slot];return nullptr;}
MH_STATUS Create(void* target,void* detour,void** original){++createAttempts;if(target==preexisting||hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=target;hooks[target]={detour,false,false};++modelCreated;return MH_OK;}
MH_STATUS Enable(void* target){auto& h=hooks[target];if(h.modelEnabled)return MH_ERROR_ENABLED;h.modelEnabled=true;++modelEnabled;return MH_OK;}
#define GetModuleHandleW Module
#define GetProcAddress Export
#define VIMH_CreateHook Create
#define VIMH_EnableHook Enable
#include "actual_exports.inl"
#undef GetModuleHandleW
#undef GetProcAddress
#undef VIMH_CreateHook
#undef VIMH_EnableHook
#include "actual_json.inl"
unsigned passed=0,failed=0,safeCapabilitiesCalls=0,safeJoyCalls=0;DWORD simulatedLastError=0;
void Check(bool ok,const char* name){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
void Reset(){for(unsigned i=0;i<5;i++){loaded[i]=false;modelExports[i]=(void*)(ULONG_PTR)(0x110000+16*i);g_padApiOriginal[i]=nullptr;g_padApiTarget[i]=nullptr;g_padApiExportState[i]=0;g_padApiExportAlias[i]=0;}for(unsigned i=0;i<4;i++)stateExports[i]=(void*)(ULONG_PTR)(0x220000+16*i);for(auto& row:g_padApiCapabilities)for(auto& c:row){c.entries=0;c.ok=0;c.error=0;}g_padApiJoy.entries=0;g_padApiJoy.ok=0;g_padApiJoy.error=0;g_padApiInstalling.clear();modulesQueried=exportsQueried=createAttempts=modelCreated=modelEnabled=0;preexisting=nullptr;hooks.clear();}
bool Active(void* target){const auto found=hooks.find(target);return found!=hooks.end()&&found->second.modelEnabled;}
DWORD WINAPI NativeCapabilities(DWORD user,DWORD flags,XINPUT_CAPABILITIES* output){++safeCapabilitiesCalls;simulatedLastError=777;if(output)output->Flags=(WORD)(user+flags);return ERROR_NOT_SUPPORTED;}
MMRESULT WINAPI NativeJoy(UINT user,LPJOYINFOEX output){++safeJoyCalls;simulatedLastError=888;if(output)output->dwButtons=user;return MMSYSERR_NODRIVER;}
int main(){
 Reset();loaded[0]=loaded[4]=true;modelExports[4]=modelExports[0];TryHookPadApiCensus();const unsigned sameCreated=modelCreated,sameEnabled=modelEnabled;Check(modelCreated==0&&modelEnabled==0,"known same-pass cap/joy collision has zero optional mutations");Check(!Active(modelExports[0]),"dropping the later joy slot does not leave earlier wrong-role detour");
 Reset();loaded[0]=true;TryHookPadApiCensus();const unsigned firstCreated=modelCreated;Check(modelCreated==0&&modelEnabled==0,"optional observer makes no speculative initial code-entry mutation");loaded[4]=true;modelExports[4]=modelExports[0];TryHookPadApiCensus();const unsigned lateCreated=modelCreated,lateEnabled=modelEnabled;Check(!Active(modelExports[4]),"later-loaded cross-role export cannot inherit an earlier optional detour");
 Reset();loaded[0]=true;TryHookPadApiCensus();loaded[1]=true;stateExports[1]=modelExports[0];TryHookPadApiCensus();Check(!Active(stateExports[1]),"later-known state role cannot inherit capability observer detour");
 Reset();loaded[0]=loaded[1]=true;stateExports[1]=modelExports[0];TryHookPadApiCensus();Check(!Active(modelExports[0]),"already-known state ABI conflict remains unmodified");
 Reset();loaded[0]=true;preexisting=modelExports[0];hooks[preexisting]={(void*)(ULONG_PTR)0x333300,true,true};TryHookPadApiCensus();Check(Active(preexisting)&&hooks[preexisting].foreign&&modelCreated==0&&modelEnabled==0,"foreign existing hook is not removed or claimed as installed by us");
 Reset();loaded[0]=loaded[1]=loaded[2]=loaded[3]=loaded[4]=true;TryHookPadApiCensus();Check(modulesQueried==0&&exportsQueried==0,"disabled optional observer adds no module/export/controller discovery");Check(modelCreated==0&&modelEnabled==0,"no optional patching even when every module is available");bool disabled=true;for(auto& state:g_padApiExportState)disabled=disabled&&state==5;Check(disabled,"all optional flat slots explicitly report disabled coverage");
 const auto beforeCreate=createAttempts;TryHookPadApiCensus();Check(createAttempts==beforeCreate,"monitor re-entry never retries optional mutation");
 Reset();g_padApiOriginal[0]=(void*)&NativeCapabilities;g_padApiOriginal[4]=(void*)&NativeJoy;XINPUT_CAPABILITIES caps{};const DWORD capResult=PadApiCapabilitiesEntry<0>(3,9,&caps);Check(capResult==ERROR_NOT_SUPPORTED&&safeCapabilitiesCalls==1&&caps.Flags==12&&simulatedLastError==777,"unchanged typed capability body preserves exact original call/result/output/error surrogate");JOYINFOEX joy{};const auto joyResult=PadApiJoyEntry(17,&joy);Check(joyResult==MMSYSERR_NODRIVER&&safeJoyCalls==1&&joy.dwButtons==17&&simulatedLastError==888,"unchanged typed joy body preserves exact original call/result/output/error surrogate");
 Reset();TryHookPadApiCensus();const auto status=PadApiCensusJson();Check(status.find("disabled_unproved_future_abi_alias")!=std::string::npos,"status names disabled alias boundary rather than claiming safe install");Check(status.find("\"xinput_capabilities\":{\"coverage\":\"unmeasured\"")!=std::string::npos&&status.find("\"joy_get_pos_ex\":{\"coverage\":\"unmeasured\"")!=std::string::npos,"disabled optional observations stay unmeasured/null");Check(status.find("\"wgi_gamepad_current_reading\":{\"coverage\":\"unmeasured\"")!=std::string::npos,"unimplemented WGI scope remains unchanged");
 std::printf("{\"passed\":%u,\"failed\":%u,\"completed\":true,\"same_pass_created\":%u,\"same_pass_enabled\":%u,\"first_pass_created\":%u,\"late_pass_created\":%u,\"late_pass_enabled\":%u,\"incompatible_callbacks_invoked\":0,\"scope\":\"actual installer/status bodies; fabricated module/export/MinHook only; incompatible tokens never called\"}\n",passed,failed,sameCreated,sameEnabled,firstCreated,lateCreated,lateEnabled);return failed?1:0;
}
