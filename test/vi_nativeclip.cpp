// R1: private fake global clip, modeled foreground, terminal lower write spies.
// No native cursor/input/foreground/queue/capture/visibility writer is called.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <share.h>
#include "MinHook.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
FILE* output=nullptr;std::mutex logMx,clipMx;
HWND windows[2]{};RECT bounds[2]{};DWORD uiThread=0;
std::atomic<int> foregroundMode{0},geometryMode{0};
std::atomic<bool> queryFail{false},clearFail{false};
RECT fakeRect{-30000,-30000,30000,30000};bool fakeEnabled=false;
std::atomic<unsigned long long> sets{0},clears{0},clearSuccess{0},queries{0},otherWrites{0},positionWrites{0},ordinal{0},tokenNow{0};
HANDLE queryEntered=nullptr,queryRelease=nullptr,newEntered=nullptr,newDone=nullptr,raceThread=nullptr;
std::atomic<bool> gateArmed{false},setGateArmed{false},raceReached{false},raceDone{false},raceBeforeRelease{false};
std::atomic<int> raceKind{0};std::atomic<bool> raceNewOk{false};
std::atomic<DWORD> raceNewError{0};
std::atomic<bool> reenterOnce{false},reenterReached{false},reenterOk{false};std::atomic<DWORD> reenterError{0};
std::atomic<bool> crossReadOnce{false},crossReadReached{false},crossReadBeforeReturn{false},crossReadOk{false};
std::atomic<bool> positionClipOnce{false},positionClipReached{false},positionClipOk{false};
std::atomic<bool> getterClipOnce{false},getterClipReached{false},getterClipOk{false};
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
const char* Bool(bool b){return b?"true":"false";}
using Fg=HWND(WINAPI*)();using Gui=BOOL(WINAPI*)(DWORD,PGUITHREADINFO);
using Client=BOOL(WINAPI*)(HWND,LPRECT);using ToScreen=BOOL(WINAPI*)(HWND,LPPOINT);using RectFn=BOOL(WINAPI*)(HWND,LPRECT);using BoolWindow=BOOL(WINAPI*)(HWND);
Client originalClient=nullptr;ToScreen originalToScreen=nullptr;RectFn originalRect=nullptr;BoolWindow originalIconic=nullptr,originalVisible=nullptr;
using Metrics=int(WINAPI*)(int);Metrics originalMetrics=nullptr;
int WINAPI LowerMetrics(int id){if(id==SM_XVIRTUALSCREEN||id==SM_YVIRTUALSCREEN)return -30000;if(id==SM_CXVIRTUALSCREEN||id==SM_CYVIRTUALSCREEN)return 60000;return originalMetrics(id);}
bool Own(HWND h){return h&&(h==windows[0]||h==windows[1]);}
HWND WINAPI LowerForeground(){const auto mode=foregroundMode.load();return mode==1||mode==3?windows[0]:mode==2?windows[1]:GetDesktopWindow();}
BOOL WINAPI LowerGui(DWORD,PGUITHREADINFO info){if(!info||info->cbSize!=sizeof(*info)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO value{sizeof(value)};value.hwndActive=foregroundMode==3?windows[1]:LowerForeground();*info=value;return TRUE;}
BOOL WINAPI LowerClient(HWND h,LPRECT r){if(Own(h)&&geometryMode==3){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}return originalClient(h,r);}
BOOL WINAPI LowerToScreen(HWND h,LPPOINT p){return originalToScreen(h,p);}
BOOL WINAPI LowerWindowRect(HWND h,LPRECT r){return originalRect(h,r);}
BOOL WINAPI LowerIconic(HWND h){return Own(h)&&geometryMode==1?TRUE:originalIconic(h);}
BOOL WINAPI LowerVisible(HWND h){return Own(h)&&geometryMode==2?FALSE:originalVisible(h);}
DWORD WINAPI PublicClipRead(void*){RECT value{};crossReadOk=GetClipCursor(&value)!=FALSE;return 0;}
BOOL WINAPI LowerClip(const RECT* r){
 const auto call=++ordinal;const bool clearing=r==nullptr;const bool ok=!clearing||!clearFail.load();
 if(clearing)++clears;else ++sets;
 {std::lock_guard<std::mutex> lock(clipMx);if(ok){fakeEnabled=!clearing;fakeRect=r?*r:RECT{-30000,-30000,30000,30000};if(clearing)++clearSuccess;}}
 if(r&&reenterOnce.exchange(false)){reenterReached=true;const RECT nested{bounds[0].left+40,bounds[0].top+40,bounds[0].right-40,bounds[0].bottom-40};SetLastError(ERROR_SUCCESS);reenterOk=ClipCursor(&nested)!=FALSE;reenterError=GetLastError();Log("{\"event\":\"reentrant_clip\",\"ok\":%s,\"error\":%lu,\"native_writer_called\":false}",Bool(reenterOk),reenterError.load());}
 if(r&&crossReadOnce.exchange(false)){crossReadReached=true;HANDLE reader=CreateThread(nullptr,0,PublicClipRead,nullptr,0,nullptr);crossReadBeforeReturn=reader&&WaitForSingleObject(reader,700)==WAIT_OBJECT_0;if(reader)CloseHandle(reader);Log("{\"event\":\"cross_thread_clip_read\",\"completed_before_original_return\":%s,\"read_ok\":%s,\"native_writer_called\":false}",Bool(crossReadBeforeReturn),Bool(crossReadOk));}
 if(!clearing&&setGateArmed.exchange(false)){raceReached=true;SetEvent(queryEntered);const auto waited=WaitForSingleObject(queryRelease,1500);Log("{\"event\":\"set_return_barrier\",\"released\":%s,\"native_writer_called\":false}",Bool(waited==WAIT_OBJECT_0));}
 Log("{\"event\":\"lower_clip\",\"ordinal\":%llu,\"token\":%llu,\"thread\":%lu,\"clear\":%s,\"ok\":%s,\"native_writer_called\":false}",call,tokenNow.load(),GetCurrentThreadId(),Bool(clearing),Bool(ok));
 if(!ok)SetLastError(ERROR_ACCESS_DENIED);return ok?TRUE:FALSE;
}
BOOL WINAPI LowerGetClip(LPRECT r){
 ++queries;if(!r){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(gateArmed.exchange(false)){raceReached=true;SetEvent(queryEntered);const auto waited=WaitForSingleObject(queryRelease,1500);Log("{\"event\":\"query_barrier\",\"released\":%s,\"native_query_called\":false}",Bool(waited==WAIT_OBJECT_0));}
 if(queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 std::lock_guard<std::mutex> lock(clipMx);*r=fakeRect;return TRUE;
}
BOOL WINAPI Position(int,int){++otherWrites;++positionWrites;if(positionClipOnce.exchange(false)){positionClipReached=true;const RECT clip{bounds[0].left+10,bounds[0].top+20,bounds[0].right-20,bounds[0].bottom-20};positionClipOk=ClipCursor(&clip)!=FALSE;}return TRUE;}
BOOL WINAPI ReadPosition(LPPOINT outputPoint){if(!outputPoint){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}*outputPoint=POINT{bounds[0].left+40,bounds[0].top+40};if(GetCurrentThreadId()==uiThread&&getterClipOnce.exchange(false)){getterClipReached=true;const RECT clip{bounds[0].left+10,bounds[0].top+20,bounds[0].right-20,bounds[0].bottom-20};getterClipOk=ClipCursor(&clip)!=FALSE;}return TRUE;}
UINT WINAPI Send(UINT count,LPINPUT,int){++otherWrites;return count;}
void WINAPI Mouse(DWORD,DWORD,DWORD,DWORD,ULONG_PTR){++otherWrites;}
void WINAPI Key(BYTE,BYTE,DWORD,ULONG_PTR){++otherWrites;}
SHORT WINAPI NoAsyncKey(int){return 0;}
BOOL WINAPI ForegroundWrite(HWND){++otherWrites;return FALSE;}
HWND WINAPI Focus(HWND){++otherWrites;return nullptr;}
void WINAPI Switch(HWND,BOOL){++otherWrites;}
BOOL WINAPI Attach(DWORD,DWORD,BOOL){++otherWrites;return FALSE;}
HWND WINAPI Capture(HWND){++otherWrites;return nullptr;}
BOOL WINAPI Release(){++otherWrites;return TRUE;}
BOOL WINAPI Permission(DWORD){++otherWrites;return FALSE;}
BOOL WINAPI LockPermission(UINT){++otherWrites;return FALSE;}
BOOL WINAPI WindowPos(HWND,HWND,int,int,int,int,UINT){++otherWrites;return TRUE;}
BOOL WINAPI Show(HWND,int){++otherWrites;return TRUE;}
struct Hook{void* entry;void* detour;};std::vector<Hook> hooks;
bool Read(const void* p,void* out,SIZE_T size){SIZE_T got=0;return p&&ReadProcessMemory(GetCurrentProcess(),p,out,size,&got)&&got==size;}
// Same bounded entry-chain witness as tracked capture/cursor spy fixtures.
bool Reaches(void* entry,void* target){auto at=(std::uintptr_t)entry;for(unsigned i=0;i<6&&at;++i){if(at==(std::uintptr_t)target)return true;unsigned char b[6]{};if(!Read((void*)at,b,sizeof(b)))return false;std::int32_t d=0;
 if(b[0]==0xE9){memcpy(&d,b+1,4);at=(std::uintptr_t)((std::intptr_t)at+5+d);continue;}if(b[0]==0xEB){at=(std::uintptr_t)((std::intptr_t)at+2+(std::int8_t)b[1]);continue;}
 if(b[0]==0xFF&&b[1]==0x25){memcpy(&d,b+2,4);
#ifdef _WIN64
 const auto slot=(std::uintptr_t)((std::intptr_t)at+6+d);
#else
 const auto slot=(std::uintptr_t)(std::uint32_t)d;
#endif
 if(!Read((void*)slot,&at,sizeof(at)))return false;continue;}return false;}return false;}
bool Add(const char* name,void* detour,void** original=nullptr){void* entry=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),name);if(!entry)return false;for(const auto& h:hooks)if(h.entry==entry)return h.detour==detour&&Reaches(entry,detour);void* copy=nullptr;if(MH_CreateHook(entry,detour,&copy)!=MH_OK||!copy)return false;if(original)*original=copy;if(MH_EnableHook(entry)!=MH_OK||!Reaches(entry,detour))return false;hooks.push_back({entry,detour});return true;}
bool Install(){if(MH_Initialize()!=MH_OK)return false;
 const char* names[]={"SetCursorPos","SetPhysicalCursorPos","ClipCursor","SendInput","mouse_event","SetForegroundWindow","BringWindowToTop","SetActiveWindow","SetFocus","SwitchToThisWindow","AttachThreadInput","SetCapture","ReleaseCapture","AllowSetForegroundWindow","LockSetForegroundWindow","SetWindowPos","ShowWindow","ShowWindowAsync","keybd_event","GetForegroundWindow","GetGUIThreadInfo","GetClipCursor","GetAsyncKeyState","GetCursorPos","GetPhysicalCursorPos"};
 void* detours[]={(void*)&Position,(void*)&Position,(void*)&LowerClip,(void*)&Send,(void*)&Mouse,(void*)&ForegroundWrite,(void*)&ForegroundWrite,(void*)&Focus,(void*)&Focus,(void*)&Switch,(void*)&Attach,(void*)&Capture,(void*)&Release,(void*)&Permission,(void*)&LockPermission,(void*)&WindowPos,(void*)&Show,(void*)&Show,(void*)&Key,(void*)&LowerForeground,(void*)&LowerGui,(void*)&LowerGetClip,(void*)&NoAsyncKey,(void*)&ReadPosition,(void*)&ReadPosition};
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!Add(names[i],detours[i]))return false;
 return Add("GetClientRect",(void*)&LowerClient,(void**)&originalClient)&&Add("ClientToScreen",(void*)&LowerToScreen,(void**)&originalToScreen)&&Add("GetWindowRect",(void*)&LowerWindowRect,(void**)&originalRect)&&Add("IsIconic",(void*)&LowerIconic,(void**)&originalIconic)&&Add("IsWindowVisible",(void*)&LowerVisible,(void**)&originalVisible)&&Add("GetSystemMetrics",(void*)&LowerMetrics,(void**)&originalMetrics);
}
RECT Inside(unsigned index,bool alternate=false){RECT r=bounds[index];r.left+=alternate?30:10;r.top+=20;r.right-=20;r.bottom-=20;return r;}
BOOL RequestClip(unsigned index,bool alternate=false){const auto r=Inside(index,alternate);return ClipCursor(&r);}
DWORD WINAPI NewRequest(void*){SetEvent(newEntered);SetLastError(ERROR_SUCCESS);raceNewOk=RequestClip(0,true)!=FALSE;raceNewError=GetLastError();SetEvent(newDone);return 0;}
DWORD WINAPI Race(void*){const bool reached=WaitForSingleObject(queryEntered,1700)==WAIT_OBJECT_0;raceReached=reached;
 if(reached){if(raceKind==1)foregroundMode=0;else{foregroundMode=1;HANDLE worker=CreateThread(nullptr,0,NewRequest,nullptr,0,nullptr);if(worker){WaitForSingleObject(newEntered,500);raceBeforeRelease=WaitForSingleObject(newDone,700)==WAIT_OBJECT_0;SetEvent(queryRelease);WaitForSingleObject(worker,1700);CloseHandle(worker);}else SetEvent(queryRelease);}SetEvent(queryRelease);}
 else SetEvent(queryRelease);raceDone=true;Log("{\"event\":\"race_done\",\"kind\":%d,\"reached_query\":%s,\"new_request_completed_before_release\":%s,\"new_request_ok\":%s}",raceKind.load(),Bool(reached),Bool(raceBeforeRelease),Bool(raceNewOk));return 0;}
bool ArmRace(int kind){if(raceThread){if(WaitForSingleObject(raceThread,0)!=WAIT_OBJECT_0)return false;CloseHandle(raceThread);raceThread=nullptr;}ResetEvent(queryEntered);ResetEvent(queryRelease);ResetEvent(newEntered);ResetEvent(newDone);raceKind=kind;raceDone=false;raceReached=false;raceNewOk=false;raceNewError=0;raceBeforeRelease=false;gateArmed=kind==2;setGateArmed=kind==1;raceThread=CreateThread(nullptr,0,Race,nullptr,0,nullptr);return raceThread!=nullptr;}
void Snapshot(unsigned long long token,unsigned action,bool callOk){RECT r{};bool enabled=false;{std::lock_guard<std::mutex> lock(clipMx);r=fakeRect;enabled=fakeEnabled;}
 Log("{\"event\":\"snapshot\",\"token\":%llu,\"action\":%u,\"call_ok\":%s,\"model\":%d,\"sets\":%llu,\"clears\":%llu,\"clear_success\":%llu,\"queries\":%llu,\"other_writes\":%llu,\"position_writes\":%llu,\"fake_enabled\":%s,\"fake_rect\":[%ld,%ld,%ld,%ld],\"race_done\":%s,\"race_reached\":%s,\"race_new_ok\":%s,\"race_completed_before_release\":%s,\"native_writer_called\":false}",token,action,Bool(callOk),foregroundMode.load(),sets.load(),clears.load(),clearSuccess.load(),queries.load(),otherWrites.load(),positionWrites.load(),Bool(enabled),r.left,r.top,r.right,r.bottom,Bool(raceDone),Bool(raceReached),Bool(raceNewOk),Bool(raceBeforeRelease));}
LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+0xA0){const auto action=(unsigned)w;tokenNow=(unsigned long long)(ULONG_PTR)l;bool ok=true;
 switch(action){case 0:break;case 1:foregroundMode=0;break;case 2:foregroundMode=1;break;case 3:foregroundMode=2;break;case 4:foregroundMode=3;break;case 5:ok=RequestClip(0)!=FALSE;break;case 6:ok=ClipCursor(nullptr)!=FALSE;break;
 case 7:{RECT r=Inside(0);r.left=bounds[0].left-1;ok=ClipCursor(&r)!=FALSE;break;}case 8:{RECT r{20,20,10,10};ok=ClipCursor(&r)!=FALSE;break;}
 case 9:case 10:case 11:geometryMode=(int)action-8;ok=RequestClip(0)!=FALSE;geometryMode=0;break;
 case 12:{std::lock_guard<std::mutex> lock(clipMx);fakeRect=Inside(0,true);fakeEnabled=true;break;}case 13:queryFail=true;break;case 14:clearFail=true;break;case 15:queryFail=false;clearFail=false;break;case 16:ok=RequestClip(1)!=FALSE;break;
 case 17:foregroundMode=1;ok=ArmRace(1);if(ok){ok=RequestClip(0)!=FALSE;WaitForSingleObject(raceThread,2000);}break;
 case 18:ok=ArmRace(2);if(ok)foregroundMode=0;break;
 case 19:{POINT p{bounds[0].left+40,bounds[0].top+40};ok=SetCursorPos(p.x,p.y)!=FALSE;SetPhysicalCursorPos(p.x,p.y);break;}
 case 20:foregroundMode=1;reenterReached=false;reenterOk=false;reenterError=0;reenterOnce=true;ok=RequestClip(0)!=FALSE;reenterOnce=false;Log("{\"event\":\"reentrant_control\",\"token\":%llu,\"reached\":%s,\"nested_ok\":%s,\"nested_error\":%lu}",tokenNow.load(),Bool(reenterReached),Bool(reenterOk),reenterError.load());break;
 case 21:foregroundMode=1;crossReadReached=false;crossReadBeforeReturn=false;crossReadOk=false;crossReadOnce=true;ok=RequestClip(0)!=FALSE;crossReadOnce=false;Log("{\"event\":\"cross_thread_control\",\"token\":%llu,\"reached\":%s,\"completed_before_original_return\":%s,\"read_ok\":%s}",tokenNow.load(),Bool(crossReadReached),Bool(crossReadBeforeReturn),Bool(crossReadOk));break;
 case 22:Log("{\"event\":\"race_result\",\"token\":%llu,\"new_request_error\":%lu}",tokenNow.load(),raceNewError.load());break;
 case 23:foregroundMode=1;for(unsigned api=0;api<2;++api){positionClipReached=false;positionClipOk=false;crossReadReached=false;crossReadBeforeReturn=false;crossReadOk=false;positionClipOnce=true;crossReadOnce=true;const BOOL result=api?SetPhysicalCursorPos(bounds[0].left+40,bounds[0].top+40):SetCursorPos(bounds[0].left+40,bounds[0].top+40);ok=ok&&result!=FALSE;positionClipOnce=false;crossReadOnce=false;Log("{\"event\":\"position_clip_control\",\"token\":%llu,\"api\":%u,\"position_callback_reached\":%s,\"nested_clip_ok\":%s,\"lower_clip_reached\":%s,\"reader_completed_before_return\":%s,\"reader_ok\":%s}",tokenNow.load(),api,Bool(positionClipReached),Bool(positionClipOk),Bool(crossReadReached),Bool(crossReadBeforeReturn),Bool(crossReadOk));}break;
 case 24:foregroundMode=1;for(unsigned api=0;api<2;++api){getterClipReached=false;getterClipOk=false;crossReadReached=false;crossReadBeforeReturn=false;crossReadOk=false;getterClipOnce=true;crossReadOnce=true;POINT value{};const BOOL result=api?GetPhysicalCursorPos(&value):GetCursorPos(&value);ok=ok&&result!=FALSE;getterClipOnce=false;crossReadOnce=false;Log("{\"event\":\"getter_clip_control\",\"token\":%llu,\"api\":%u,\"getter_callback_reached\":%s,\"nested_clip_ok\":%s,\"lower_clip_reached\":%s,\"reader_completed_before_return\":%s,\"reader_ok\":%s}",tokenNow.load(),api,Bool(getterClipReached),Bool(getterClipOk),Bool(crossReadReached),Bool(crossReadBeforeReturn),Bool(crossReadOk));}break;
 default:ok=false;break;}Snapshot(tokenNow,action,ok);return 0;}if(m==WM_DESTROY)return 0;return DefWindowProcW(h,m,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);if(!argv||count!=2){if(argv)LocalFree(argv);return 2;}output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 const bool installed=Install();Log("{\"event\":\"guards\",\"verified\":%s,\"terminal_writes\":19,\"keybd_event_installed\":true,\"keybd_event_probed\":false,\"native_writer_called\":false}",Bool(installed));if(!installed)return 4;
 uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"ViNativeClipOwned";if(!RegisterClassW(&wc))return 5;
 RECT screen{originalMetrics(SM_XVIRTUALSCREEN),originalMetrics(SM_YVIRTUALSCREEN),0,0};const auto width=originalMetrics(SM_CXVIRTUALSCREEN),height=originalMetrics(SM_CYVIRTUALSCREEN);if(width<=0||height<=0||!(-26000LL+640<=screen.left||-26000LL>=LONGLONG(screen.left)+width||500LL+1200<=screen.top||500LL>=LONGLONG(screen.top)+height))return 5;
 for(unsigned i=0;i<2;++i){windows[i]=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,i?L"clip own B":L"clip own A",WS_POPUP|WS_VISIBLE,-26000,500+(int)i*600,i?600:640,i?440:480,nullptr,nullptr,instance,nullptr);if(!windows[i])return 5;RECT client{};if(!originalClient(windows[i],&client))return 5;POINT a{0,0},b{client.right,client.bottom};if(!originalToScreen(windows[i],&a)||!originalToScreen(windows[i],&b))return 5;bounds[i]={a.x,a.y,b.x,b.y};}
 queryEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);queryRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);newEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);newDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!queryEntered||!queryRelease||!newEntered||!newDone)return 6;
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"a\":%llu,\"b\":%llu,\"a_rect\":[%ld,%ld,%ld,%ld],\"b_rect\":[%ld,%ld,%ld,%ld],\"terminal_spies\":true,\"modeled_foreground\":true,\"private_clip_only\":true,\"modeled_virtual_desktop\":[-30000,-30000,30000,30000],\"actual_offscreen_prerequisite\":true,\"native_os_clip_compatibility_measured\":false}",GetCurrentProcessId(),uiThread,(unsigned long long)(ULONG_PTR)windows[0],(unsigned long long)(ULONG_PTR)windows[1],bounds[0].left,bounds[0].top,bounds[0].right,bounds[0].bottom,bounds[1].left,bounds[1].top,bounds[1].right,bounds[1].bottom);
 const auto end=GetTickCount64()+180000;while(GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}
 SetEvent(queryRelease);if(raceThread){WaitForSingleObject(raceThread,2500);CloseHandle(raceThread);}for(auto h:windows)if(h)DestroyWindow(h);
 // Spies stay installed. No ClipCursor(NULL) or other native cleanup restoration.
 {std::lock_guard<std::mutex> lock(logMx);FILE* old=output;output=nullptr;if(old)fclose(old);}return 0;
}
