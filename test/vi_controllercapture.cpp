// Scoped controller/capture witness. Desktop writers use the tracked R2 terminal
// spies; polling/layout follows tracked vi_xinputtest/vi_padtriggerreader.
// No physical controller is read and no native PrintWindow/desktop DC is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <atomic>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using U64 = unsigned long long;
using GetState = DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
FILE* output=nullptr;std::mutex logMx,stateMx;HWND window=nullptr;DWORD uiThread=0;
GetState readers[2]{};std::atomic<U64> nativeReads{0},rumbleCalls{0},enableCalls{0},captureId{0};
std::atomic<bool> pollingPaused{false},stopping{false};std::atomic<unsigned> coldRemaining{0};std::atomic<int> displayRotation{0};thread_local int sourceInCall=-1;
struct State {
 U64 token=0,start=0,polls[2]{},activeSamples[2]{},firstActive=0,lastActive=0,neutralAfter=0;
 U64 previousTick=0,integralX=0,integralY=0,lastLog[2]{},lastPoll[2]{};
 std::int64_t displayX=0,displayY=0;
 DWORD rc[2]{ERROR_DEVICE_NOT_CONNECTED,ERROR_DEVICE_NOT_CONNECTED};XINPUT_STATE pad[2]{};
 bool seen[2]{},wasActive=false;
} state;
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
const char* Bool(bool v){return v?"true":"false";}
bool Active(const XINPUT_STATE& s){const auto& p=s.Gamepad;return p.wButtons||p.bLeftTrigger||p.bRightTrigger||p.sThumbLX||p.sThumbLY||p.sThumbRX||p.sThumbRY;}
DWORD WINAPI NeutralNative(DWORD user,XINPUT_STATE* value){++nativeReads;unsigned remaining=coldRemaining.load();if(GetCurrentThreadId()==uiThread&&sourceInCall>=0&&remaining&&coldRemaining.compare_exchange_strong(remaining,remaining-1)){const U64 begin=GetTickCount64();Log("{\"event\":\"cold_native_begin\",\"source\":%d,\"tick\":%llu,\"delay_ms\":300}",sourceInCall,begin);Sleep(300);Log("{\"event\":\"cold_native_return\",\"source\":%d,\"tick\":%llu,\"begin\":%llu}",sourceInCall,GetTickCount64(),begin);}if(!value)return ERROR_BAD_ARGUMENTS;memset(value,0,sizeof(*value));return user<4?ERROR_DEVICE_NOT_CONNECTED:ERROR_BAD_ARGUMENTS;}
DWORD WINAPI TerminalRumble(DWORD,XINPUT_VIBRATION*){++rumbleCalls;return ERROR_SUCCESS;}
void WINAPI TerminalEnable(BOOL){++enableCalls;}
void WINAPI TerminalKey(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
SHORT WINAPI NoAsync(int){return 0;}
HWND WINAPI OutsideForeground(){return GetDesktopWindow();}
BOOL WINAPI OutsideGui(DWORD,PGUITHREADINFO value){if(!value||value->cbSize!=sizeof(*value)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO fake{sizeof(fake)};fake.hwndActive=GetDesktopWindow();*value=fake;return TRUE;}
BOOL WINAPI ReadPosition(LPPOINT value){if(!value){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}*value=POINT{0,0};return TRUE;}
bool AddAddress(void* entry,void* detour){
 if(!entry)return false;for(const auto& old:capture_spy::hooks)if(old.entry==entry)return old.detour==detour&&capture_spy::Reaches(entry,detour);
 void* original=nullptr;if(MH_CreateHook(entry,detour,&original)!=MH_OK||!original||MH_EnableHook(entry)!=MH_OK||!capture_spy::Reaches(entry,detour))return false;
 capture_spy::hooks.push_back({entry,detour,original});return true;
}
void Poll(unsigned source){
 XINPUT_STATE value{};sourceInCall=(int)source;const DWORD rc=readers[source](0,&value);sourceInCall=-1;if(rc!=ERROR_SUCCESS)memset(&value,0,sizeof(value));const U64 tick=GetTickCount64();bool emit=false;U64 token=0,seq=0;
 {std::lock_guard<std::mutex> lock(stateMx);emit=!state.seen[source]||state.rc[source]!=rc||memcmp(&state.pad[source].Gamepad,&value.Gamepad,sizeof(value.Gamepad))!=0||tick-state.lastLog[source]>=100;
  state.seen[source]=true;state.rc[source]=rc;state.pad[source]=value;state.lastPoll[source]=tick;seq=++state.polls[source];token=state.token;if(Active(value))++state.activeSamples[source];
  if(source==0){const bool active=rc==ERROR_SUCCESS&&Active(value);if(active){if(!state.firstActive)state.firstActive=tick;state.lastActive=tick;}if(state.wasActive&&!active)state.neutralAfter=tick;state.wasActive=active;
   const U64 elapsed=state.previousTick?tick-state.previousTick:0;const auto x=value.Gamepad.sThumbLX,y=value.Gamepad.sThumbLY;
   state.integralX+=(U64)(x<0?-(int)x:x)*elapsed;state.integralY+=(U64)(y<0?-(int)y:y)*elapsed;
   const int rotation=displayRotation.load();const int dx=rotation==180?-(int)x:rotation==90?(int)y:(int)x,dy=rotation==180?(int)y:rotation==90?-(int)x:-(int)y;
   state.displayX+=(std::int64_t)dx*(std::int64_t)elapsed;state.displayY+=(std::int64_t)dy*(std::int64_t)elapsed;state.previousTick=tick;}
  if(emit)state.lastLog[source]=tick;
 }
 if(emit){const auto& p=value.Gamepad;Log("{\"event\":\"state\",\"case_token\":%llu,\"source\":\"%s\",\"sample\":%llu,\"tick\":%llu,\"rc\":%lu,\"packet\":%lu,\"buttons\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d}",token,source?"ex":"std",seq,tick,rc,value.dwPacketNumber,p.wButtons,p.bLeftTrigger,p.bRightTrigger,p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY);}
}
void WriteByte(HDC dc,int x,BYTE value){SetPixelV(dc,x,0,RGB(value,255-value,90));}
void Write64(HDC dc,int field,U64 value){for(int i=0;i<8;++i)WriteByte(dc,8+field*8+i,(BYTE)(value>>(i*8)));}
BOOL WINAPI CaptureMemory(HWND h,HDC dc,UINT){
 DWORD pid=0;if(!h||h!=window||!dc||GetObjectType(dc)!=OBJ_MEMDC||GetWindowThreadProcessId(h,&pid)!=uiThread||pid!=GetCurrentProcessId()){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 RECT actual{};if(!GetClientRect(h,&actual)||actual.right!=640||actual.bottom!=480){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}
 State observed;{std::lock_guard<std::mutex> lock(stateMx);observed=state;}const U64 tick=GetTickCount64(),id=++captureId;const auto& p=observed.pad[0].Gamepad;const auto& e=observed.pad[1].Gamepad;
 HBRUSH brush=CreateSolidBrush(RGB(20,30,40));if(!brush)return FALSE;RECT client{0,0,640,480};const int filled=FillRect(dc,&client,brush);DeleteObject(brush);if(!filled)return FALSE;
 const char magic[]="MCPPAD01";for(int i=0;i<8;++i)WriteByte(dc,i,(BYTE)magic[i]);
 const U64 fields[]={1,id,tick,observed.token,observed.polls[0],observed.polls[1],observed.firstActive,observed.lastActive,observed.neutralAfter,observed.rc[0],observed.rc[1],p.wButtons,p.bLeftTrigger,p.bRightTrigger,(USHORT)p.sThumbLX,(USHORT)p.sThumbLY,(USHORT)p.sThumbRX,(USHORT)p.sThumbRY,observed.activeSamples[0],observed.activeSamples[1],observed.integralX,observed.integralY,(U64)displayRotation.load(),(U64)observed.displayX,(U64)observed.displayY,nativeReads.load(),e.wButtons,e.bLeftTrigger,e.bRightTrigger,(USHORT)e.sThumbLX,(USHORT)e.sThumbLY,(USHORT)e.sThumbRX,(USHORT)e.sThumbRY,observed.lastPoll[0],observed.lastPoll[1]};
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i)Write64(dc,(int)i,fields[i]);
 Log("{\"event\":\"capture\",\"capture_id\":%llu,\"case_token\":%llu,\"tick\":%llu,\"neutral_after\":%llu,\"first_active\":%llu,\"last_active\":%llu,\"native_printwindow_called\":false,\"source\":\"observed_XInput_memory_DC_model\"}",id,observed.token,tick,observed.neutralAfter,observed.firstActive,observed.lastActive);return TRUE;
}
LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_APP+0xB1){const unsigned action=(unsigned)w;const U64 token=(U64)(ULONG_PTR)l;bool ok=action==0;
  if(action==1){Poll(0);Poll(1);std::lock_guard<std::mutex> lock(stateMx);ok=state.rc[0]==ERROR_SUCCESS&&state.rc[1]==ERROR_SUCCESS&&!Active(state.pad[0])&&!Active(state.pad[1]);if(ok){state=State{};state.token=token;state.start=GetTickCount64();}}
  if(action==2){pollingPaused=true;coldRemaining=2;ok=true;}if(action==3){pollingPaused=false;ok=true;}if(action==4){coldRemaining=0;pollingPaused=false;ok=true;}
  if(action>=5&&action<=7){std::lock_guard<std::mutex> lock(stateMx);ok=!Active(state.pad[0])&&!Active(state.pad[1]);if(ok)displayRotation=action==5?180:action==7?90:0;}
  if(action==8){coldRemaining=0;pollingPaused=false;stopping=true;ok=true;}
  State observed;{std::lock_guard<std::mutex> lock(stateMx);observed=state;}
  Log("{\"event\":\"control\",\"action\":%u,\"token\":%llu,\"case_token\":%llu,\"ok\":%s,\"tick\":%llu,\"std_samples\":%llu,\"ex_samples\":%llu,\"std_active\":%llu,\"ex_active\":%llu,\"first_active\":%llu,\"last_active\":%llu,\"neutral_after\":%llu}",action,token,observed.token,Bool(ok),GetTickCount64(),observed.polls[0],observed.polls[1],observed.activeSamples[0],observed.activeSamples[1],observed.firstActive,observed.lastActive,observed.neutralAfter);
  Log("{\"event\":\"model\",\"token\":%llu,\"polling_paused\":%s,\"cold_remaining\":%u,\"native_reads\":%llu,\"rumble_calls\":%llu,\"enable_calls\":%llu,\"display_rotation\":%d,\"synthetic_display_x\":%lld,\"synthetic_display_y\":%lld}",token,Bool(pollingPaused),coldRemaining.load(),nativeReads.load(),rumbleCalls.load(),enableCalls.load(),displayRotation.load(),(long long)observed.displayX,(long long)observed.displayY);return 0;
 }
 if(message==WM_PAINT){PAINTSTRUCT paint{};BeginPaint(h,&paint);EndPaint(h,&paint);return 0;}if(message==WM_DESTROY)return 0;return DefWindowProcW(h,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);if(!argv||count!=2){if(argv)LocalFree(argv);return 2;}output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 bool safe=capture_spy::Install()&&capture_spy::Add("keybd_event",(void*)&TerminalKey)&&capture_spy::Add("GetAsyncKeyState",(void*)&NoAsync)&&capture_spy::Add("GetForegroundWindow",(void*)&OutsideForeground)&&capture_spy::Add("GetGUIThreadInfo",(void*)&OutsideGui)&&capture_spy::Add("GetCursorPos",(void*)&ReadPosition)&&capture_spy::Add("GetPhysicalCursorPos",(void*)&ReadPosition)&&capture_spy::Add("PrintWindow",(void*)&CaptureMemory);
 HMODULE xi=safe?LoadLibraryW(L"xinput1_4.dll"):nullptr;
 if(xi){readers[0]=(GetState)GetProcAddress(xi,"XInputGetState");readers[1]=(GetState)GetProcAddress(xi,MAKEINTRESOURCEA(100));safe=AddAddress((void*)readers[0],(void*)&NeutralNative)&&AddAddress((void*)readers[1],(void*)&NeutralNative)&&AddAddress((void*)GetProcAddress(xi,"XInputSetState"),(void*)&TerminalRumble)&&AddAddress((void*)GetProcAddress(xi,"XInputEnable"),(void*)&TerminalEnable);}else safe=false;
 Log("{\"event\":\"guards\",\"safe\":%s,\"terminal_desktop_writers\":19,\"terminal_rumble_enable\":true,\"rumble_enable_probed\":false,\"hardware_reader_called\":false,\"native_printwindow_called\":false}",Bool(safe));if(!safe)return 4;
 uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"ViControllerCaptureOwned";if(!RegisterClassW(&wc))return 5;
 const int screenX=GetSystemMetrics(SM_XVIRTUALSCREEN),screenY=GetSystemMetrics(SM_YVIRTUALSCREEN),screenW=GetSystemMetrics(SM_CXVIRTUALSCREEN),screenH=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 if(screenW<=0||screenH<=0||!(-26000LL+640<=screenX||-26000LL>=(LONGLONG)screenX+screenW||500LL+480<=screenY||500LL>=(LONGLONG)screenY+screenH))return 5;
 window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"owned XInput/capture model",WS_POPUP|WS_VISIBLE,-26000,500,640,480,nullptr,nullptr,instance,nullptr);if(!window)return 6;
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"actual_offscreen_checked\":true,\"width\":640,\"height\":480,\"capture_backend\":\"owned_memory_DC_PrintWindow_surrogate\",\"input_source\":\"actual_hooked_XInput_exports_over_disconnected_lower_source\"}",GetCurrentProcessId(),uiThread,(U64)(ULONG_PTR)window);
 const U64 end=GetTickCount64()+180000;while(!stopping&&GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(!pollingPaused){Poll(0);Poll(1);}Sleep(2);}
 DestroyWindow(window);{std::lock_guard<std::mutex> lock(logMx);FILE* old=output;output=nullptr;if(old)fclose(old);}return 0;
}
