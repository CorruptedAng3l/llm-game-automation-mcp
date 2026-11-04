// R2 fixture: genuine A/W originals, independent thread controls, no native writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <atomic>
#include <mutex>
#include <string>
#include <algorithm>
#include <share.h>
#include "vi_r2_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"gdi32.lib")
namespace {
constexpr UINT kControl=WM_APP+0x281,kProbe=WM_APP+0x282,kNested=WM_APP+0x283,kControlQuery=WM_APP+0x284;
constexpr ULONG_PTR kAnswer=0x52320000,kControlAnswer=0x43540001;
const wchar_t kLifetime[]=L"vinput.wndproc_binding.lifetime.v1",kOwn[]=L"fixture.r2.own_window.v1";
FILE* output=nullptr;std::mutex logMx;HWND mainWindow=nullptr,controlWindow=nullptr,freshWindow=nullptr;DWORD uiThread=0;HMODULE product=nullptr;
bool originalUnicode=false,outerMode=false,paused=false;WNDPROC hookToken=nullptr,outerSaved=nullptr;ULONG_PTR oldLifetime=0;bool outerInstalled=false;unsigned ownCookie=0x5232;
std::atomic<bool> revokeOnRaw{false};unsigned long long revokeTicket=0;
std::string textA;std::wstring textW;
std::atomic<unsigned long long> customIn{0},customOut{0},paintIn{0},paintOut{0},closeIn{0},closeOut{0},ncIn{0},ncOut{0},textIn{0},wrongOriginal{0},outerIn{0},outerOut{0},cycles{0},keyDown{0},keyUp{0},characters{0},mouse{0},rawSeen[2]{},rawApplied[2]{},rawServed[2]{};
std::atomic<bool> held{false};thread_local unsigned depth=0;
struct RawState {volatile LONG dx=0,dy=0;};RawState raw[2];
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
const char* B(bool value){return value?"true":"false";}
bool Own(HWND h){DWORD pid=0;return h&&GetWindowThreadProcessId(h,&pid)==uiThread&&pid==GetCurrentProcessId()&&(h==mainWindow||h==controlWindow||h==freshWindow)&&(ULONG_PTR)GetPropW(h,kOwn)==(ULONG_PTR)&ownCookie;}
void WINAPI Key(BYTE,BYTE,DWORD,ULONG_PTR){++capture_spy::writes;}
SHORT WINAPI NoKey(int){return 0;}
HWND WINAPI ExternalForeground(){return GetDesktopWindow();}
BOOL WINAPI ExternalGui(DWORD,PGUITHREADINFO out){if(!out||out->cbSize!=sizeof(*out)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}GUITHREADINFO value{sizeof(value)};value.hwndActive=GetDesktopWindow();*out=value;return TRUE;}
UINT WINAPI RawRead(HRAWINPUT handle,UINT command,LPVOID data,PUINT size,UINT header){
 unsigned which=2;for(unsigned i=0;i<2;++i)if(handle==(HRAWINPUT)&raw[i])which=i;
 if(which==2||!size||header!=sizeof(RAWINPUTHEADER)||(command!=RID_INPUT&&command!=RID_HEADER)){SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;}
 const UINT wanted=command==RID_HEADER?(UINT)sizeof(RAWINPUTHEADER):(UINT)sizeof(RAWINPUT);if(!data){*size=wanted;return 0;}if(*size<wanted){*size=wanted;SetLastError(ERROR_INSUFFICIENT_BUFFER);return (UINT)-1;}
 RAWINPUT packet{};packet.header.dwType=RIM_TYPEMOUSE;packet.header.dwSize=sizeof(packet);packet.header.wParam=RIM_INPUT;
 if(command==RID_INPUT){packet.data.mouse.lLastX=InterlockedExchange(&raw[which].dx,0);packet.data.mouse.lLastY=InterlockedExchange(&raw[which].dy,0);++rawServed[which];}
 memcpy(data,&packet,wanted);*size=wanted;return wanted;
}
void Decode(unsigned which,LPARAM marker){if((HRAWINPUT)marker!=(HRAWINPUT)&raw[which])return;++rawSeen[which];UINT size=0;RAWINPUT packet{};
 const auto query=GetRawInputData((HRAWINPUT)marker,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));const auto got=query==0&&size==sizeof(packet)?GetRawInputData((HRAWINPUT)marker,RID_INPUT,&packet,&size,sizeof(RAWINPUTHEADER)):(UINT)-1;
 const bool applied=got==sizeof(packet)&&packet.header.dwType==RIM_TYPEMOUSE&&(packet.data.mouse.lLastX||packet.data.mouse.lLastY);if(applied)++rawApplied[which];
 Log("{\"event\":\"raw_decode\",\"target\":%u,\"query\":%u,\"got\":%u,\"applied\":%s}",which,query,got,B(applied));
}
BOOL WINAPI PrintOwn(HWND h,HDC dc,UINT){DWORD pid=0;if(h!=mainWindow||!Own(h)||!GetWindowThreadProcessId(h,&pid)||pid!=GetCurrentProcessId()||!dc||GetObjectType(dc)!=OBJ_MEMDC){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}RECT r{};if(!GetClientRect(h,&r))return FALSE;HBRUSH color=CreateSolidBrush(RGB(29,61,101));const bool ok=color&&FillRect(dc,&r,color);if(color)DeleteObject(color);return ok?TRUE:FALSE;}
LRESULT CALLBACK OriginalA(HWND,UINT,WPARAM,LPARAM);LRESULT CALLBACK OriginalW(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK OuterA(HWND,UINT,WPARAM,LPARAM);LRESULT CALLBACK OuterW(HWND,UINT,WPARAM,LPARAM);
LRESULT Original(HWND h,UINT m,WPARAM w,LPARAM l,bool unicode){
 if(!mainWindow&&unicode==originalUnicode){if(m==WM_NCCREATE)mainWindow=h;else return unicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);}
 if(h!=mainWindow||unicode!=originalUnicode){++wrongOriginal;return 0;}
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==kProbe||m==kNested){++customIn;const auto nonce=(ULONG_PTR)w;LRESULT result=(LRESULT)(kAnswer^nonce);
  if(m==kNested){const auto inner=unicode?SendMessageW(h,kProbe,w+1,0):SendMessageA(h,kProbe,w+1,0);const auto other=SendMessageW(controlWindow,kControlQuery,0,0);RemovePropW(h,kLifetime);Log("{\"event\":\"reentrant_gui\",\"inner_result\":%llu,\"control_result\":%llu}",(unsigned long long)(ULONG_PTR)inner,(unsigned long long)(ULONG_PTR)other);}
  ++customOut;return result;}
 if(m==WM_GETTEXT){++textIn;const size_t count=(size_t)w;if(!l||!count)return 0;if(unicode){auto out=(wchar_t*)l;const auto n=(std::min)(textW.size(),count-1);memcpy(out,textW.data(),n*sizeof(wchar_t));out[n]=0;return (LRESULT)n;}auto out=(char*)l;const auto n=(std::min)(textA.size(),count-1);memcpy(out,textA.data(),n);out[n]=0;return (LRESULT)n;}
 if(m==WM_PAINT){++paintIn;Log("{\"event\":\"original_message\",\"stage\":\"entry\",\"message\":%u,\"unicode\":%s,\"thread\":%lu}",m,B(unicode),GetCurrentThreadId());PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);if(dc)EndPaint(h,&p);++paintOut;Log("{\"event\":\"original_message\",\"stage\":\"return\",\"message\":%u,\"result\":0}",m);return 0;}
 if(m==WM_CLOSE){++closeIn;Log("{\"event\":\"original_message\",\"stage\":\"entry\",\"message\":%u,\"unicode\":%s,\"thread\":%lu}",m,B(unicode),GetCurrentThreadId());++closeOut;Log("{\"event\":\"original_message\",\"stage\":\"return\",\"message\":%u,\"result\":0}",m);return 0;}
 if(m==WM_NCDESTROY){++ncIn;Log("{\"event\":\"original_message\",\"stage\":\"entry\",\"message\":%u,\"unicode\":%s,\"thread\":%lu}",m,B(unicode),GetCurrentThreadId());const auto result=unicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);++ncOut;Log("{\"event\":\"original_message\",\"stage\":\"return\",\"message\":%u,\"result\":%llu}",m,(unsigned long long)(ULONG_PTR)result);return result;}
 if(m==WM_KEYDOWN&&w==VK_F6){++keyDown;held=true;}if(m==WM_KEYUP&&w==VK_F6){++keyUp;held=false;}
 if(m==WM_CHAR||m==WM_SYSCHAR)++characters;if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_MOUSEWHEEL)++mouse;if(m==WM_INPUT){Decode(0,l);return 0;}
 return unicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
}
LRESULT CALLBACK OriginalA(HWND h,UINT m,WPARAM w,LPARAM l){return Original(h,m,w,l,false);}
LRESULT CALLBACK OriginalW(HWND h,UINT m,WPARAM w,LPARAM l){return Original(h,m,w,l,true);}
LRESULT Outer(HWND h,UINT m,WPARAM w,LPARAM l,bool unicode){if(++depth>12){--depth;++cycles;return 0;}++outerIn;if(m==WM_INPUT){if(revokeOnRaw.exchange(false)){const auto removed=(ULONG_PTR)RemovePropW(h,kLifetime);Log("{\"event\":\"raw_revoked\",\"arm_token\":%llu,\"removed\":%llu,\"expected\":%llu,\"outer_callback_already_entered\":true}",revokeTicket,(unsigned long long)removed,(unsigned long long)oldLifetime);}Decode(0,l);}const auto result=unicode?CallWindowProcW(outerSaved,h,m,w,l):CallWindowProcA(outerSaved,h,m,w,l);++outerOut;--depth;return result;}
LRESULT CALLBACK OuterA(HWND h,UINT m,WPARAM w,LPARAM l){return Outer(h,m,w,l,false);}
LRESULT CALLBACK OuterW(HWND h,UINT m,WPARAM w,LPARAM l){return Outer(h,m,w,l,true);}
LRESULT CALLBACK ControlWindow(HWND h,UINT m,WPARAM w,LPARAM l){if(m==kControlQuery)return (LRESULT)kControlAnswer;if(m==WM_INPUT){Decode(1,l);return 0;}return DefWindowProcW(h,m,w,l);}
void Snapshot(unsigned action,unsigned long long token,bool ok){DWORD owner=0;const auto thread=mainWindow?GetWindowThreadProcessId(mainWindow,&owner):0;
 Log("{\"event\":\"chain\",\"token\":%llu,\"paused\":%s,\"original_proc\":%llu,\"hook_token\":%llu,\"getter_a\":%llu,\"getter_w\":%llu,\"outer_saved\":%llu,\"outer_setter_unicode\":%s}",token,B(paused),(unsigned long long)(ULONG_PTR)(originalUnicode?OriginalW:OriginalA),(unsigned long long)(ULONG_PTR)hookToken,(unsigned long long)(ULONG_PTR)GetWindowLongPtrA(mainWindow,GWLP_WNDPROC),(unsigned long long)(ULONG_PTR)GetWindowLongPtrW(mainWindow,GWLP_WNDPROC),(unsigned long long)(ULONG_PTR)outerSaved,B(!originalUnicode));
 Log("{\"event\":\"snapshot\",\"action\":%u,\"token\":%llu,\"control_ok\":%s,\"live\":%s,\"pid\":%lu,\"thread\":%lu,\"property\":%llu,\"old_property\":%llu,\"current_unicode\":%s,\"original_unicode\":%s,\"outer_installed\":%s,\"custom_in\":%llu,\"custom_out\":%llu,\"paint_in\":%llu,\"paint_out\":%llu,\"close_in\":%llu,\"close_out\":%llu,\"nc_in\":%llu,\"nc_out\":%llu,\"wrong_original\":%llu,\"cycles\":%llu,\"key_down\":%llu,\"key_up\":%llu,\"held\":%s,\"chars\":%llu,\"mouse\":%llu,\"raw_seen\":[%llu,%llu],\"raw_applied\":[%llu,%llu],\"raw_served\":[%llu,%llu],\"absorbed_writes\":%llu}",action,token,B(ok),B(Own(mainWindow)),owner,thread,(unsigned long long)(ULONG_PTR)GetPropW(mainWindow,kLifetime),(unsigned long long)oldLifetime,B(IsWindowUnicode(mainWindow)!=FALSE),B(originalUnicode),B(outerInstalled),customIn.load(),customOut.load(),paintIn.load(),paintOut.load(),closeIn.load(),closeOut.load(),ncIn.load(),ncOut.load(),wrongOriginal.load(),cycles.load(),keyDown.load(),keyUp.load(),B(held),characters.load(),mouse.load(),rawSeen[0].load(),rawSeen[1].load(),rawApplied[0].load(),rawApplied[1].load(),rawServed[0].load(),rawServed[1].load(),capture_spy::writes.load());}
void Probe(unsigned long long token,bool nested=false){
 const auto nonce=(WPARAM)(token&0xFFFF);DWORD_PTR result=0;const auto sent=originalUnicode?SendMessageTimeoutW(mainWindow,nested?kNested:kProbe,nonce,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,700,&result):SendMessageTimeoutA(mainWindow,nested?kNested:kProbe,nonce,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,700,&result);
 char a[160];wchar_t b[160];memset(a,0xA5,sizeof(a));memset(b,0xA5,sizeof(b));DWORD_PTR ar=0,br=0;
 const bool sa=SendMessageTimeoutA(mainWindow,WM_GETTEXT,128,(LPARAM)a,SMTO_ABORTIFHUNG|SMTO_BLOCK,700,&ar)!=0;
 const bool sw=SendMessageTimeoutW(mainWindow,WM_GETTEXT,128,(LPARAM)b,SMTO_ABORTIFHUNG|SMTO_BLOCK,700,&br)!=0;
 bool ag=true,wg=true;for(unsigned i=128;i<160;++i){if((unsigned char)a[i]!=0xA5)ag=false;if(b[i]!=(wchar_t)0xA5A5)wg=false;}
 const bool am=sa&&ar==textA.size()&&!memcmp(a,textA.c_str(),textA.size()+1),wm=sw&&br==textW.size()&&!memcmp(b,textW.c_str(),(textW.size()+1)*sizeof(wchar_t));
 const auto pi=paintIn.load();if(Own(mainWindow)){InvalidateRect(mainWindow,nullptr,FALSE);UpdateWindow(mainWindow);}
 Log("{\"event\":\"gui_probe\",\"token\":%llu,\"sent\":%s,\"result\":%llu,\"expected\":%llu,\"ansi_matches\":%s,\"unicode_matches\":%s,\"ansi_guard\":%s,\"unicode_guard\":%s,\"paint_delta\":%llu}",token,B(sent!=0),(unsigned long long)result,(unsigned long long)(kAnswer^(ULONG_PTR)nonce),B(am),B(wm),B(ag),B(wg),paintIn.load()-pi);
}
struct WrongCall {HWND window;LRESULT result=0;};
DWORD WINAPI WrongThread(void* context){auto* c=(WrongCall*)context;c->result=originalUnicode?CallWindowProcW(hookToken,c->window,kProbe,7,0):CallWindowProcA(hookToken,c->window,kProbe,7,0);return 0;}
void Action(unsigned code,unsigned long long token){bool ok=true;
 if(code==1){oldLifetime=(ULONG_PTR)GetPropW(mainWindow,kLifetime);hookToken=originalUnicode?(WNDPROC)GetWindowLongPtrW(mainWindow,GWLP_WNDPROC):(WNDPROC)GetWindowLongPtrA(mainWindow,GWLP_WNDPROC);ok=Own(mainWindow)&&oldLifetime&&hookToken;}
 else if(code==2)Probe(token);
 else if(code==3)paused=true;
 else if(code==4||code==5){const unsigned which=code-4;InterlockedExchange(&raw[which].dx,17);InterlockedExchange(&raw[which].dy,-9);ok=PostMessageW(which?controlWindow:mainWindow,WM_INPUT,RIM_INPUT,(LPARAM)&raw[which])!=FALSE;}
 else if(code==6){WNDPROC top=originalUnicode?(WNDPROC)GetWindowLongPtrW(mainWindow,GWLP_WNDPROC):(WNDPROC)GetWindowLongPtrA(mainWindow,GWLP_WNDPROC);const bool exact=Own(mainWindow)&&oldLifetime&&top==hookToken;const auto removed=exact?(ULONG_PTR)RemovePropW(mainWindow,kLifetime):0;ok=exact&&removed==oldLifetime;Log("{\"event\":\"removed\",\"token\":%llu,\"exact_captured_top\":%s,\"removed\":%llu,\"expected\":%llu}",token,B(exact),(unsigned long long)removed,(unsigned long long)oldLifetime);if(ok){Probe(token);if(outerMode){const bool unicode=!originalUnicode;SetLastError(0);outerSaved=unicode?(WNDPROC)SetWindowLongPtrW(mainWindow,GWLP_WNDPROC,(LONG_PTR)OuterW):(WNDPROC)SetWindowLongPtrA(mainWindow,GWLP_WNDPROC,(LONG_PTR)OuterA);outerInstalled=outerSaved!=nullptr;ok=outerInstalled;}}}
 else if(code==7)paused=false;
 else if(code==8){ok=Own(mainWindow)&&SetPropW(mainWindow,kLifetime,(HANDLE)(oldLifetime+0x10001));}
 else if(code==9){ok=Own(mainWindow)&&SetPropW(mainWindow,kLifetime,(HANDLE)oldLifetime);}
 else if(code==10){if(Own(mainWindow)){DWORD_PTR value=0;ok=SendMessageTimeoutW(mainWindow,WM_CLOSE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,700,&value)!=0;}else ok=false;}
 else if(code==11){ok=Own(mainWindow)?DestroyWindow(mainWindow)!=FALSE:false;}
 else if(code==12){ok=Own(mainWindow);if(ok)RemovePropW(mainWindow,kLifetime);}
 else if(code==13)Probe(token,true);
 else if(code==14){const auto prior=customIn.load();auto* call=new WrongCall{mainWindow};HANDLE thread=CreateThread(nullptr,0,WrongThread,call,0,nullptr);const bool ended=thread&&WaitForSingleObject(thread,1000)==WAIT_OBJECT_0;const LRESULT value=ended?call->result:0;if(thread)CloseHandle(thread);if(ended||!thread)delete call;Log("{\"event\":\"wrong_thread\",\"token\":%llu,\"completed\":%s,\"result\":%llu,\"original_delta\":%llu,\"timed_out_context_retained_until_process_exit\":%s}",token,B(ended),(unsigned long long)(ULONG_PTR)value,customIn.load()-prior,B(thread&&!ended));ok=ended;}
 else if(code==15){const auto prior=customIn.load();const auto result=originalUnicode?CallWindowProcW(hookToken,controlWindow,kProbe,7,0):CallWindowProcA(hookToken,controlWindow,kProbe,7,0);Log("{\"event\":\"missing_record\",\"token\":%llu,\"result\":%llu,\"original_delta\":%llu}",token,(unsigned long long)(ULONG_PTR)result,customIn.load()-prior);}
 else if(code==16){const auto top=originalUnicode?(WNDPROC)GetWindowLongPtrW(mainWindow,GWLP_WNDPROC):(WNDPROC)GetWindowLongPtrA(mainWindow,GWLP_WNDPROC);ok=Own(mainWindow)&&oldLifetime&&top==hookToken&&!outerInstalled;if(ok){outerSaved=!originalUnicode?(WNDPROC)SetWindowLongPtrW(mainWindow,GWLP_WNDPROC,(LONG_PTR)OuterW):(WNDPROC)SetWindowLongPtrA(mainWindow,GWLP_WNDPROC,(LONG_PTR)OuterA);outerInstalled=outerSaved!=nullptr;ok=outerInstalled;if(ok){revokeTicket=token;revokeOnRaw=true;}}}
 else if(code!=0)ok=false;
 Snapshot(code,token,ok);
}
DWORD WINAPI Watchdog(void*){Sleep(120000);ExitProcess(119);}
void Text(){textW=L"R2 original sentinel";for(wchar_t cp:{(wchar_t)0xE9,(wchar_t)0xA3,(wchar_t)0x3042}){std::wstring w=L"R2 original ";w+=cp;char a[128]{};wchar_t back[128]{};if(WideCharToMultiByte(CP_ACP,0,w.c_str(),-1,a,128,nullptr,nullptr)>0&&MultiByteToWideChar(CP_ACP,0,a,-1,back,128)>0&&w==back){textW=w;break;}}char a[128]{};WideCharToMultiByte(CP_ACP,0,textW.c_str(),-1,a,128,nullptr,nullptr);textA=a;}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);HANDLE watchdog=CreateThread(nullptr,0,Watchdog,nullptr,0,nullptr);if(!watchdog)return 2;CloseHandle(watchdog);
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=5){if(args)LocalFree(args);return 2;}output=_wfsopen(args[1],L"w",_SH_DENYNO);const std::wstring dll=args[2];originalUnicode=!wcscmp(args[3],L"unicode");outerMode=!wcscmp(args[4],L"outer");LocalFree(args);if(!output)return 3;
 const bool safe=capture_spy::Install()&&capture_spy::Add("keybd_event",(void*)&Key)&&capture_spy::Add("GetForegroundWindow",(void*)&ExternalForeground)&&capture_spy::Add("GetGUIThreadInfo",(void*)&ExternalGui)&&capture_spy::Add("GetAsyncKeyState",(void*)&NoKey)&&capture_spy::Add("GetRawInputData",(void*)&RawRead)&&capture_spy::Add("PrintWindow",(void*)&PrintOwn);
 Log("{\"event\":\"guards\",\"safe\":%s,\"terminal_writers\":19,\"native_write_trampolines_called\":false,\"keybd_event_probed\":false}",B(safe));if(!safe)return 4;
 uiThread=GetCurrentThreadId();Text();WNDCLASSA a{};a.hInstance=instance;a.lpfnWndProc=OriginalA;a.lpszClassName="ViR2OriginalA";WNDCLASSW w{};w.hInstance=instance;w.lpfnWndProc=OriginalW;w.lpszClassName=L"ViR2OriginalW";WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=ControlWindow;c.lpszClassName=L"ViR2Control";if(!RegisterClassA(&a)||!RegisterClassW(&w)||!RegisterClassW(&c))return 5;
 const LONGLONG sx=GetSystemMetrics(SM_XVIRTUALSCREEN),sy=GetSystemMetrics(SM_YVIRTUALSCREEN),sw=GetSystemMetrics(SM_CXVIRTUALSCREEN),sh=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(sw<=0||sh<=0||!(-27000LL+640<=sx||-27000LL>=sx+sw||700LL+480<=sy||700LL>=sy+sh))return 5;
 mainWindow=originalUnicode?CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,w.lpszClassName,L"R2 normal caption W",WS_POPUP|WS_VISIBLE,-27000,700,640,480,nullptr,nullptr,instance,nullptr):CreateWindowExA(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,a.lpszClassName,"R2 normal caption A",WS_POPUP|WS_VISIBLE,-27000,700,640,480,nullptr,nullptr,instance,nullptr);
 controlWindow=CreateWindowExW(0,c.lpszClassName,L"R2 independent control",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
 // Pre-DLL ordinary creation: no hooked constructor/native-show escape hatch.
 freshWindow=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"R2 fresh binding B",WS_POPUP|WS_VISIBLE,-27000,700,640,480,nullptr,nullptr,instance,nullptr);
 if(!mainWindow||!controlWindow||!freshWindow||!SetPropW(mainWindow,kOwn,(HANDLE)&ownCookie)||!SetPropW(controlWindow,kOwn,(HANDLE)&ownCookie)||!SetPropW(freshWindow,kOwn,(HANDLE)&ownCookie))return 5;
 product=LoadLibraryW(dll.c_str());Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"control\":%llu,\"fresh\":%llu,\"fresh_property\":%llu,\"fresh_precreated_before_dll\":true,\"raw_main\":%llu,\"raw_child\":%llu,\"unicode\":%s,\"outer_mode\":%s,\"self_loaded\":%s,\"ordinary_thread_controls\":true,\"capture_transport\":\"owned_memory_dc_printwindow_api_surrogate\",\"native_printwindow_compatibility_measured\":false}",GetCurrentProcessId(),uiThread,(unsigned long long)(ULONG_PTR)mainWindow,(unsigned long long)(ULONG_PTR)controlWindow,(unsigned long long)(ULONG_PTR)freshWindow,(unsigned long long)(ULONG_PTR)GetPropW(freshWindow,kLifetime),(unsigned long long)(ULONG_PTR)&raw[0],(unsigned long long)(ULONG_PTR)&raw[1],B(originalUnicode),B(outerMode),B(product!=nullptr));if(!product)return 6;
 DWORD freshPid=0;const DWORD freshThread=GetWindowThreadProcessId(freshWindow,&freshPid);RECT freshClient{};const bool clientOk=GetClientRect(freshWindow,&freshClient)!=FALSE;
 Log("{\"event\":\"fresh_window\",\"pid\":%lu,\"thread\":%lu,\"owned_cookie\":%s,\"client_ok\":%s,\"client_w\":%ld,\"client_h\":%ld,\"visible\":%s,\"noactivate\":%s,\"property\":%llu}",freshPid,freshThread,B(Own(freshWindow)),B(clientOk),freshClient.right,freshClient.bottom,B(IsWindowVisible(freshWindow)!=FALSE),B((GetWindowLongPtrW(freshWindow,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0),(unsigned long long)(ULONG_PTR)GetPropW(freshWindow,kLifetime));
 const auto until=GetTickCount64()+110000;while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,paused?(HWND)-1:nullptr,0,0,PM_REMOVE)){if(!m.hwnd&&m.message==kControl){Action((unsigned)m.wParam,(unsigned long long)(ULONG_PTR)m.lParam);continue;}TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}return 0;
}
