// Owned radar plant. Pixels move ONLY when actual hooked keyboard readers
// return high state. Native PrintWindow, physical input and foreground writers
// are never called. This is scheduler/sensor evidence, not a game simulation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using U64=unsigned long long;constexpr UINT ControlMessage=WM_APP+0xB9;constexpr int Keys[4]={'W','A','D','J'};
constexpr const wchar_t* ClassName=L"ViRadarOwned11";constexpr const wchar_t* Lifetime=L"vinput.radar.fixture.lifetime.v1";
constexpr double Pi=3.14159265358979323846;int lifetimeToken=0;HWND target=nullptr;DWORD uiThread=0;FILE* output=nullptr;bool exiting=false;std::mutex stateMutex,logMutex;std::atomic<U64> nextSerial{0};
struct State {U64 epoch=1,lastTick=0,serial=0,polls=0,captures=0,lastToken=0,edges=0,dropped=0;unsigned mode=1,mask=0,gks=0,gkbs=0,wmMask=0;bool keyboardOk=false,overlap=false;double bearing=45,radius=46;std::array<U64,4> high{},down{},up{},wmDown{},wmUp{};};State state;
const char* Bool(bool value){return value?"true":"false";}
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMutex);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
bool Own(HWND h){DWORD pid=0;return h&&h==target&&GetWindowThreadProcessId(h,&pid)==uiThread&&pid==GetCurrentProcessId()&&GetPropW(h,Lifetime)==reinterpret_cast<HANDLE>(&lifetimeToken);}
State Copy(){std::lock_guard<std::mutex> lock(stateMutex);return state;}
bool Writable(void* value,SIZE_T count){if(!value)return false;const auto begin=reinterpret_cast<std::uintptr_t>(value);if(count>UINTPTR_MAX-begin)return false;MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(value,&region,sizeof(region))||region.State!=MEM_COMMIT||(region.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;const auto end=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;return end>=begin&&count<=end-begin&&((region.Protect&0xff)==PAGE_READWRITE||(region.Protect&0xff)==PAGE_WRITECOPY||(region.Protect&0xff)==PAGE_EXECUTE_READWRITE||(region.Protect&0xff)==PAGE_EXECUTE_WRITECOPY);}
SHORT WINAPI NoKey(int){return 0;}
BOOL WINAPI NoKeyboard(PBYTE value){if(!Writable(value,256))return FALSE;__try{memset(value,0,256);return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){return FALSE;}}
HWND WINAPI Outside(){return GetDesktopWindow();}HWND WINAPI NullFocus(){return nullptr;}
BOOL WINAPI OutsideGui(DWORD,PGUITHREADINFO value){if(!Writable(value,sizeof(*value)))return FALSE;__try{if(value->cbSize!=sizeof(*value))return FALSE;GUITHREADINFO out{sizeof(out)};out.hwndActive=GetDesktopWindow();*value=out;return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){return FALSE;}}
BOOL WINAPI FakeCursor(LPPOINT value){if(!Writable(value,sizeof(*value)))return FALSE;__try{*value=POINT{5000,6000};return TRUE;}__except(EXCEPTION_EXECUTE_HANDLER){return FALSE;}}
void WINAPI KeyWriter(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
using Position=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);Position originalPosition=nullptr;thread_local bool layoutToken=false;std::atomic<U64> layoutCalls{0};
BOOL WINAPI BoundedLayout(HWND h,HWND after,int x,int y,int width,int height,UINT flags){
 const UINT required=SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER;RECT before{};
 if(layoutToken&&Own(h)&&GetCurrentThreadId()==uiThread&&originalPosition&&GetWindowRect(h,&before)&&before.right<-20000&&x==-26000&&y==600&&(width==640||width==648)&&height==480&&(flags&required)==required&&!(flags&(SWP_SHOWWINDOW|SWP_HIDEWINDOW|SWP_ASYNCWINDOWPOS))){++layoutCalls;return originalPosition(h,after,x,y,width,height,flags);}
 ++capture_spy::writes;return TRUE;
}
bool Resize(unsigned width){if(!Own(target))return false;layoutToken=true;const bool ok=SetWindowPos(target,nullptr,-26000,600,(int)width,480,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER)!=FALSE;layoutToken=false;RECT after{};return ok&&GetClientRect(target,&after)&&after.right==(LONG)width&&after.bottom==480;}
bool Poll(){
 const U64 epoch=Copy().epoch,serial=++nextSerial,now=GetTickCount64();unsigned mask=0,gks=0,gkbs=0;BYTE keyboard[256]{};
 for(unsigned i=0;i<4;++i){if(GetAsyncKeyState(Keys[i])&0x8000)mask|=1u<<i;if(GetKeyState(Keys[i])&0x8000)gks|=1u<<i;}const bool kb=GetKeyboardState(keyboard)!=FALSE;if(kb)for(unsigned i=0;i<4;++i)if(keyboard[Keys[i]]&0x80)gkbs|=1u<<i;
 State observed;unsigned changed=0;bool emit=false;
 {std::lock_guard<std::mutex> lock(stateMutex);if(epoch!=state.epoch)return false;if(serial<=state.serial)return true;
  const U64 elapsed=state.lastTick&&now>=state.lastTick?(std::min)(20ULL,now-state.lastTick):0;changed=state.mask^mask;state.mask=mask;state.gks=gks;state.gkbs=gkbs;state.keyboardOk=kb;state.lastTick=now;state.serial=serial;++state.polls;state.overlap|=(mask&6)==6;
  for(unsigned i=0;i<4;++i){if(mask&(1u<<i))++state.high[i];if(changed&(1u<<i)){if(mask&(1u<<i))++state.down[i];else ++state.up[i];}}
  // No frame/capture ordinal or timer changes a bearing/distance. Only a high
  // returned by the injected GAKS hook can drive this owned pixel plant.
  if(state.mode==1||state.mode==2){if((mask&6)==2)state.bearing+=.08*(double)elapsed;if((mask&6)==4)state.bearing-=.08*(double)elapsed;while(state.bearing>180)state.bearing-=360;while(state.bearing<=-180)state.bearing+=360;if(state.mode==1&&(mask&1))state.radius=(std::max)(4.0,state.radius-.035*(double)elapsed);}
  if(changed){if(state.edges++<512)emit=true;else ++state.dropped;}observed=state;
 }
 if(emit)Log("{\"event\":\"poll_edge\",\"epoch\":%llu,\"tick\":%llu,\"serial\":%llu,\"mask\":%u,\"changed\":%u,\"bearing\":%.6f,\"radius\":%.6f}",observed.epoch,now,serial,mask,changed,observed.bearing,observed.radius);return true;
}
void Snapshot(U64 token,const char* event="snapshot"){
 const auto s=Copy();Log("{\"event\":\"%s\",\"token\":%llu,\"epoch\":%llu,\"mode\":%u,\"mask\":%u,\"gks_mask\":%u,\"gkbs_mask\":%u,\"gkbs_ok\":%s,\"bearing\":%.6f,\"radius\":%.6f,\"polls\":%llu,\"captures\":%llu,\"a_d_overlap\":%s,\"records_dropped\":%llu,\"terminal_calls\":%llu,\"layout_calls\":%llu}",event,token,s.epoch,s.mode,s.mask,s.gks,s.gkbs,Bool(s.keyboardOk),s.bearing,s.radius,s.polls,s.captures,Bool(s.overlap),s.dropped,capture_spy::writes.load(),layoutCalls.load());
 for(unsigned i=0;i<4;++i)Log("{\"event\":\"key_snapshot\",\"token\":%llu,\"epoch\":%llu,\"vk\":%d,\"high\":%llu,\"down\":%llu,\"up\":%llu,\"wm_down\":%llu,\"wm_up\":%llu}",token,s.epoch,Keys[i],s.high[i],s.down[i],s.up[i],s.wmDown[i],s.wmUp[i]);
}
BOOL WINAPI Print(HWND h,HDC dc,UINT){
 if(!Own(h)||GetObjectType(dc)!=OBJ_MEMDC)return FALSE;if(!Poll())return FALSE;U64 count=0;unsigned mode=0;{std::lock_guard<std::mutex> lock(stateMutex);count=++state.captures;mode=state.mode;}
 if(mode==9&&count==2){Log("{\"event\":\"capture_stall\",\"epoch\":%llu,\"capture\":%llu,\"delay_ms\":1500}",Copy().epoch,count);Sleep(1500);Poll();}
 if(mode==10&&count==2){DWORD_PTR reply=0;if(!SendMessageTimeoutW(h,ControlMessage,90,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&reply)||!reply)return FALSE;}
 if(mode==12||(mode==8&&count>=2)){Log("{\"event\":\"capture_failure\",\"epoch\":%llu,\"capture\":%llu,\"native_printwindow_called\":false}",Copy().epoch,count);SetLastError(ERROR_RETRY);return FALSE;}
 const auto s=Copy();RECT client{};if(!GetClientRect(h,&client))return FALSE;LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);HBRUSH background=CreateSolidBrush(RGB(12,24,36));if(!background)return FALSE;const bool filled=FillRect(dc,&client,background)!=0;DeleteObject(background);if(!filled)return FALSE;
 const bool missing=mode==4||(mode==6&&count>=2),ambiguous=mode==5||(mode==7&&count>=2);
 const int x=100+64+(int)std::llround(std::sin(s.bearing*Pi/180)*s.radius),y=100+64-(int)std::llround(std::cos(s.bearing*Pi/180)*s.radius);
 if(!missing){HBRUSH marker=CreateSolidBrush(RGB(0,255,0));if(!marker)return FALSE;RECT dot{x-1,y-1,x+2,y+2};FillRect(dc,&dot,marker);if(ambiguous){RECT other{105,105,108,108};FillRect(dc,&other,marker);}DeleteObject(marker);}
 GdiFlush();Log("{\"event\":\"capture\",\"epoch\":%llu,\"capture\":%llu,\"mode\":%u,\"qpc\":\"%lld\",\"mask\":%u,\"bearing\":%.6f,\"radius\":%.6f,\"dot_x\":%d,\"dot_y\":%d,\"dot_pixels\":9,\"missing\":%s,\"ambiguous\":%s,\"source\":\"actual hooked GAKS-driven owned pixel plant; memory DC only\",\"native_printwindow_called\":false}",s.epoch,count,mode,qpc.QuadPart,s.mask,s.bearing,s.radius,x,y,Bool(missing),Bool(ambiguous));return TRUE;
}
LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;for(unsigned i=0;i<4;++i)if(w==(WPARAM)Keys[i]){U64 epoch=0;{std::lock_guard<std::mutex> lock(stateMutex);if(down){++state.wmDown[i];state.wmMask|=1u<<i;}else{++state.wmUp[i];state.wmMask&=~(1u<<i);}epoch=state.epoch;}Log("{\"event\":\"key_message\",\"epoch\":%llu,\"tick\":%llu,\"vk\":%d,\"down\":%s}",epoch,GetTickCount64(),Keys[i],Bool(down));break;}}
 if(message==ControlMessage){const U64 token=(U64)(ULONG_PTR)l;bool ok=Own(h);Poll();if(ok&&w>=1&&w<=13){std::lock_guard<std::mutex> lock(stateMutex);if(state.mask&7)ok=false;else{const auto next=state.epoch+1;const auto manual=state.mask&8;state=State{};state.epoch=next;state.mode=(unsigned)w;state.mask=manual;state.lastTick=GetTickCount64();if(w==3)state.bearing=0;if(w==11||w==13){state.bearing=0;state.radius=w==13?0:4;}}}else if(ok&&(w==90||w==91))ok=Resize(w==90?648:640);else if(ok&&w==98)exiting=true;else if(w!=0)ok=false;{std::lock_guard<std::mutex> lock(stateMutex);state.lastToken=token;}Snapshot(token);Log("{\"event\":\"control\",\"action\":%llu,\"token\":%llu,\"ok\":%s}",(U64)w,token,Bool(ok));return ok?1:0;}
 if(message==WM_CLOSE||message==WM_DESTROY){exiting=true;return 0;}return DefWindowProcW(h,message,w,l);
}
bool Verify(){for(const auto& hook:capture_spy::hooks)if(!capture_spy::Reaches(hook.entry,hook.detour))return false;return true;}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||(count!=2&&count!=3)){if(args)LocalFree(args);return 2;}
 if(count==3){HANDLE gate=OpenEventW(SYNCHRONIZE,FALSE,args[2]);if(!gate){LocalFree(args);return 2;}const DWORD waited=WaitForSingleObject(gate,10000);CloseHandle(gate);if(waited!=WAIT_OBJECT_0){LocalFree(args);return 2;}}
 output=_wfsopen(args[1],L"w",_SH_DENYNO);LocalFree(args);if(!output)return 3;
 const bool writers=capture_spy::Install(BoundedLayout)&&capture_spy::Add("keybd_event",reinterpret_cast<void*>(&KeyWriter));for(const auto& hook:capture_spy::hooks)if(hook.entry==reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowPos")))originalPosition=reinterpret_cast<Position>(hook.original);
 const bool safe=writers&&originalPosition&&capture_spy::Add("GetAsyncKeyState",reinterpret_cast<void*>(&NoKey))&&capture_spy::Add("GetKeyState",reinterpret_cast<void*>(&NoKey))&&capture_spy::Add("GetKeyboardState",reinterpret_cast<void*>(&NoKeyboard))&&capture_spy::Add("GetForegroundWindow",reinterpret_cast<void*>(&Outside))&&capture_spy::Add("GetGUIThreadInfo",reinterpret_cast<void*>(&OutsideGui))&&capture_spy::Add("GetActiveWindow",reinterpret_cast<void*>(&NullFocus))&&capture_spy::Add("GetFocus",reinterpret_cast<void*>(&NullFocus))&&capture_spy::Add("GetCursorPos",reinterpret_cast<void*>(&FakeCursor))&&capture_spy::Add("GetPhysicalCursorPos",reinterpret_cast<void*>(&FakeCursor))&&capture_spy::Add("PrintWindow",reinterpret_cast<void*>(&Print))&&Verify();
 Log("{\"event\":\"guards\",\"safe\":%s,\"terminal_desktop_writers\":19,\"lower_keys\":\"fabricated all up\",\"lower_focus\":\"fabricated outside/null\",\"native_writer_called\":false,\"native_printwindow_called\":false}",Bool(safe));if(!safe)return 4;
 WNDCLASSW cls{};cls.lpfnWndProc=Window;cls.hInstance=instance;cls.lpszClassName=ClassName;if(!RegisterClassW(&cls))return 5;uiThread=GetCurrentThreadId();
 const LONGLONG sx=GetSystemMetrics(SM_XVIRTUALSCREEN),sy=GetSystemMetrics(SM_YVIRTUALSCREEN),sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(sw<=0||sh<=0||!(-26000LL+648<=sx||-26000LL>=sx+sw||600LL+480<=sy||600LL>=sy+sh))return 5;
 target=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,ClassName,L"owned radar consumer",WS_POPUP|WS_VISIBLE,-26000,600,640,480,nullptr,nullptr,instance,nullptr);if(!target||!SetPropW(target,Lifetime,reinterpret_cast<HANDLE>(&lifetimeToken))||!Own(target))return 6;Poll();
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"client_w\":640,\"client_h\":480,\"actual_offscreen_checked\":true,\"control_message\":%u,\"radar_region\":\"100,100,128,128\",\"image_source\":\"memory-DC known source; position changes only from actual hooked GAKS highs\"}",GetCurrentProcessId(),uiThread,(U64)(ULONG_PTR)target,ControlMessage);
 const U64 end=GetTickCount64()+180000;while(!exiting&&GetTickCount64()<end){MSG m{};unsigned drained=0;while(drained++<256&&PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){exiting=true;break;}TranslateMessage(&m);DispatchMessageW(&m);}Poll();Sleep(2);}Poll();Snapshot(0,"final");DestroyWindow(target);target=nullptr;Log("{\"event\":\"exit\",\"terminal_calls\":%llu,\"native_writer_called\":false}",capture_spy::writes.load());fclose(output);output=nullptr;return 0;
}
