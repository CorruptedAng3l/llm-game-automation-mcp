// Exact old/new installers, finite module/export/MinHook/memory endpoints only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include "vi_mh_rename.h"
#include "MinHook.h"
static const char* g_expectedNames[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","GetForegroundWindow","GetGUIThreadInfo","SetForegroundWindow","BringWindowToTop","SetActiveWindow","SetFocus","SwitchToThisWindow","AttachThreadInput","SetCapture","ReleaseCapture","AllowSetForegroundWindow","SetWindowPos","ShowWindow","ShowWindowAsync","LockSetForegroundWindow","keybd_event"};
static unsigned char storage[21][32]{};struct Hook{void* detour;bool enabled;};static std::map<void*,Hook> hooks;static unsigned lookups[21]{},createCalls=0,enableCalls=0;static std::string g_modelMode;
static HMODULE WINAPI Module(LPCWSTR){return (HMODULE)(ULONG_PTR)1;}
static FARPROC WINAPI Export(HMODULE,LPCSTR name){for(unsigned i=0;i<21;++i)if(!strcmp(name,g_expectedNames[i])){++lookups[i];if(g_modelMode=="missing"&&i==20)return nullptr;unsigned actual=i;if(g_modelMode=="alias"&&(i==1||i==18))actual=i==1?0:17;if(g_modelMode=="conflict"&&i==16)actual=0;return (FARPROC)&storage[actual][8];}return nullptr;}
static MH_STATUS WINAPI Initialize(){return MH_OK;}
static MH_STATUS WINAPI Create(void* target,void* detour,void** original){++createCalls;if(g_modelMode=="create-fail"&&createCalls==10)return MH_ERROR_MEMORY_ALLOC;if(hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=(unsigned char*)target+8;hooks[target]={detour,false};return MH_OK;}
static MH_STATUS WINAPI Enable(void* target){++enableCalls;if(g_modelMode=="enable-fail")return MH_ERROR_MEMORY_PROTECT;if(!target){for(auto& item:hooks)item.second.enabled=true;}else hooks[target].enabled=true;return MH_OK;}
static BOOL WINAPI ModelRead(HANDLE,LPCVOID address,LPVOID out,SIZE_T size,SIZE_T* actual){memset(out,0,size);auto found=hooks.find((void*)address);if(found!=hooks.end()&&found->second.enabled&&size>=5&&!(g_modelMode=="route-fail"&&address==&storage[0][8])){const auto offset=(std::intptr_t)found->second.detour-(std::intptr_t)address-5;const std::int32_t jump=(std::int32_t)offset;if((std::intptr_t)jump!=offset)return FALSE;((unsigned char*)out)[0]=0xe9;memcpy((unsigned char*)out+1,&jump,4);}if(actual)*actual=size;return TRUE;}
static HANDLE WINAPI Current(){return (HANDLE)(LONG_PTR)-1;}
static HWND WINAPI Desktop(){return (HWND)(ULONG_PTR)2;}
#define GetModuleHandleW Module
#define GetProcAddress Export
#define ReadProcessMemory ModelRead
#define GetCurrentProcess Current
#define GetDesktopWindow Desktop
#define VIMH_Initialize Initialize
#define VIMH_CreateHook Create
#define VIMH_EnableHook Enable
#include "vi_launch_terminal_batch.h"
static unsigned pass=0,fail=0;static void Check(bool value,const char* name){printf("%s %s\n",value?"PASS":"FAIL",name);value?++pass:++fail;}
int main(int argc,char** argv){
 if(argc!=3)return 2;const bool batch=!strcmp(argv[1],"batch");g_modelMode=argv[2];const bool positive=g_modelMode=="normal"||g_modelMode=="alias";
 const bool installed=vi_launch_terminal_batch::Install(batch);
 Check(installed==positive,"all prerequisites must succeed before a product boundary can open");
 if(positive){
  unsigned queried=0;for(auto value:lookups)if(value==1)++queried;Check(queried==21,"both installers request the same21 exact exports once");
  const unsigned expected=g_modelMode=="alias"?19:21;Check(hooks.size()==expected&&createCalls==expected,"ABI-compatible aliases deduplicate identical native entries");
  Check(enableCalls==(batch?1:expected),"batch performs one enable cycle and legacy performs each separate cycle");
  Check(vi_launch_terminal_batch::verifiedExports==21&&vi_launch_terminal_batch::Verify(),"every one of21 export routes is explicitly verified before product");
  vi_launch_terminal_batch::Key(0,0,0,0);Check(vi_launch_terminal_batch::keyCalls==1,"keybd_event included as a terminal nondelegating sink");
  auto& first=hooks.begin()->second;first.enabled=false;Check(!vi_launch_terminal_batch::Verify(),"lost enabled route closes verification");first.enabled=true;Check(vi_launch_terminal_batch::Verify(),"only restored actual route restores verification");
 }else{
  Check(!vi_launch_terminal_batch::Verify(),"failed setup never reports verified product coverage");
  if(batch&&(g_modelMode=="missing"||g_modelMode=="conflict"||g_modelMode=="create-fail")){bool enabled=false;for(const auto& item:hooks)enabled|=item.second.enabled;Check(enableCalls==0&&!enabled,"create/ABI/export failure leaves all queued hooks disabled");}
 }
 printf("terminal batch %s/%s: %u PASS / %u FAIL; creates=%u enables=%u\n",argv[1],argv[2],pass,fail,createCalls,enableCalls);return fail?1:0;
}
