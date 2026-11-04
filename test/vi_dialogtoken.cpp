// Virgin system-dialog token observer. No product token decoding or normalizing.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include "vi_cursor_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
using Read=LONG_PTR(WINAPI*)(HWND,int);
using Write=LONG_PTR(WINAPI*)(HWND,int,LONG_PTR);
FILE* output=nullptr;std::mutex logMx;HWND dialog=nullptr,control=nullptr;DWORD uiThread=0;
Read nativeReadA=nullptr,nativeReadW=nullptr;Write nativeWriteA=nullptr,nativeWriteW=nullptr;
void* entries[4]{};void* replacements[4]{};void* originals[4]{};bool sharedApi[4]{};
thread_local unsigned logDepth=0,outerDepth=0;
std::atomic<WNDPROC> nativeSaved{nullptr};std::atomic<unsigned long long> nativeOuterCalls{0};
bool requestedUnicode=true,nativeOnly=false,nativeInstalled=false;
int lifetimeStorage=0;const wchar_t lifetimeProperty[]=L"vi.fixture.dialogtoken.lifetime.v1";
ULONG_PTR Number(LONG_PTR p){return (ULONG_PTR)p;}
void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);if(!output)return;va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
bool Own(HWND h){DWORD owner=0;return h&&h==dialog&&GetWindowThreadProcessId(h,&owner)==uiThread&&owner==GetCurrentProcessId()&&GetPropW(h,lifetimeProperty)==(HANDLE)&lifetimeStorage;}
struct Logging{bool outer;Logging():outer(++logDepth==1){}~Logging(){--logDepth;}};
void Snapshot(const char* stage,unsigned long long token){
 if(!Own(dialog))return;
 const auto a=nativeReadA(dialog,GWLP_WNDPROC),w=nativeReadW(dialog,GWLP_WNDPROC);
 const auto ca=GetClassLongPtrA(dialog,GCLP_WNDPROC),cw=GetClassLongPtrW(dialog,GCLP_WNDPROC);
 const auto da=nativeReadA(dialog,DWLP_DLGPROC),dw=nativeReadW(dialog,DWLP_DLGPROC);
 const auto user=GetModuleHandleW(L"user32.dll");wchar_t cls[80]{};GetClassNameW(dialog,cls,80);
 Log("{\"event\":\"snapshot\",\"stage\":\"%s\",\"token\":%llu,\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"class_is_dialog\":%s,\"requested_unicode\":%s,\"current_unicode\":%s,\"getter_a\":%llu,\"getter_w\":%llu,\"class_proc_a\":%llu,\"class_proc_w\":%llu,\"dialog_proc_a\":%llu,\"dialog_proc_w\":%llu,\"defdlg_a_export\":%llu,\"defdlg_w_export\":%llu,\"defwindow_a_export\":%llu,\"defwindow_w_export\":%llu,\"native_saved\":%llu,\"native_outer_calls\":%llu,\"opaque_tokens_not_decoded\":true}",stage,token,GetCurrentProcessId(),uiThread,(unsigned long long)(ULONG_PTR)dialog,!wcscmp(cls,L"#32770")?"true":"false",requestedUnicode?"true":"false",IsWindowUnicode(dialog)?"true":"false",(unsigned long long)Number(a),(unsigned long long)Number(w),(unsigned long long)(ULONG_PTR)ca,(unsigned long long)(ULONG_PTR)cw,(unsigned long long)Number(da),(unsigned long long)Number(dw),(unsigned long long)(ULONG_PTR)GetProcAddress(user,"DefDlgProcA"),(unsigned long long)(ULONG_PTR)GetProcAddress(user,"DefDlgProcW"),(unsigned long long)(ULONG_PTR)GetProcAddress(user,"DefWindowProcA"),(unsigned long long)(ULONG_PTR)GetProcAddress(user,"DefWindowProcW"),(unsigned long long)(ULONG_PTR)nativeSaved.load(),nativeOuterCalls.load());
}
LONG_PTR ObserveRead(Read original,const char* api,HWND h,int index){
 const LONG_PTR result=original(h,index);const DWORD error=GetLastError();Logging scope;
 if(scope.outer&&Own(h)&&(index==GWLP_WNDPROC||index==DWLP_DLGPROC))Log("{\"event\":\"native_get\",\"api\":\"%s\",\"caller_thread\":%lu,\"hwnd\":%llu,\"index\":%d,\"result\":%llu,\"last_error\":%lu,\"unicode\":%s}",api,GetCurrentThreadId(),(unsigned long long)(ULONG_PTR)h,index,(unsigned long long)Number(result),error,IsWindowUnicode(h)?"true":"false");
 SetLastError(error);return result;
}
LONG_PTR WINAPI ReadA(HWND h,int i){return ObserveRead(nativeReadA,sharedApi[0]?"GetA/W_shared_entry":"GetA",h,i);}
LONG_PTR WINAPI ReadW(HWND h,int i){return ObserveRead(nativeReadW,sharedApi[1]?"GetA/W_shared_entry":"GetW",h,i);}
LONG_PTR ObserveWrite(Write original,const char* api,HWND h,int index,LONG_PTR replacement){
 const DWORD incoming=GetLastError();Logging scope;const bool watched=scope.outer&&Own(h)&&index==GWLP_WNDPROC;
 LONG_PTR beforeA=0,beforeW=0;bool beforeUnicode=false;
 if(watched){beforeA=nativeReadA(h,GWLP_WNDPROC);beforeW=nativeReadW(h,GWLP_WNDPROC);beforeUnicode=IsWindowUnicode(h)!=FALSE;}
 SetLastError(incoming);const LONG_PTR previous=original(h,index,replacement);const DWORD error=GetLastError();
 if(watched){const auto afterA=nativeReadA(h,GWLP_WNDPROC),afterW=nativeReadW(h,GWLP_WNDPROC);
  Log("{\"event\":\"native_set\",\"api\":\"%s\",\"caller_thread\":%lu,\"hwnd\":%llu,\"replacement\":%llu,\"before_a\":%llu,\"before_w\":%llu,\"previous\":%llu,\"after_a\":%llu,\"after_w\":%llu,\"last_error\":%lu,\"unicode_before\":%s,\"unicode_after\":%s,\"previous_equals_before_a\":%s,\"previous_equals_before_w\":%s,\"native_result_returned_unchanged\":true}",api,GetCurrentThreadId(),(unsigned long long)(ULONG_PTR)h,(unsigned long long)Number(replacement),(unsigned long long)Number(beforeA),(unsigned long long)Number(beforeW),(unsigned long long)Number(previous),(unsigned long long)Number(afterA),(unsigned long long)Number(afterW),error,beforeUnicode?"true":"false",IsWindowUnicode(h)?"true":"false",previous==beforeA?"true":"false",previous==beforeW?"true":"false");
 }
 SetLastError(error);return previous;
}
LONG_PTR WINAPI WriteA(HWND h,int i,LONG_PTR v){return ObserveWrite(nativeWriteA,sharedApi[2]?"SetA/W_shared_entry":"SetA",h,i,v);}
LONG_PTR WINAPI WriteW(HWND h,int i,LONG_PTR v){return ObserveWrite(nativeWriteW,sharedApi[3]?"SetA/W_shared_entry":"SetW",h,i,v);}
bool InstallObservers(){
 const auto user=GetModuleHandleW(L"user32.dll");
#ifdef _WIN64
 const char* names[]={"GetWindowLongPtrA","GetWindowLongPtrW","SetWindowLongPtrA","SetWindowLongPtrW"};
#else
 const char* names[]={"GetWindowLongA","GetWindowLongW","SetWindowLongA","SetWindowLongW"};
#endif
 void* detours[]={(void*)&ReadA,(void*)&ReadW,(void*)&WriteA,(void*)&WriteW};
 for(unsigned i=0;i<4;++i){
  entries[i]=(void*)GetProcAddress(user,names[i]);replacements[i]=detours[i];if(!entries[i])return false;
  int alias=-1;for(unsigned j=0;j<i;++j)if(entries[j]==entries[i])alias=(int)j;
  if(alias>=0){if((i<2)!=(alias<2))return false;originals[i]=originals[alias];replacements[i]=replacements[alias];sharedApi[i]=sharedApi[alias]=true;}
  else if(VIMH_CreateHook(entries[i],detours[i],&originals[i])!=MH_OK||!originals[i])return false;
  if(i==0)nativeReadA=(Read)originals[i];if(i==1)nativeReadW=(Read)originals[i];if(i==2)nativeWriteA=(Write)originals[i];if(i==3)nativeWriteW=(Write)originals[i];
  if(alias<0&&VIMH_EnableHook(entries[i])!=MH_OK)return false;
  if(!vi_cursor_spy::Reaches(entries[i],replacements[i]))return false;
 }
 return true;
}
LRESULT CALLBACK NativeOuter(HWND h,UINT m,WPARAM w,LPARAM l){
 struct Depth{Depth(){++outerDepth;}~Depth(){--outerDepth;}} depth;
 if(outerDepth>1)return 0;++nativeOuterCalls;const auto previous=nativeSaved.load(std::memory_order_acquire);
 if(!previous)return requestedUnicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
 return requestedUnicode?CallWindowProcW(previous,h,m,w,l):CallWindowProcA(previous,h,m,w,l);
}
LRESULT CALLBACK Controls(HWND h,UINT m,WPARAM w,LPARAM l){
 const auto token=(unsigned long long)(ULONG_PTR)l;
 if(m==WM_APP+0x90&&Own(dialog)){Snapshot("requested",token);return 0;}
 if(m==WM_APP+0x91&&Own(dialog)){
  wchar_t text[512]{};DWORD_PTR result=0;const bool ok=SendMessageTimeoutW(dialog,WM_GETTEXT,512,(LPARAM)text,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&result)!=0;
  Log("{\"event\":\"text\",\"token\":%llu,\"ok\":%s,\"caption_matches\":%s,\"result\":%llu}",token,ok?"true":"false",!wcscmp(text,L"Virgin system dialog")?"true":"false",(unsigned long long)result);return 0;
 }
 if(m==WM_APP+0x92&&Own(dialog)&&nativeOnly&&!nativeInstalled){
  Snapshot("native_only_before_install",token);SetLastError(0);
  const auto previous=requestedUnicode?(WNDPROC)SetWindowLongPtrW(dialog,GWLP_WNDPROC,(LONG_PTR)NativeOuter):(WNDPROC)SetWindowLongPtrA(dialog,GWLP_WNDPROC,(LONG_PTR)NativeOuter);
  const DWORD error=GetLastError();const bool ok=previous&&previous!=NativeOuter;nativeSaved.store(ok?previous:nullptr,std::memory_order_release);nativeInstalled=ok;
  Log("{\"event\":\"native_only_installed\",\"token\":%llu,\"ok\":%s,\"last_error\":%lu,\"previous\":%llu}",token,ok?"true":"false",error,(unsigned long long)(ULONG_PTR)previous);Snapshot("native_only_after_install",token);return 0;
 }
 if(m==WM_APP+0x93&&Own(dialog)&&nativeOnly&&nativeInstalled){
  const auto top=requestedUnicode?nativeReadW(dialog,GWLP_WNDPROC):nativeReadA(dialog,GWLP_WNDPROC);bool ok=(WNDPROC)top==NativeOuter;
  if(ok){const auto previous=requestedUnicode?(WNDPROC)SetWindowLongPtrW(dialog,GWLP_WNDPROC,(LONG_PTR)nativeSaved.load()):(WNDPROC)SetWindowLongPtrA(dialog,GWLP_WNDPROC,(LONG_PTR)nativeSaved.load());ok=previous==NativeOuter;if(ok)nativeInstalled=false;}
  Log("{\"event\":\"native_only_removed\",\"token\":%llu,\"ok\":%s}",token,ok?"true":"false");Snapshot("native_only_after_remove",token);return 0;
 }
 return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count!=4){if(args)LocalFree(args);return 2;}
 output=_wfsopen(args[1],L"w",_SH_DENYNO);requestedUnicode=std::wstring(args[2])==L"w";nativeOnly=std::wstring(args[3])==L"native";LocalFree(args);if(!output)return 2;
 if(!vi_cursor_spy::Install()||!vi_cursor_spy::VerifyBeforeProduct()||!InstallObservers())return 3;uiThread=GetCurrentThreadId();
 dialog=requestedUnicode?CreateWindowExW(WS_EX_NOACTIVATE,L"#32770",L"Virgin system dialog",WS_OVERLAPPEDWINDOW,-28000,600,480,300,nullptr,nullptr,instance,nullptr):CreateWindowExA(WS_EX_NOACTIVATE,"#32770","Virgin system dialog",WS_OVERLAPPEDWINDOW,-28000,600,480,300,nullptr,nullptr,instance,nullptr);
 if(!dialog||!SetPropW(dialog,lifetimeProperty,(HANDLE)&lifetimeStorage))return 4;
 CreateWindowExW(0,L"STATIC",L"Controlled system dialog child",WS_CHILD|WS_VISIBLE,10,10,440,80,dialog,nullptr,instance,nullptr);
 ShowWindow(dialog,SW_SHOWNOACTIVATE); // Exact focusprobe setup order; own offscreen/no-activate only.
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Controls;cls.lpszClassName=L"ViDialogTokenControl";if(!RegisterClassW(&cls))return 4;
 control=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Token controls",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);if(!control)return 4;
 Snapshot("virgin_before_product",0);
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"hwnd\":%llu,\"control\":%llu,\"requested_unicode\":%s,\"native_only\":%s,\"terminal_spies\":true,\"native_write_trampolines_called\":false,\"no_fixture_setter_before_ready\":true}",GetCurrentProcessId(),uiThread,(unsigned long long)(ULONG_PTR)dialog,(unsigned long long)(ULONG_PTR)control,requestedUnicode?"true":"false",nativeOnly?"true":"false");
 const auto until=GetTickCount64()+120000;while(GetTickCount64()<until){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)return 0;TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(2);}
 // All observers and terminal write guards remain installed through termination.
 return 0;
}
