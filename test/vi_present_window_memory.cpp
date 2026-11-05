// Exact production metadata adapter with all window/process/clock APIs replaced
// by memory. No native window, process handle, render API or input is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <limits>
#include <string>
#include <cstdio>
#include "../src/vi_present_window.h"
struct Window {DWORD pid=77,thread=10;bool alive=true,visible=false;LONG_PTR style=(LONG_PTR)WS_POPUP,extended=0;HWND parent=nullptr,owner=nullptr,root=nullptr,actualParent=(HWND)(ULONG_PTR)9999;LONG width=640,height=480;std::wstring name=L"OwnedGame";ULONG_PTR cookie=0;};
static std::map<HWND,Window> windows;static DWORD g_pid=77;static unsigned long long born=100;static ULONGLONG nowMs=1000;static unsigned propertyWrites=0;static bool changeAfterStamp=false;static std::atomic<const char*> g_acProduct{nullptr};
static HWND New(unsigned number){const auto h=(HWND)(ULONG_PTR)number;auto& w=windows[h];w.root=h;return h;}
static DWORD FThread(HWND h,DWORD* pid){auto it=windows.find(h);if(it==windows.end()||!it->second.alive){if(pid)*pid=0;return 0;}if(pid)*pid=it->second.pid;return it->second.thread;}
static BOOL FWindow(HWND h){return windows.count(h)&&windows[h].alive;}
static BOOL FTimes(HANDLE,FILETIME* start,FILETIME*,FILETIME*,FILETIME*){start->dwLowDateTime=(DWORD)born;start->dwHighDateTime=(DWORD)(born>>32);return born!=0;}
static HANDLE FProcess(){return (HANDLE)(ULONG_PTR)1;}static ULONGLONG FTick(){return nowMs;}
static HWND FDesktop(){return (HWND)(ULONG_PTR)9999;}static HWND FParent(HWND h){return windows[h].parent;}static HWND FAncestor(HWND h,UINT flags){return flags==GA_PARENT?windows[h].actualParent:windows[h].root;}static HWND FOwner(HWND h,UINT){return windows[h].owner;}
static LONG_PTR FLong(HWND h,int index){return index==GWL_STYLE?windows[h].style:windows[h].extended;}
static BOOL FVisible(HWND h){return windows[h].visible;}static BOOL FClient(HWND h,RECT* r){*r={0,0,windows[h].width,windows[h].height};return TRUE;}
static int FClass(HWND h,wchar_t* text,int size){wcsncpy_s(text,size,windows[h].name.c_str(),_TRUNCATE);return (int)wcslen(text);}
static HANDLE FProp(HWND h,const wchar_t*){return (HANDLE)windows[h].cookie;}
static BOOL FSet(HWND h,const wchar_t*,HANDLE value){++propertyWrites;windows[h].cookie=(ULONG_PTR)value;if(changeAfterStamp)++windows[h].thread;return TRUE;}
#define GetWindowThreadProcessId FThread
#define IsWindow FWindow
#define GetProcessTimes FTimes
#define GetCurrentProcess FProcess
#define GetTickCount64 FTick
#define GetDesktopWindow FDesktop
#define GetParent FParent
#define GetAncestor FAncestor
#define GetWindow FOwner
#ifdef GetWindowLongPtrW
#undef GetWindowLongPtrW
#endif
#define GetWindowLongPtrW FLong
#define IsWindowVisible FVisible
#define GetClientRect FClient
#define GetClassNameW FClass
#define GetPropW FProp
#define SetPropW FSet
#include "../src/vi_present_window.inl"
static void ObservePresentedWindow(HWND h,unsigned api,HRESULT result){if(!vi_present_window::Source(api))return;auto witness=BeginPresentedWindow(h);EndPresentedWindow(witness,api,result);}
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};auto reset=[](){windows.clear();g_presentWindows.clear();g_presentWindowSerial=0;g_presentWindowOverflowUntil=0;g_presentWindowUnconfirmedUntil=0;g_presentWindowRejected=0;g_presentWindowCapacity=0;g_acProduct=nullptr;propertyWrites=0;changeAfterStamp=false;born=100;nowMs=1000;};
 reset();auto h=New(1);ObservePresentedWindow(h,9,S_OK);auto selected=HiddenPresentedWindow();check(selected.selected.Unique()&&selected.evidence.proof.pid==g_pid&&selected.evidence.proof.created==born&&selected.evidence.proof.cookie&&propertyWrites==1,"successful metadata creates one validated hidden lifetime candidate");check(PresentedWindowCurrent(selected.evidence),"fresh exact observed identity remains current");
 ObservePresentedWindow(h,11,S_OK);check(g_presentWindows.size()==1&&HiddenPresentedWindow().selected.Unique()&&propertyWrites==1,"duplicate device/chain observations never create ambiguity or restamp");
 auto other=New(2);ObservePresentedWindow(other,12,S_OK);check(HiddenPresentedWindow().selected.count==2&&!HiddenPresentedWindow().selected.Unique(),"two fresh hidden presenters refuse ambiguous selection");windows[other].visible=true;check(HiddenPresentedWindow().selected.Unique(),"visible presenter is excluded from hidden fallback");
 nowMs+=vi_present_window::FreshMs+1;check(!HiddenPresentedWindow().selected.Unique(),"old Present evidence expires");ObservePresentedWindow(h,9,S_OK);check(HiddenPresentedWindow().selected.Unique(),"a new successful Present refreshes the same proved lifetime");nowMs=1;check(!HiddenPresentedWindow().selected.Unique(),"future-clock evidence is refused without underflow");
 reset();h=New(1);ObservePresentedWindow(h,9,E_FAIL);check(propertyWrites==1&&!HiddenPresentedWindow().selected.Unique()&&g_presentWindows[h].api==0&&g_presentWindows[h].observed==0,"failed native Present leaves only unproven private lifetime reservation, never successful-frame evidence");ObservePresentedWindow(h,0,S_OK);check(propertyWrites==1&&!HiddenPresentedWindow().selected.Unique()&&g_presentWindows[h].api==0,"test/unknown API publication is refused without a new reservation");
 for(unsigned kind=0;kind<9;++kind){reset();h=New(1);auto& w=windows[h];if(kind==0)w.pid=88;if(kind==1){w.style|=WS_CHILD;w.root=(HWND)(ULONG_PTR)2;}if(kind==2)w.parent=HWND_MESSAGE;if(kind==3)w.owner=(HWND)(ULONG_PTR)2;if(kind==4)w.extended=WS_EX_TOOLWINDOW;if(kind==5)w.name=L"vinput_dummy_dxgi";if(kind==6)w.name=L"vinput.d3d9.hidden.probe";if(kind==7)w.width=w.height=1;if(kind==8)born=0;ObservePresentedWindow(h,9,S_OK);check(!propertyWrites&&g_presentWindows.empty(),("ineligible identity/class/shape control "+std::to_string(kind)).c_str());}
 reset();h=New(1);windows[h].actualParent=(HWND)(ULONG_PTR)9998;ObservePresentedWindow(h,9,S_OK);check(!propertyWrites&&g_presentWindows.empty(),"style0 message-only hidden parent is excluded even when GetParent returns null and GA_ROOT returns self");
 reset();h=New(1);windows[h].actualParent=nullptr;ObservePresentedWindow(h,9,S_OK);check(!propertyWrites&&g_presentWindows.empty(),"unconfirmed real parent never becomes an eligible top-level presenter");
 reset();h=New(1);windows[h].cookie=9;ObservePresentedWindow(h,9,S_OK);check(!propertyWrites&&g_presentWindows.empty(),"unrecognized preexisting lifetime property is never trusted or overwritten");
 other=New(2);ObservePresentedWindow(other,9,S_OK);check(HiddenPresentedWindow().selected.count==1&&HiddenPresentedWindow().selected.unconfirmed&&!HiddenPresentedWindow().selected.Unique(),"dropping unconfirmed hidden evidence cannot invent one unique known presenter");
 reset();h=New(1);ObservePresentedWindow(h,9,S_OK);const auto old=HiddenPresentedWindow().evidence;const auto cookie=windows[h].cookie;windows[h].cookie=0;check(!HiddenPresentedWindow().selected.Unique(),"missing lifetime witness immediately refuses selection");windows[h].cookie=cookie;ObservePresentedWindow(h,9,S_OK);check(!HiddenPresentedWindow().selected.Unique()&&!PresentedWindowCurrent(old)&&propertyWrites==1,"restoring a lost cookie cannot recover quarantined evidence");
 reset();h=New(1);ObservePresentedWindow(h,9,S_OK);windows[h].thread++;check(!HiddenPresentedWindow().selected.Unique(),"same HWND with a changed UI thread is refused");
 reset();h=New(1);changeAfterStamp=true;ObservePresentedWindow(h,9,S_OK);check(g_presentWindows.empty()&&!HiddenPresentedWindow().selected.Unique(),"identity change during initial property creation is refused before publication");
 reset();h=New(1);g_acProduct="controlled-ac";ObservePresentedWindow(h,9,S_OK);check(!propertyWrites&&g_presentWindows.empty(),"anti-cheat refusal prevents new presentation evidence");
 reset();for(unsigned n=1;n<=128;++n){h=New(n);ObservePresentedWindow(h,9,S_OK);windows[h].visible=n!=1;}h=New(129);ObservePresentedWindow(h,9,S_OK);selected=HiddenPresentedWindow();check(selected.selected.count==1&&selected.selected.capacity&&!selected.selected.Unique()&&g_presentWindows.size()==128,"metadata capacity cannot silently turn incomplete evidence into one unique candidate");nowMs+=vi_present_window::FreshMs+1;ObservePresentedWindow((HWND)(ULONG_PTR)1,9,S_OK);check(HiddenPresentedWindow().selected.Unique(),"capacity uncertainty expires only after unrecorded observations become stale");
 reset();h=New(1);auto witness=BeginPresentedWindow(h);check(witness.cookie&&g_presentWindows[h].api==0&&!HiddenPresentedWindow().selected.Unique(),"pre-native reservation alone is not successful-Present proof");EndPresentedWindow(witness,9,S_OK);check(HiddenPresentedWindow().selected.Unique(),"one exact unchanged pre/post lifetime publishes successful Present proof");
 reset();h=New(1);witness=BeginPresentedWindow(h);windows[h].alive=false;EndPresentedWindow(witness,9,S_OK);check(!HiddenPresentedWindow().selected.Unique()&&g_presentWindows[h].quarantined,"destroy during successful native Present refuses and quarantines old proof");
 reset();h=New(1);witness=BeginPresentedWindow(h);windows[h]=Window{};windows[h].root=h;EndPresentedWindow(witness,9,S_OK);check(propertyWrites==1,"post-Present validation never stamps the recycled HWND");check(!HiddenPresentedWindow().selected.Unique()&&g_presentWindows[h].quarantined&&windows[h].cookie==0,"same PID/thread HWND recycle during successful Present cannot inherit old proof");auto replacement=BeginPresentedWindow(h);EndPresentedWindow(replacement,9,S_OK);check(!HiddenPresentedWindow().selected.Unique()&&propertyWrites==1,"recycled HWND cannot recover by replaying Present or restamping");
 reset();h=New(1);witness=BeginPresentedWindow(h);const auto savedCookie=witness.cookie;windows[h].cookie=0;EndPresentedWindow(witness,9,S_OK);windows[h].cookie=savedCookie;auto restored=BeginPresentedWindow(h);EndPresentedWindow(restored,9,S_OK);check(!HiddenPresentedWindow().selected.Unique(),"restoring a lost in-flight cookie does not recover quarantine");
 printf("PRESENT_WINDOW_MEMORY_RESULT %u %u\n",pass,fail);return fail?1:0;}
