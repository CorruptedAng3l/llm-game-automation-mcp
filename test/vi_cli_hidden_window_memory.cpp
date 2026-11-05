// Actual CLI hidden eligibility/parser adapter under memory-only OS functions.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <map>
#include <limits>
#include <string>
#include <cstdio>
#include "../src/vi_ready_json.h"
struct TestWindow {DWORD pid=77,thread=10;bool visible=false;LONG_PTR style=WS_POPUP,extended=0;HWND parent=nullptr,owner=nullptr,root=nullptr,actualParent=(HWND)(ULONG_PTR)9999;ULONG_PTR cookie=1;std::wstring name=L"OwnedGame";};
static std::map<HWND,TestWindow> windows;static DWORD processPid=77;static unsigned long long processBorn=100;static bool alive=true,queryImage=true,changeDuringEnum=false;static unsigned enumerateCalls=0;
static HWND New(unsigned n){const auto h=(HWND)(ULONG_PTR)n;windows[h].root=h;return h;}
static DWORD FWait(HANDLE,DWORD){return alive?WAIT_TIMEOUT:WAIT_OBJECT_0;}static DWORD FPid(HANDLE){return processPid;}
static BOOL FTimes(HANDLE,FILETIME* b,FILETIME*,FILETIME*,FILETIME*){b->dwLowDateTime=(DWORD)processBorn;b->dwHighDateTime=(DWORD)(processBorn>>32);return TRUE;}
static BOOL FImage(HANDLE,DWORD,wchar_t* image,DWORD* count){if(!queryImage)return FALSE;wcscpy_s(image,*count,L"owned-fixture.exe");*count=17;return TRUE;}
static DWORD FThread(HWND h,DWORD* pid){auto i=windows.find(h);if(i==windows.end()){*pid=0;return 0;}*pid=i->second.pid;return i->second.thread;}
static BOOL FWindow(HWND h){return windows.count(h)!=0;}static HWND FDesktop(){return (HWND)(ULONG_PTR)9999;}static HWND FParent(HWND h){return windows[h].parent;}static HWND FRoot(HWND h,UINT flags){return flags==GA_PARENT?windows[h].actualParent:windows[h].root;}static HWND FOwner(HWND h,UINT){return windows[h].owner;}
static LONG_PTR FLong(HWND h,int index){return index==GWL_STYLE?windows[h].style:windows[h].extended;}static BOOL FVisible(HWND h){return windows[h].visible;}
static BOOL FClient(HWND,RECT* r){*r={0,0,640,480};return TRUE;}static int FClass(HWND h,wchar_t* s,int count){wcsncpy_s(s,count,windows[h].name.c_str(),_TRUNCATE);return (int)wcslen(s);}static HANDLE FProp(HWND h,const wchar_t*){return (HANDLE)windows[h].cookie;}
static BOOL FEnum(WNDENUMPROC callback,LPARAM value){++enumerateCalls;for(const auto& w:windows){if(!callback(w.first,value))return FALSE;}if(changeDuringEnum)processBorn++;return TRUE;}
#define WaitForSingleObject FWait
#define GetProcessId FPid
#define GetProcessTimes FTimes
#define QueryFullProcessImageNameW FImage
#define GetWindowThreadProcessId FThread
#define IsWindow FWindow
#define GetDesktopWindow FDesktop
#define GetParent FParent
#define GetAncestor FRoot
#define GetWindow FOwner
#ifdef GetWindowLongPtrW
#undef GetWindowLongPtrW
#endif
#define GetWindowLongPtrW FLong
#define IsWindowVisible FVisible
#define GetClientRect FClient
#define GetClassNameW FClass
#define GetPropW FProp
#define EnumWindows FEnum
#include "../src/vi_cli_hidden_window.inl"
static std::string Status(bool adopted=true,std::string hidden="true",std::string source="d3d9_present",std::string age="10",std::string cookie="1"){
 return "{\"ok\":true,\"pid\":77,\"hwnd\":\""+std::string(adopted?"0x1":"0x0")+"\",\"hidden_window_version\":1,\"window_adopted\":"+(adopted?"true":"false")+",\"window_hidden\":"+hidden+",\"presentation_window\":{\"version\":1,\"process_creation_filetime\":\"100\",\"hidden_candidate_count\":1,\"ambiguous\":false,\"capacity_unconfirmed\":false,\"evidence_unconfirmed\":false,\"candidates_truncated\":false,\"hidden_candidates\":[{\"hwnd\":\"0x1\",\"pid\":77,\"thread\":10,\"window_lifetime_cookie\":\""+cookie+"\",\"process_creation_filetime\":\"100\",\"observed_age_ms\":"+age+",\"source\":\""+source+"\"}]}}";
}
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};auto reset=[](){windows.clear();processPid=77;processBorn=100;alive=queryImage=true;changeDuringEnum=false;enumerateCalls=0;New(1);};const auto process=(HANDLE)(ULONG_PTR)1;
 reset();auto snapshot=vi_launch_hidden::Observe(process,77,100);check(snapshot.process_matched&&snapshot.complete&&snapshot.hidden_count==1&&snapshot.unique==(HWND)(ULONG_PTR)1&&snapshot.reason=="hidden_window_unproven","unique own hidden enumeration stays explicitly unproven");
 New(2);snapshot=vi_launch_hidden::Observe(process,77,100);check(snapshot.complete&&snapshot.hidden_count==2&&!snapshot.unique,"two hidden windows never become one selected HWND");
 for(unsigned kind=0;kind<5;++kind){reset();auto& w=windows[(HWND)(ULONG_PTR)1];if(kind==0)w.pid=88;if(kind==1){w.style|=WS_CHILD;w.root=(HWND)(ULONG_PTR)2;}if(kind==2)w.parent=HWND_MESSAGE;if(kind==3)w.owner=(HWND)(ULONG_PTR)2;if(kind==4)w.extended=WS_EX_TOOLWINDOW;snapshot=vi_launch_hidden::Observe(process,77,100);check(snapshot.complete&&!snapshot.unique&&!snapshot.hidden_count,("foreign/child/message/owned/tool enumeration control "+std::to_string(kind)).c_str());}
 reset();alive=false;snapshot=vi_launch_hidden::Observe(process,77,100);check(!snapshot.process_matched&&!snapshot.complete&&!enumerateCalls,"exited retained process refuses before enumeration");
 reset();processBorn=101;snapshot=vi_launch_hidden::Observe(process,77,100);check(!snapshot.process_matched&&!enumerateCalls,"different process birth refuses before enumeration");
 reset();queryImage=false;snapshot=vi_launch_hidden::Observe(process,77,100);check(!snapshot.process_matched&&!enumerateCalls,"unavailable retained kernel image never falls back to PID");
 reset();changeDuringEnum=true;snapshot=vi_launch_hidden::Observe(process,77,100);check(!snapshot.complete&&!snapshot.unique,"changed process identity during enumeration refuses the snapshot");
 reset();auto state=vi_launch_hidden::Parse(Status(),process,77,100);check(state.valid&&state.adopted&&state.hidden_known&&state.hidden&&state.presentation_proven&&state.window==(HWND)(ULONG_PTR)1,"typed native adopted hidden Present proof validates exact current process and HWND identity");
 state=vi_launch_hidden::Parse(Status(false),process,77,100);check(state.valid&&!state.adopted&&state.hidden&&state.presentation_proven&&state.window==nullptr&&state.candidate==(HWND)(ULONG_PTR)1,"presenting candidate never becomes adopted merely because its proof exists");
 for(const auto& bad: {std::string("{}"),std::string("{\"ok\":true,\"pid\":77,\"hwnd\":\"0x1\"}"),Status(true,"\"true\""),Status(true,"null"),Status(true,"true","unknown"),Status(true,"true","d3d9_present","5001"),Status(true,"true","d3d9_present","10","2")}){state=vi_launch_hidden::Parse(bad,process,77,100);check(!state.valid&&!state.presentation_proven,"unsupported/malformed/stale/mismatched native status never guesses a visible or proved window");}
 reset();windows[(HWND)(ULONG_PTR)1].pid=88;state=vi_launch_hidden::Parse(Status(),process,77,100);check(!state.valid&&!state.presentation_proven,"native reply cannot authorize a foreign actual HWND");
 reset();processBorn=101;state=vi_launch_hidden::Parse(Status(),process,77,100);check(!state.valid&&!state.presentation_proven,"native status from another retained process birth is refused");
 reset();auto duplicate=Status();duplicate.insert(1,"\"window_hidden\":false,");state=vi_launch_hidden::Parse(duplicate,process,77,100);check(!state.valid,"duplicate direct native visibility fields are refused");
 auto unconfirmed=Status();const auto at=unconfirmed.find("\"evidence_unconfirmed\":false");unconfirmed.replace(at,strlen("\"evidence_unconfirmed\":false"),"\"evidence_unconfirmed\":true");state=vi_launch_hidden::Parse(unconfirmed,process,77,100);check(state.valid&&state.adopted&&!state.presentation_proven,"unconfirmed omitted evidence prevents a false unique-proof claim");
 unsigned long long n=0;check(!vi_launch_hidden::Number("18446744073709551616",(std::numeric_limits<unsigned long long>::max)(),n)&&!vi_launch_hidden::Number("9",1,n)&&!vi_launch_hidden::Number("01",5000,n,true),"numeric overflow, tiny maximum underflow and noncanonical decimal tokens refuse");
 windows.clear();auto messageWindow=New(1);windows[messageWindow].actualParent=(HWND)(ULONG_PTR)9998;auto messageOnly=vi_launch_hidden::Observe((HANDLE)1,77,100);check(messageOnly.complete&&!messageOnly.hidden_count&&!vi_launch_hidden::Parse(Status(),(HANDLE)1,77,100).valid,"message-only real parent with null GetParent cannot enter hidden selection or typed adopted proof");
 printf("CLI_HIDDEN_WINDOW_MEMORY_RESULT %u %u\n",pass,fail);return fail?1:0;}
