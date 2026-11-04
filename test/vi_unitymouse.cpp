// Source-owned Unity-like polling consumer; no Unity/game engine is executed.
// Read spies fabricate native data; every desktop writer is terminal before HWND/DLL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using U64=unsigned long long;
FILE* output=nullptr;std::mutex logMx;
HWND window=nullptr;DWORD uiThread=0;bool stop=false,cachedActive=false,requireCached=false;
U64 caseToken=0,samples=0,downSamples=0,hits=0,firstDown=0,lastDown=0,upAfter=0,lastLog=0;
unsigned previousMask=0;std::atomic<U64> lowerKeys{0},lowerCursor{0},keyWrites{0};
constexpr int keys[5]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
const char* Bool(bool b){return b?"true":"false";}
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
SHORT WINAPI NativeKey(int vk){++lowerKeys;for(int key:keys)if(vk==key)return (SHORT)-32767;return 0;}
BOOL WINAPI NativeCursor(LPPOINT point){++lowerCursor;if(!point){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}*point=POINT{5000,6000};return TRUE;}
HWND WINAPI NativeForeground(){return GetDesktopWindow();}
HWND WINAPI NativeNullFocus(){return nullptr;}
BOOL WINAPI NativeGui(DWORD,PGUITHREADINFO info){if(!info||info->cbSize!=sizeof(*info)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO value{sizeof(value)};value.hwndActive=GetDesktopWindow();*info=value;return TRUE;}
void WINAPI NativeKeyWrite(BYTE,BYTE,DWORD,ULONG_PTR){++keyWrites;}
struct Observation {
 U64 tick=0;unsigned mask=0;USHORT raw[5]{};POINT screen{},client{},roundtrip{};
 bool cursorOk=false,clientOk=false,roundtripOk=false,point=false,foreground=false,active=false,focus=false,gate=false;
};
Observation Read(){Observation o;o.tick=GetTickCount64();for(unsigned i=0;i<5;++i){o.raw[i]=(USHORT)GetAsyncKeyState(keys[i]);if(o.raw[i]&0x8000)o.mask|=1u<<i;}
 o.cursorOk=GetCursorPos(&o.screen)!=FALSE;o.client=o.screen;o.clientOk=o.cursorOk&&ScreenToClient(window,&o.client)!=FALSE;o.roundtrip=o.client;o.roundtripOk=o.clientOk&&ClientToScreen(window,&o.roundtrip)!=FALSE&&o.roundtrip.x==o.screen.x&&o.roundtrip.y==o.screen.y;
 o.point=o.clientOk&&o.client.x>=120&&o.client.x<220&&o.client.y>=80&&o.client.y<180;
 o.foreground=GetForegroundWindow()==window;o.active=GetActiveWindow()==window;o.focus=GetFocus()==window;
 o.gate=o.foreground&&o.active&&o.focus&&(!requireCached||cachedActive);return o;
}
Observation latest{};
void Record(const Observation& o,const char* event,U64 token){Log("{\"event\":\"%s\",\"token\":%llu,\"case_token\":%llu,\"tick\":%llu,\"sample\":%llu,\"mask\":%u,\"raw\":[%u,%u,%u,%u,%u],\"cursor_ok\":%s,\"client_ok\":%s,\"roundtrip_ok\":%s,\"screen_x\":%ld,\"screen_y\":%ld,\"client_x\":%ld,\"client_y\":%ld,\"point_inside\":%s,\"foreground\":%s,\"active\":%s,\"focus\":%s,\"cached_active\":%s,\"requires_cached\":%s,\"gate\":%s,\"down_samples\":%llu,\"hits\":%llu,\"first_down\":%llu,\"last_down\":%llu,\"up_after\":%llu,\"lower_key_reads\":%llu,\"lower_cursor_reads\":%llu}",event,token,caseToken,o.tick,samples,o.mask,(unsigned)o.raw[0],(unsigned)o.raw[1],(unsigned)o.raw[2],(unsigned)o.raw[3],(unsigned)o.raw[4],Bool(o.cursorOk),Bool(o.clientOk),Bool(o.roundtripOk),o.screen.x,o.screen.y,o.client.x,o.client.y,Bool(o.point),Bool(o.foreground),Bool(o.active),Bool(o.focus),Bool(cachedActive),Bool(requireCached),Bool(o.gate),downSamples,hits,firstDown,lastDown,upAfter,lowerKeys.load(),lowerCursor.load());}
void Poll(){const auto o=Read();++samples;if(o.mask){++downSamples;if(!firstDown)firstDown=o.tick;lastDown=o.tick;}if(previousMask&&!o.mask)upAfter=o.tick;if((o.mask&~previousMask)&&o.point&&o.gate)++hits;
 const bool emit=o.mask!=latest.mask||o.gate!=latest.gate||o.client.x!=latest.client.x||o.client.y!=latest.client.y||o.tick-lastLog>=100;
 previousMask=o.mask;if(emit){Record(o,"sample",0);lastLog=o.tick;}latest=o;
}
DWORD WINAPI Worker(void* pointer){const U64 token=(U64)(ULONG_PTR)pointer;Log("{\"event\":\"worker_focus\",\"token\":%llu,\"thread\":%lu,\"foreground_is_target\":%s,\"active_is_null\":%s,\"focus_is_null\":%s}",token,GetCurrentThreadId(),Bool(GetForegroundWindow()==window),Bool(GetActiveWindow()==nullptr),Bool(GetFocus()==nullptr));return 0;}
LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_ACTIVATEAPP){cachedActive=w!=0;Log("{\"event\":\"activateapp\",\"tick\":%llu,\"active\":%s,\"thread\":%lu}",GetTickCount64(),Bool(cachedActive),GetCurrentThreadId());}
 if(message==WM_APP+0xB2){const U64 token=(U64)(ULONG_PTR)l;bool ok=true;
  if(w==1||w==2||w==3){caseToken=token;samples=downSamples=hits=firstDown=lastDown=upAfter=0;previousMask=0;requireCached=w!=1;if(w==3){cachedActive=false;Log("{\"event\":\"owned_cached_flag_reset\",\"token\":%llu,\"native_activation_called\":false}",token);}}
  else if(w==4){HANDLE worker=CreateThread(nullptr,0,Worker,(void*)(ULONG_PTR)token,0,nullptr);ok=worker&&WaitForSingleObject(worker,1000)==WAIT_OBJECT_0;if(worker)CloseHandle(worker);}
  else if(w==5)ok=PostMessageW(h,WM_ACTIVATEAPP,FALSE,0)!=FALSE;
  else if(w==8)stop=true;else if(w!=0)ok=false;
  Poll();Record(latest,"snapshot",token);Log("{\"event\":\"control\",\"action\":%llu,\"token\":%llu,\"case_token\":%llu,\"ok\":%s}",(U64)w,token,caseToken,Bool(ok));return 0;
 }
 if(message==WM_CLOSE){stop=true;return 0;}if(message==WM_DESTROY){stop=true;return 0;}return DefWindowProcW(h,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc!=2){if(argv)LocalFree(argv);return 2;}output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 const bool safe=capture_spy::Install()&&capture_spy::Add("keybd_event",(void*)&NativeKeyWrite)&&capture_spy::Add("GetAsyncKeyState",(void*)&NativeKey)&&capture_spy::Add("GetCursorPos",(void*)&NativeCursor)&&capture_spy::Add("GetPhysicalCursorPos",(void*)&NativeCursor)&&capture_spy::Add("GetForegroundWindow",(void*)&NativeForeground)&&capture_spy::Add("GetActiveWindow",(void*)&NativeNullFocus)&&capture_spy::Add("GetFocus",(void*)&NativeNullFocus)&&capture_spy::Add("GetGUIThreadInfo",(void*)&NativeGui);
 Log("{\"event\":\"guards\",\"safe\":%s,\"terminal_desktop_writers\":19,\"physical_reader_source\":\"fabricated\",\"native_writer_called\":false,\"writer_restoration\":false}",Bool(safe));if(!safe)return 4;
 uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"ViUnityMouseOwned";if(!RegisterClassW(&wc))return 5;
 const int sx=GetSystemMetrics(SM_XVIRTUALSCREEN),sy=GetSystemMetrics(SM_YVIRTUALSCREEN),sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 if(sw<=0||sh<=0||!(-26000LL+640<=sx||-26000LL>=(LONGLONG)sx+sw||500LL+480<=sy||500LL>=(LONGLONG)sy+sh))return 5;
 window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"owned Unity-like mouse consumer",WS_POPUP|WS_VISIBLE,-26000,500,640,480,nullptr,nullptr,instance,nullptr);if(!window)return 6;
 const auto before=Read();Record(before,"before_dll",0);
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"actual_offscreen_checked\":true,\"width\":640,\"height\":480,\"input_source\":\"actual_hooked_mouse_readers_over_fabricated_native_source\"}",GetCurrentProcessId(),uiThread,(U64)(ULONG_PTR)window);
 const U64 end=GetTickCount64()+120000;while(!stop&&GetTickCount64()<end){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Poll();Sleep(2);}
 DestroyWindow(window);Log("{\"event\":\"exit\",\"desktop_writes_absorbed\":%llu,\"keybd_writes_absorbed\":%llu,\"native_writer_called\":false}",capture_spy::writes.load(),keyWrites.load());{std::lock_guard<std::mutex> lock(logMx);FILE* old=output;output=nullptr;if(old)fclose(old);}return 0;
}
