// KNOWN-SOURCE PRINTWINDOW API SURROGATE. Native PrintWindow compatibility is UNKNOWN.
// Add-only derivative of vi_capture_diagnostics.cpp SHA256
// F3560591BC9D1A5D7D12415C8ACEAA0DDECE7C7CB1850D3B69BACC96F0E0CB4F.
// The immutable native baseline remains112 PASS/14 FAIL; no result is replaced here.
// This is neither a native PrintWindow test nor a DDraw/D3D9 wrapper emulator.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <share.h>
#include <cwchar>
#include "vi_capture_diagnostic_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
FILE* output=nullptr;HWND mainWindow=nullptr,proxyWindow=nullptr,secondWindow=nullptr;HINSTANCE module=nullptr;
bool nonblack=false;unsigned customText=0;
std::mutex outputMutex;
std::atomic<HWND> surrogateWindows[3]{};
std::atomic<unsigned long long> surrogateAccepted{0},surrogateRejected{0};
void Log(const char* format,...){std::lock_guard<std::mutex> lock(outputMutex);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
void Paint(HWND window,HDC dc){
 RECT client{};GetClientRect(window,&client);const bool proxy=window!=mainWindow;
 HBRUSH black=CreateSolidBrush(RGB(0,0,0));FillRect(dc,&client,black);DeleteObject(black);
 if(proxy||nonblack){
  for(LONG y=0;y<client.bottom;y+=40)for(LONG x=0;x<client.right;x+=40){RECT tile{x,y,(std::min)(x+40,client.right),(std::min)(y+40,client.bottom)};
   HBRUSH color=CreateSolidBrush(((x/40+y/40)&1)?RGB(25,60,230):RGB(220,35,20));FillRect(dc,&tile,color);DeleteObject(color);}
  RECT marker{4,4,24,24};HBRUSH green=CreateSolidBrush(RGB(20,220,30));FillRect(dc,&marker,green);DeleteObject(green);
 }
}
bool SurrogateOwnWindow(HWND window,DWORD& thread){
 bool listed=false;for(const auto& own:surrogateWindows)if(window&&own.load()==window)listed=true;
 DWORD pid=0;thread=window?GetWindowThreadProcessId(window,&pid):0;
 return listed&&thread&&pid==GetCurrentProcessId()&&IsWindow(window);
}
BOOL WINAPI KnownSourcePrintWindow(HWND window,HDC dc,UINT flags){
 DWORD thread=0;const bool owned=SurrogateOwnWindow(window,thread);
 DWORD actualPid=0;if(window)GetWindowThreadProcessId(window,&actualPid);
 const DWORD dcType=dc?GetObjectType(dc):0;const bool memoryDc=dc&&dcType==OBJ_MEMDC;
 RECT client{};const bool dimensions=owned&&GetClientRect(window,&client)&&client.right>0&&client.bottom>0&&client.right<=4096&&client.bottom<=4096;
 if(!owned||!memoryDc||!dimensions){++surrogateRejected;
  Log("{\"kind\":\"printwindow_surrogate\",\"capture_transport\":\"known_source_printwindow_api_surrogate\",\"window\":%llu,\"owner_pid\":%lu,\"owned_live_window\":%s,\"dc_object_type\":%lu,\"memory_dc\":%s,\"flags\":%u,\"accepted\":false,\"native_printwindow_called\":false}",(unsigned long long)(ULONG_PTR)window,actualPid,owned?"true":"false",dcType,memoryDc?"true":"false",flags);SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 // GetObjectType==OBJ_MEMDC is mandatory BEFORE any deterministic painting.
 // Never call a native PrintWindow trampoline or acquire a screen/window DC here.
 Paint(window,dc);
 DWORD afterThread=0;const bool stable=SurrogateOwnWindow(window,afterThread)&&afterThread==thread&&GetObjectType(dc)==OBJ_MEMDC;
 if(stable)++surrogateAccepted;else ++surrogateRejected;
 Log("{\"kind\":\"printwindow_surrogate\",\"capture_transport\":\"known_source_printwindow_api_surrogate\",\"window\":%llu,\"owner_pid\":%lu,\"owned_live_window\":true,\"dc_object_type\":%lu,\"memory_dc\":true,\"flags\":%u,\"accepted\":%s,\"native_printwindow_called\":false}",(unsigned long long)(ULONG_PTR)window,GetCurrentProcessId(),dcType,flags,stable?"true":"false");
 if(!stable)SetLastError(ERROR_INVALID_WINDOW_HANDLE);return stable?TRUE:FALSE;
}

// A disabled ordinary-MinHook record supplies a pre-DLL original trampoline.
// It is NEVER enabled, queued, removed, or published as a general native API.
// All 18 physical/foreground/window-write absorbers remain terminal.
using NativeCreateWindow=HWND (WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
NativeCreateWindow replacementCreate=nullptr;
DWORD fixtureUiThread=0;
bool replacementCreatePrepared=false;
unsigned replacementCreateCalls=0;
LRESULT CALLBACK Window(HWND,UINT,WPARAM,LPARAM);
HWND WINAPI DisabledCreateReject(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
bool PrepareFixedReplacementConstructor(){
 void* entry=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"CreateWindowExW"));
 unsigned char before[16]{},after[16]{};void* original=nullptr;
 if(!entry||!capture_spy::Read(entry,before,sizeof(before))||MH_CreateHook(entry,reinterpret_cast<void*>(&DisabledCreateReject),&original)!=MH_OK||!original||!capture_spy::Read(entry,after,sizeof(after))||memcmp(before,after,sizeof(before))!=0)return false;
 replacementCreate=reinterpret_cast<NativeCreateWindow>(original);replacementCreatePrepared=true;
 Log("{\"kind\":\"replacement_constructor_prepared\",\"ui_thread\":%lu,\"pre_dll_trampoline\":true,\"hook_enabled\":false,\"entry_bytes_unchanged\":true,\"native_create_called\":false}",fixtureUiThread);return true;
}
HWND CreateFixedOwnedReplacement(){
 // No caller-supplied class, title, dimensions, styles, parent, or callback.
 // Refuse if an unusual desktop actually covers this fixed offscreen rectangle.
 const LONGLONG x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
 const LONGLONG w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 const bool outside=w>0&&h>0&&(-25000LL+640<=x||-25000LL>=x+w||900LL+480<=y||900LL>=y+h);
 DWORD pid=0;const DWORD mainThread=GetWindowThreadProcessId(mainWindow,&pid);WNDCLASSW klass{};
 const bool valid=replacementCreatePrepared&&replacementCreate&&!proxyWindow&&module&&GetCurrentThreadId()==fixtureUiThread&&mainWindow&&IsWindow(mainWindow)&&pid==GetCurrentProcessId()&&mainThread==fixtureUiThread&&GetClassInfoW(module,L"D3DProxyWindow",&klass)&&klass.hInstance==module&&klass.lpfnWndProc==Window&&outside;
 if(!valid){Log("{\"kind\":\"replacement_constructor\",\"admitted\":false,\"native_create_called\":false,\"fixed_rectangle_outside_virtual_screen\":%s}",outside?"true":"false");SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
 ++replacementCreateCalls;
 HWND result=replacementCreate(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"D3DProxyWindow",L"Synthetic proxy replacement",WS_POPUP|WS_VISIBLE,-25000,900,640,480,nullptr,nullptr,module,nullptr);
 Log("{\"kind\":\"replacement_constructor\",\"admitted\":true,\"native_create_called\":true,\"fixed_rectangle_outside_virtual_screen\":true,\"noactivate_style\":true,\"fixture_explicit_native_show_or_activation_called\":false,\"hwnd\":%llu,\"call_count\":%u}",(unsigned long long)(ULONG_PTR)result,replacementCreateCalls);return result;
}

HWND Proxy(const wchar_t* title,int width,int height,bool visible){return CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"D3DProxyWindow",title,WS_POPUP|(visible?WS_VISIBLE:0),-25000,900,width,height,nullptr,nullptr,module,nullptr);}

// Observation only, called on the existing snapshot/control UI thread. Never
// shows, moves, creates, activates or modifies the observed proxy window.
std::string ObserverJsonText(const std::string& text){
 std::string out="\"";for(char raw:text){const auto c=(unsigned char)raw;if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c<32){char hex[7];sprintf_s(hex,"\\u%04X",(unsigned)c);out+=hex;}else out+=(char)c;}return out+'"';
}
std::string ObserveProxy(){
 const HWND h=proxyWindow;DWORD pid=0;const DWORD thread=h?GetWindowThreadProcessId(h,&pid):0;
 const bool live=h&&IsWindow(h);RECT client{},rect{};const bool clientOk=live&&GetClientRect(h,&client),rectOk=live&&GetWindowRect(h,&rect);
 const bool visible=live&&IsWindowVisible(h);const DWORD style=live?(DWORD)GetWindowLongPtrW(h,GWL_STYLE):0,exstyle=live?(DWORD)GetWindowLongPtrW(h,GWL_EXSTYLE):0;
 wchar_t wide[128]{};const int characters=live?GetClassNameW(h,wide,128):0;char utf8[512]{};const int bytes=characters>0?WideCharToMultiByte(CP_UTF8,0,wide,characters,utf8,512,nullptr,nullptr):0;
 const std::string klass=bytes>0?std::string(utf8,(size_t)bytes):std::string();const auto quoted=ObserverJsonText(klass);char text[4096];
 sprintf_s(text,"{\"hwnd\":%llu,\"is_window\":%s,\"visible\":%s,\"style\":%lu,\"exstyle\":%lu,\"style_visible\":%s,\"noactivate\":%s,\"client_ok\":%s,\"client_w\":%ld,\"client_h\":%ld,\"rect_ok\":%s,\"left\":%ld,\"top\":%ld,\"right\":%ld,\"bottom\":%ld,\"pid\":%lu,\"thread\":%lu,\"query_thread\":%lu,\"on_proxy_ui_thread\":%s,\"class_available\":%s,\"class\":%s,\"class_encoding\":\"UTF8_from_GetClassNameW\",\"class_may_be_truncated\":%s}",(unsigned long long)(ULONG_PTR)h,live?"true":"false",visible?"true":"false",style,exstyle,(style&WS_VISIBLE)?"true":"false",(exstyle&WS_EX_NOACTIVATE)?"true":"false",clientOk?"true":"false",client.right,client.bottom,rectOk?"true":"false",rect.left,rect.top,rect.right,rect.bottom,pid,thread,GetCurrentThreadId(),thread&&thread==GetCurrentThreadId()?"true":"false",bytes>0?"true":"false",quoted.c_str(),characters==127?"true":"false");return text;
}

LRESULT WindowCommon(HWND window,UINT message,WPARAM w,LPARAM l,bool ansi){
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_PRINT||message==WM_PRINTCLIENT){Paint(window,(HDC)w);return 0;}
 if(message==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);Paint(window,dc);EndPaint(window,&paint);return 0;}
 if(message==WM_GETTEXT){++customText;return ansi?DefWindowProcA(window,message,w,l):DefWindowProcW(window,message,w,l);}
 if(message==WM_APP+0x90){
  if(w==1&&proxyWindow){surrogateWindows[1]=nullptr;DestroyWindow(proxyWindow);proxyWindow=nullptr;}
  if(w==2&&!proxyWindow){proxyWindow=CreateFixedOwnedReplacement();surrogateWindows[1]=proxyWindow;}
  const auto observation=ObserveProxy();
  Log("{\"kind\":\"snapshot\",\"token\":%llu,\"proxy\":%llu,\"custom_wm_gettext\":%u,\"absorbed_writes\":%llu,\"capture_transport\":\"known_source_printwindow_api_surrogate\",\"native_printwindow_compatibility_measured\":false,\"surrogate_accepted\":%llu,\"surrogate_rejected\":%llu,\"proxy_observation\":%s}",(unsigned long long)(ULONG_PTR)l,(unsigned long long)(ULONG_PTR)proxyWindow,customText,capture_spy::writes.load(),surrogateAccepted.load(),surrogateRejected.load(),observation.c_str());return 0;
 }
 if(message==WM_DESTROY){if(window==mainWindow)PostQuitMessage(0);return 0;}
 return ansi?DefWindowProcA(window,message,w,l):DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){return WindowCommon(window,message,w,l,false);}
LRESULT CALLBACK AnsiWindow(HWND window,UINT message,WPARAM w,LPARAM l){return WindowCommon(window,message,w,l,true);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=3){if(args)LocalFree(args);return 2;}
 output=_wfsopen(args[1],L"w",_SH_DENYNO);const std::wstring mode=args[2];LocalFree(args);if(!output)return 2;
 if(!capture_spy::Install()){Log("{\"kind\":\"fatal\",\"stage\":\"terminal_spy_installation\"}");return 3;}
 module=instance;nonblack=mode==L"nonblack";fixtureUiThread=GetCurrentThreadId();
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"GNW95 Class";
 if(!RegisterClassW(&wc))return 4;wc.lpszClassName=L"D3DProxyWindow";if(!RegisterClassW(&wc))return 4;
 if(!PrepareFixedReplacementConstructor()){Log("{\"kind\":\"fatal\",\"stage\":\"disabled_replacement_constructor\"}");return 7;}
 if(mode==L"ansi"){
  WNDCLASSA narrow{};narrow.hInstance=instance;narrow.lpfnWndProc=AnsiWindow;narrow.lpszClassName="GNW95 ANSI Class";if(!RegisterClassA(&narrow))return 4;
  mainWindow=CreateWindowExA(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,narrow.lpszClassName,"Synthetic GNW95 input",WS_POPUP|WS_VISIBLE,-25000,500,320,240,nullptr,nullptr,instance,nullptr);
 }else mainWindow=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"GNW95 Class",L"Synthetic GNW95 input",WS_POPUP|WS_VISIBLE,-25000,500,320,240,nullptr,nullptr,instance,nullptr);
 if(!mainWindow)return 4;
 if(mode!=L"none")proxyWindow=Proxy(mode==L"foreign"?L"Foreign process proxy":L"Synthetic visible proxy",mode==L"foreign"?960:640,mode==L"foreign"?720:480,mode!=L"hidden");
 if(mode!=L"none"&&!proxyWindow)return 4;
 if(mode==L"two"){secondWindow=Proxy(L"Synthetic second proxy",480,360,true);if(!secondWindow)return 4;}
 surrogateWindows[0]=mainWindow;surrogateWindows[1]=proxyWindow;surrogateWindows[2]=secondWindow;
 if(!capture_spy::Add("PrintWindow",reinterpret_cast<void*>(&KnownSourcePrintWindow))){Log("{\"kind\":\"fatal\",\"stage\":\"known_source_printwindow_surrogate_hook\"}");return 6;}
 Log("{\"kind\":\"ready\",\"pid\":%lu,\"hwnd\":%llu,\"proxy\":%llu,\"second_proxy\":%llu,\"terminal_spies\":true,\"api_entries_verified\":true,\"no_activation_or_physical_input\":true,\"capture_transport\":\"known_source_printwindow_api_surrogate\",\"native_printwindow_compatibility_measured\":false,\"printwindow_surrogate_entry_verified\":true}",GetCurrentProcessId(),(unsigned long long)(ULONG_PTR)mainWindow,(unsigned long long)(ULONG_PTR)proxyWindow,(unsigned long long)(ULONG_PTR)secondWindow);
 const auto deadline=GetTickCount64()+120000;bool running=true;
 while(running&&GetTickCount64()<deadline){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}
 for(auto& own:surrogateWindows)own=nullptr;
 if(secondWindow)DestroyWindow(secondWindow);if(proxyWindow)DestroyWindow(proxyWindow);if(IsWindow(mainWindow))DestroyWindow(mainWindow);
 Log("{\"kind\":\"exit\",\"terminal_spies_retained\":true,\"absorbed_writes\":%llu,\"capture_transport\":\"known_source_printwindow_api_surrogate\",\"native_printwindow_compatibility_measured\":false}",capture_spy::writes.load());
 {std::lock_guard<std::mutex> lock(outputMutex);FILE* closing=output;output=nullptr;if(closing)fclose(closing);}return 0;
}
