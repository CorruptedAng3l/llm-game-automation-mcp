// Owned key-drive/PWM consumer; actual injected scheduler and readers. No physical input or native PrintWindow.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <xinput.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <array>
#include <mutex>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using U64=unsigned long long;
constexpr UINT ControlMessage=WM_APP+0xB8;
constexpr int Keys[5]={VK_SHIFT,'W','A','D','S'};
constexpr const char* Names[5]={"shift","w","a","d","s"};
constexpr const wchar_t* ClassName=L"ViKeyDriveOwned";
constexpr const wchar_t* Property=L"vi.keydrive.fixture.v1";
int lifetimeToken=0;HWND target=nullptr;DWORD uiThread=0;FILE* output=nullptr;std::mutex logMutex,stateMutex;bool exiting=false;
struct State {
 U64 epoch=1,lastTick=0,sampleSerial=0,polls=0,firstChordTick=0,completedTick=0,lastToken=0,captures=0,edgeRecords=0,wmRecords=0,recordsDropped=0;
 DWORD sampleThread=0;unsigned mask=0,keyStateMask=0,keyboardMask=0,eventCaptures=0,failCaptureCountdown=0;bool keyboardOk=false,chordSeen=false,completed=false,eventVisible=false;
 std::array<U64,5> reads{},high{},down{},up{},wmDown{},wmUp{};
 U64 padReads=0,padDown=0,padUp=0;DWORD padResult=ERROR_DEVICE_NOT_CONNECTED;XINPUT_GAMEPAD pad{},firstPad{};bool padActive=false,padSeen=false,padCompleted=false;
};
State state;std::atomic<U64> nextSample{0};std::atomic<bool> rejectNextKeyDown{false};
using PostFn=BOOL(WINAPI*)(HWND,UINT,WPARAM,LPARAM);PostFn realPost=nullptr;
bool Own(HWND);
void Log(const char*,...);
BOOL WINAPI ControlledPost(HWND h,UINT message,WPARAM w,LPARAM l){
 if(!Own(h)||!realPost){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&rejectNextKeyDown.exchange(false)){
  const bool high=(GetAsyncKeyState((int)w)&0x8000)!=0;
  Log("{\"event\":\"rejected_post\",\"vk\":%llu,\"message\":%u,\"virtual_high_during_rejection\":%s,\"native_post_called\":false}",static_cast<U64>(w),message,high?"true":"false");SetLastError(ERROR_NOT_ENOUGH_QUOTA);return FALSE;
 }
 return realPost(h,message,w,l);
}
bool InstallPost(){if(!capture_spy::Add("PostMessageW",reinterpret_cast<void*>(&ControlledPost)))return false;realPost=reinterpret_cast<PostFn>(capture_spy::hooks.back().original);return realPost!=nullptr;}
using QpcFn=BOOL(WINAPI*)(LARGE_INTEGER*);QpcFn realQpc=nullptr;std::atomic<LONGLONG> qpcOffset{0};LONGLONG clockFrequency=0;
BOOL WINAPI OffsetQpc(LARGE_INTEGER* value){if(!realQpc||!value)return FALSE;LARGE_INTEGER sampled{};if(!realQpc(&sampled))return FALSE;sampled.QuadPart+=qpcOffset.load();*value=sampled;return TRUE;}
bool InstallClock(){LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0||frequency.QuadPart>1000000000000LL)return false;clockFrequency=frequency.QuadPart;void* entry=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"QueryPerformanceCounter"));void* original=nullptr;void* detour=reinterpret_cast<void*>(&OffsetQpc);if(!entry||MH_CreateHook(entry,detour,&original)!=MH_OK||!original)return false;realQpc=reinterpret_cast<QpcFn>(original);if(MH_EnableHook(entry)!=MH_OK||!capture_spy::Reaches(entry,detour))return false;capture_spy::hooks.push_back({entry,detour,original});return true;}
using PadGetFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);PadGetFn padGet=nullptr;
const char* Bool(bool value){return value?"true":"false";}
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMutex);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
bool Writable(void* p,SIZE_T bytes){if(!p)return false;auto at=reinterpret_cast<std::uintptr_t>(p);if(bytes>UINTPTR_MAX-at)return false;const auto end=at+bytes;while(at<end){MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region))||region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const DWORD protect=region.Protect&0xff;const bool writable=protect==PAGE_READWRITE||protect==PAGE_WRITECOPY||protect==PAGE_EXECUTE_READWRITE||protect==PAGE_EXECUTE_WRITECOPY;if(!writable)return false;const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);if(region.RegionSize>UINTPTR_MAX-begin)return false;const auto next=begin+region.RegionSize;if(next<=at)return false;at=next<end?next:end;}return true;}
DWORD WINAPI NoPad(DWORD,XINPUT_STATE* value){if(!Writable(value,sizeof(*value)))return ERROR_BAD_ARGUMENTS;__try{*value={};return ERROR_DEVICE_NOT_CONNECTED;}__except(EXCEPTION_EXECUTE_HANDLER){return ERROR_BAD_ARGUMENTS;}}
bool InstallPadReader(){const auto module=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return false;void* const entry=reinterpret_cast<void*>(GetProcAddress(module,"XInputGetState"));if(!entry)return false;void* original=nullptr;const auto detour=reinterpret_cast<void*>(&NoPad);if(MH_CreateHook(entry,detour,&original)!=MH_OK||!original||MH_EnableHook(entry)!=MH_OK||!capture_spy::Reaches(entry,detour))return false;capture_spy::hooks.push_back({entry,detour,original});padGet=reinterpret_cast<PadGetFn>(entry);return true;}
SHORT WINAPI NoKey(int){return 0;}
BOOL WINAPI NoKeyboard(PBYTE data){if(!Writable(data,256)){SetLastError(ERROR_NOACCESS);return FALSE;}__try{memset(data,0,256);return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){SetLastError(ERROR_NOACCESS);return FALSE;}}
HWND WINAPI Outside(){return GetDesktopWindow();}
HWND WINAPI NullFocus(){return nullptr;}
BOOL WINAPI OutsideGui(DWORD,PGUITHREADINFO p){if(!Writable(p,sizeof(*p))){SetLastError(ERROR_NOACCESS);return FALSE;}__try{if(p->cbSize!=sizeof(*p)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO value{sizeof(value)};value.hwndActive=GetDesktopWindow();*p=value;return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){SetLastError(ERROR_NOACCESS);return FALSE;}}
BOOL WINAPI FakeCursor(LPPOINT p){if(!Writable(p,sizeof(*p))){SetLastError(ERROR_NOACCESS);return FALSE;}__try{*p=POINT{5000,6000};return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){SetLastError(ERROR_NOACCESS);return FALSE;}}
void WINAPI KeyWriter(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
State CopyState(){std::lock_guard<std::mutex> lock(stateMutex);return state;}
bool Own(HWND h){DWORD pid=0;return h&&h==target&&IsWindow(h)&&GetWindowThreadProcessId(h,&pid)==uiThread&&pid==GetCurrentProcessId()&&GetPropW(h,Property)==reinterpret_cast<HANDLE>(&lifetimeToken);}
bool Poll(){
 U64 epoch=0;{std::lock_guard<std::mutex> lock(stateMutex);epoch=state.epoch;}const U64 serial=nextSample.fetch_add(1)+1;
 unsigned mask=0,gks=0,gkbs=0;BYTE keyboard[256]{};
 for(unsigned i=0;i<5;i++){if(GetAsyncKeyState(Keys[i])&0x8000)mask|=1u<<i;if(GetKeyState(Keys[i])&0x8000)gks|=1u<<i;}
 const bool keyboardOk=GetKeyboardState(keyboard)!=FALSE;if(keyboardOk)for(unsigned i=0;i<5;i++)if(keyboard[Keys[i]]&0x80)gkbs|=1u<<i;
 XINPUT_STATE pad{};const DWORD padResult=padGet?padGet(0,&pad):ERROR_DEVICE_NOT_CONNECTED;const auto& p=pad.Gamepad;const bool padActive=padResult==ERROR_SUCCESS&&(p.wButtons||p.bLeftTrigger||p.bRightTrigger||p.sThumbLX||p.sThumbLY||p.sThumbRX||p.sThumbRY);
 const U64 now=GetTickCount64();const DWORD readerThread=GetCurrentThreadId();unsigned edges=0;State observed;bool emit=false;
 {std::lock_guard<std::mutex> lock(stateMutex);if(state.epoch!=epoch)return false;if(serial<=state.sampleSerial)return true;
  edges=state.mask^mask;state.mask=mask;state.keyStateMask=gks;state.keyboardMask=gkbs;state.keyboardOk=keyboardOk;state.lastTick=now;state.sampleSerial=serial;state.sampleThread=readerThread;++state.polls;
  for(unsigned i=0;i<5;i++){++state.reads[i];if(mask&(1u<<i))++state.high[i];if(edges&(1u<<i)){if(mask&(1u<<i))++state.down[i];else ++state.up[i];}}
  ++state.padReads;if(padActive!=state.padActive){if(padActive)++state.padDown;else ++state.padUp;}state.padActive=padActive;state.padResult=padResult;state.pad=pad.Gamepad;if(padActive&&!state.padSeen)state.firstPad=pad.Gamepad;if(padActive)state.padSeen=true;if(state.padSeen&&!padActive)state.padCompleted=true;
  if(mask&&!state.chordSeen){state.chordSeen=true;state.firstChordTick=now;}
  if(state.chordSeen&&!mask&&!state.completed){state.completed=true;state.completedTick=now;}
  if(edges){if(state.edgeRecords<256){++state.edgeRecords;emit=true;}else ++state.recordsDropped;}observed=state;
 }
 if(emit)Log("{\"event\":\"poll_edge\",\"epoch\":%llu,\"tick\":%llu,\"sample_serial\":%llu,\"sample_thread\":%lu,\"mask\":%u,\"changed\":%u,\"chord_seen\":%s,\"completed_latch\":%s}",observed.epoch,now,serial,readerThread,mask,edges,Bool(observed.chordSeen),Bool(observed.completed));
 return true;
}
void Snapshot(U64 token,const char* event="snapshot"){
 const State value=CopyState();
 Log("{\"event\":\"%s\",\"token\":%llu,\"epoch\":%llu,\"sample_tick\":%llu,\"sample_serial\":%llu,\"sample_thread\":%lu,\"ui_thread\":%lu,\"mask\":%u,\"gaks_mask\":%u,\"gks_mask\":%u,\"gkbs_mask\":%u,\"gkbs_ok\":%s,\"final_all_up\":%s,\"chord_seen\":%s,\"completed_latch\":%s,\"first_chord_tick\":%llu,\"completed_tick\":%llu,\"polls\":%llu,\"captures\":%llu,\"records_dropped\":%llu,\"terminal_calls\":%llu,\"native_writer_called\":false}",event,token,value.epoch,value.lastTick,value.sampleSerial,value.sampleThread,uiThread,value.mask,value.mask,value.keyStateMask,value.keyboardMask,Bool(value.keyboardOk),Bool(value.mask==0),Bool(value.chordSeen),Bool(value.completed),value.firstChordTick,value.completedTick,value.polls,value.captures,value.recordsDropped,capture_spy::writes.load());
 for(unsigned i=0;i<5;i++)Log("{\"event\":\"key_snapshot\",\"token\":%llu,\"epoch\":%llu,\"name\":\"%s\",\"vk\":%d,\"reads\":%llu,\"high_samples\":%llu,\"down_count\":%llu,\"up_count\":%llu,\"wm_down\":%llu,\"wm_up\":%llu}",token,value.epoch,Names[i],Keys[i],value.reads[i],value.high[i],value.down[i],value.up[i],value.wmDown[i],value.wmUp[i]);
 Log("{\"event\":\"pad_snapshot\",\"token\":%llu,\"epoch\":%llu,\"reads\":%llu,\"result\":%lu,\"active\":%s,\"seen\":%s,\"completed\":%s,\"down_count\":%llu,\"up_count\":%llu,\"buttons\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"lower_native_xinput_called\":false}",token,value.epoch,value.padReads,value.padResult,Bool(value.padActive),Bool(value.padSeen),Bool(value.padCompleted),value.padDown,value.padUp,value.pad.wButtons,value.pad.bLeftTrigger,value.pad.bRightTrigger,value.pad.sThumbLX,value.pad.sThumbLY,value.pad.sThumbRX,value.pad.sThumbRY);
 Log("{\"event\":\"pad_first_active\",\"token\":%llu,\"epoch\":%llu,\"seen\":%s,\"buttons\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d}",token,value.epoch,Bool(value.padSeen),value.firstPad.wButtons,value.firstPad.bLeftTrigger,value.firstPad.bRightTrigger,value.firstPad.sThumbLX,value.firstPad.sThumbLY,value.firstPad.sThumbRX,value.firstPad.sThumbRY);
}
// Writes only a caller-owned memory DC. Original PrintWindow is never called.
BOOL WINAPI SurrogatePrint(HWND h,HDC dc,UINT){
 if(!Own(h)||!dc||GetObjectType(dc)!=OBJ_MEMDC){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 RECT client{};if(!GetClientRect(h,&client)||client.left!=0||client.top!=0||client.right!=640||client.bottom!=480){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
 // Private control2 arms an explicit known-source change on the second capture,
 // so the real MCP observer must see a baseline and a later different image.
 if(!Poll()){SetLastError(ERROR_RETRY);return FALSE;}bool failCapture=false;{std::lock_guard<std::mutex> lock(stateMutex);if(state.eventCaptures&&!--state.eventCaptures)state.eventVisible=true;if(state.failCaptureCountdown&&!--state.failCaptureCountdown)failCapture=true;}const State value=CopyState();
 if(failCapture){Log("{\"event\":\"capture_failure\",\"epoch\":%llu,\"mask\":%u,\"source\":\"one private-control known-source failure after baseline\",\"native_printwindow_called\":false}",value.epoch,value.mask);SetLastError(ERROR_RETRY);return FALSE;}
 const bool done=value.completed&&value.mask==0;const int red=done?30:value.chordSeen?220:value.eventVisible?80:30,green=done?180:value.chordSeen?160:value.eventVisible?120:80,blue=done?60:value.chordSeen?40:value.eventVisible?210:150;
 HBRUSH brush=CreateSolidBrush(RGB(red,green,blue));if(!brush)return FALSE;const bool painted=FillRect(dc,&client,brush)!=0;DeleteObject(brush);if(!painted)return FALSE;
 // A separate constant ROI supports a non-triggering guard while the event ROI
 // and known consumer state change. Both are painted into caller-owned memory.
 RECT constantRegion{0,0,32,32};brush=CreateSolidBrush(RGB(40,40,40));if(!brush)return FALSE;const bool constantPainted=FillRect(dc,&constantRegion,brush)!=0;DeleteObject(brush);if(!constantPainted)return FALSE;
 U64 capture=0;{std::lock_guard<std::mutex> lock(stateMutex);capture=++state.captures;}
 Log("{\"event\":\"capture\",\"capture\":%llu,\"epoch\":%llu,\"token\":%llu,\"sample_tick\":%llu,\"sample_serial\":%llu,\"sample_thread\":%lu,\"mask\":%u,\"chord_seen\":%s,\"completed_latch\":%s,\"event_visible\":%s,\"constant_roi\":true,\"r\":%d,\"g\":%d,\"b\":%d,\"source\":\"owned-known-state PrintWindow API surrogate; memory DC only\",\"native_printwindow_called\":false}",capture,value.epoch,value.lastToken,value.lastTick,value.sampleSerial,value.sampleThread,value.mask,Bool(value.chordSeen),Bool(value.completed),Bool(value.eventVisible),red,green,blue);return TRUE;
}
LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){
  const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;for(unsigned i=0;i<5;i++)if(w==static_cast<WPARAM>(Keys[i])||(i==0&&(w==VK_LSHIFT||w==VK_RSHIFT))){bool emit=false;U64 epoch=0;{std::lock_guard<std::mutex> lock(stateMutex);if(down)++state.wmDown[i];else ++state.wmUp[i];epoch=state.epoch;if(state.wmRecords<256){++state.wmRecords;emit=true;}else ++state.recordsDropped;}if(emit)Log("{\"event\":\"key_message\",\"epoch\":%llu,\"tick\":%llu,\"name\":\"%s\",\"vk\":%llu,\"down\":%s,\"message\":%u}",epoch,GetTickCount64(),Names[i],static_cast<U64>(w),Bool(down),message);break;}
 }
 if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP){std::lock_guard<std::mutex> lock(stateMutex);if(message==WM_LBUTTONDOWN)++state.wmDown[3];else ++state.wmUp[3];}
 if(message==ControlMessage){const U64 token=static_cast<U64>(static_cast<ULONG_PTR>(l));bool ok=Own(h);Poll();if(ok&&w==1){std::lock_guard<std::mutex> lock(stateMutex);if(state.mask)ok=false;else{const U64 next=state.epoch+1;state=State{};state.epoch=next;state.lastTick=GetTickCount64();}}else if(ok&&w==2){std::lock_guard<std::mutex> lock(stateMutex);if(state.mask)ok=false;else{state.eventCaptures=2;state.eventVisible=false;}}else if(ok&&w==3){std::lock_guard<std::mutex> lock(stateMutex);if(state.mask)ok=false;else state.failCaptureCountdown=2;}else if(ok&&w==4){const LONGLONG delta=clockFrequency*450/1000;const auto total=qpcOffset.fetch_add(delta)+delta;Log("{\"event\":\"clock_jump\",\"token\":%llu,\"delta_ms\":450,\"offset_ticks\":%lld,\"qpc_frequency\":%lld,\"clock\":\"real QueryPerformanceCounter plus controlled positive per-process offset\"}",token,total,clockFrequency);}else if(ok&&w==6){if(CopyState().mask)ok=false;else rejectNextKeyDown=true;}else if(ok&&w==8)exiting=true;else if(w!=0)ok=false;{std::lock_guard<std::mutex> lock(stateMutex);state.lastToken=token;}Snapshot(token);Log("{\"event\":\"control\",\"token\":%llu,\"action\":%llu,\"ok\":%s}",token,static_cast<U64>(w),Bool(ok));return 0;}
 if(message==WM_CLOSE||message==WM_DESTROY){exiting=true;return 0;}return DefWindowProcW(h,message,w,l);
}
bool Verify(){for(const auto& hook:capture_spy::hooks)if(!capture_spy::Reaches(hook.entry,hook.detour))return false;return true;}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=2){if(args)LocalFree(args);return 2;}output=_wfsopen(args[1],L"w",_SH_DENYNO);LocalFree(args);if(!output)return 3;
 const bool writers=capture_spy::Install()&&capture_spy::Add("keybd_event",reinterpret_cast<void*>(&KeyWriter));const auto uniqueWriterEntries=capture_spy::hooks.size();const unsigned writerCount=writers?19u:0u;
 const bool readers=writers&&capture_spy::Add("GetAsyncKeyState",reinterpret_cast<void*>(&NoKey))&&capture_spy::Add("GetKeyState",reinterpret_cast<void*>(&NoKey))&&capture_spy::Add("GetKeyboardState",reinterpret_cast<void*>(&NoKeyboard))&&capture_spy::Add("GetForegroundWindow",reinterpret_cast<void*>(&Outside))&&capture_spy::Add("GetGUIThreadInfo",reinterpret_cast<void*>(&OutsideGui))&&capture_spy::Add("GetActiveWindow",reinterpret_cast<void*>(&NullFocus))&&capture_spy::Add("GetFocus",reinterpret_cast<void*>(&NullFocus))&&capture_spy::Add("GetCursorPos",reinterpret_cast<void*>(&FakeCursor))&&capture_spy::Add("GetPhysicalCursorPos",reinterpret_cast<void*>(&FakeCursor))&&capture_spy::Add("PrintWindow",reinterpret_cast<void*>(&SurrogatePrint));
 const bool safe=readers&&InstallPadReader()&&InstallClock()&&InstallPost()&&Verify();Log("{\"event\":\"guards\",\"safe\":%s,\"phase\":\"before window and DLL\",\"terminal_desktop_writers\":%llu,\"unique_writer_entries\":%llu,\"native_writer_called\":false,\"native_printwindow_called\":false,\"lower_keys\":\"fabricated all up\",\"lower_focus\":\"fabricated outside/null\",\"lower_xinput\":\"terminal fabricated disconnected; native export never called\"}",Bool(safe),static_cast<U64>(writerCount),static_cast<U64>(uniqueWriterEntries));if(!safe)return 4;
 WNDCLASSW cls{};cls.lpfnWndProc=Window;cls.hInstance=instance;cls.lpszClassName=ClassName;if(!RegisterClassW(&cls))return 5;uiThread=GetCurrentThreadId();
 const LONGLONG sx=GetSystemMetrics(SM_XVIRTUALSCREEN),sy=GetSystemMetrics(SM_YVIRTUALSCREEN),sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(sw<=0||sh<=0||!(-26000LL+640<=sx||-26000LL>=sx+sw||600LL+480<=sy||600LL>=sy+sh))return 5;
 target=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,ClassName,L"owned key drive consumer",WS_POPUP|WS_VISIBLE,-26000,600,640,480,nullptr,nullptr,instance,nullptr);if(!target||!SetPropW(target,Property,reinterpret_cast<HANDLE>(&lifetimeToken))||!Own(target))return 6;
 RECT client{};if(!GetClientRect(target,&client)||client.right!=640||client.bottom!=480||!IsWindowVisible(target))return 6;Poll();
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"lifetime_cookie\":%llu,\"class\":\"ViKeyDriveOwned\",\"client_w\":640,\"client_h\":480,\"actual_offscreen_checked\":true,\"window_visible\":true,\"noactivate\":true,\"toolwindow\":true,\"control_message\":%u,\"consumer\":\"actual hooked GAKS Shift/W/A/D/S with original WndProc edge counts\",\"image_source\":\"owned-known-state PrintWindow API surrogate; memory DC only\"}",GetCurrentProcessId(),uiThread,static_cast<U64>(reinterpret_cast<ULONG_PTR>(target)),static_cast<U64>(reinterpret_cast<ULONG_PTR>(&lifetimeToken)),ControlMessage);
 const U64 end=GetTickCount64()+120000;U64 heartbeat=0;while(!exiting&&GetTickCount64()<end){MSG m{};unsigned dispatched=0;while(dispatched++<256&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){exiting=true;break;}TranslateMessage(&m);DispatchMessageW(&m);if(exiting||GetTickCount64()>=end)break;}Poll();const U64 now=GetTickCount64();if(now-heartbeat>=5000){Snapshot(0,"heartbeat");heartbeat=now;}Sleep(2);}
 Poll();Snapshot(0,"final");DestroyWindow(target);target=nullptr;Log("{\"event\":\"exit\",\"terminal_calls\":%llu,\"native_writer_called\":false,\"native_printwindow_called\":false}",capture_spy::writes.load());{std::lock_guard<std::mutex> lock(logMutex);FILE* old=output;output=nullptr;if(old)fclose(old);}return 0;
}
