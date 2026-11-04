// Owned mixed A/W subclass observer; all desktop-write APIs terminate below product.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <string>
#include <mutex>
#include "vi_cursor_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
namespace {
FILE* output=nullptr;std::mutex outputMx;HWND control=nullptr;DWORD uiThread=0;
constexpr UINT kPrivateProbe=WM_APP+0xB1;
struct Target{HWND window=nullptr;bool originalUnicode=false,outerUnicode=false,installed=false;WNDPROC saved=nullptr;unsigned long long originalText=0,textMatch=0,privateCalls=0,downs=0,outerMoves=0,outerDowns=0,loopMoves=0,cycles=0;};
Target targets[2];unsigned filter=0;thread_local unsigned outerDepth=0;unsigned long long probeToken=0;
std::wstring textW;std::string textA;unsigned sentinelCodePoint=0;
bool probingText=false;
void Log(const char* format,...){std::lock_guard<std::mutex> lock(outputMx);va_list args;va_start(args,format);vfprintf(output,format,args);va_end(args);fputc('\n',output);fflush(output);}
Target* Find(HWND h){for(auto& t:targets)if(t.window==h)return &t;return nullptr;}
bool Owned(HWND h){DWORD pid=0;return GetWindowThreadProcessId(h,&pid)==uiThread&&pid==GetCurrentProcessId();}
bool MatchA(LPARAM l){if(!l)return false;for(size_t i=0;i<=textA.size();i++){char value=0;SIZE_T read=0;if(!ReadProcessMemory(GetCurrentProcess(),(const char*)l+i,&value,1,&read)||read!=1||value!=textA.c_str()[i])return false;}return true;}
bool MatchW(LPARAM l){if(!l)return false;for(size_t i=0;i<=textW.size();i++){wchar_t value=0;SIZE_T read=0;if(!ReadProcessMemory(GetCurrentProcess(),(const wchar_t*)l+i,&value,sizeof(value),&read)||read!=sizeof(value)||value!=textW.c_str()[i])return false;}return true;}
bool Store(void* outputBuffer,const void* source,size_t bytes){__try{memcpy(outputBuffer,source,bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
LRESULT Original(HWND h,UINT m,WPARAM w,LPARAM l,bool unicode){auto* t=Find(h);
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(t&&m==WM_GETTEXT&&l){const size_t needed=unicode?textW.size()+1:textA.size()+1;if(w<needed)return 0;const bool matched=t->originalUnicode==unicode&&Store((void*)l,unicode?(const void*)textW.c_str():(const void*)textA.c_str(),unicode?needed*sizeof(wchar_t):needed);if(probingText){++t->originalText;if(matched)++t->textMatch;Log("{\"event\":\"original_text\",\"token\":%llu,\"which\":%u,\"procedure_unicode\":%s,\"matched\":%s}",probeToken,(unsigned)(t-targets)+1,unicode?"true":"false",matched?"true":"false");}return matched?(LRESULT)(unicode?textW.size():textA.size()):0;}
 if(t&&m==kPrivateProbe){++t->privateCalls;return unicode?0xB02:0xA01;}
 if(t&&m==WM_LBUTTONDOWN)++t->downs;
 return unicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
}
LRESULT CALLBACK OriginalA(HWND h,UINT m,WPARAM w,LPARAM l){return Original(h,m,w,l,false);}
LRESULT CALLBACK OriginalW(HWND h,UINT m,WPARAM w,LPARAM l){return Original(h,m,w,l,true);}
LRESULT Outer(HWND h,UINT m,WPARAM w,LPARAM l,bool unicode){auto* t=Find(h);struct Depth{Depth(){++outerDepth;}~Depth(){--outerDepth;}} depth;
 if(!t)return unicode?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
 if(outerDepth>1){++t->cycles;return 0;}
 if(m==WM_MOUSEMOVE){++t->outerMoves;return 0;}
 if(m==WM_LBUTTONDOWN){++t->outerDowns;return 0;}if(m==WM_LBUTTONUP)return 0;
 return unicode?CallWindowProcW(t->saved,h,m,w,l):CallWindowProcA(t->saved,h,m,w,l);
}
LRESULT CALLBACK OuterA(HWND h,UINT m,WPARAM w,LPARAM l){return Outer(h,m,w,l,false);}
LRESULT CALLBACK OuterW(HWND h,UINT m,WPARAM w,LPARAM l){return Outer(h,m,w,l,true);}
void Snapshot(unsigned which,unsigned long long token,const char* event){auto& t=targets[which-1];const auto a=(WNDPROC)GetWindowLongPtrA(t.window,GWLP_WNDPROC),b=(WNDPROC)GetWindowLongPtrW(t.window,GWLP_WNDPROC);
 Log("{\"event\":\"%s\",\"token\":%llu,\"which\":%u,\"hwnd\":%llu,\"current_is_unicode\":%s,\"original_unicode\":%s,\"getter_a\":%llu,\"getter_w\":%llu,\"outer_installed\":%s,\"outer_install_unicode\":%s,\"saved_predecessor_token\":%llu,\"original_a\":%llu,\"original_w\":%llu,\"outer_a\":%llu,\"outer_w\":%llu,\"original_text\":%llu,\"text_matches\":%llu,\"private_calls\":%llu,\"original_downs\":%llu,\"outer_moves\":%llu,\"outer_downs\":%llu,\"loop_moves\":%llu,\"cycles\":%llu,\"lower_write_attempts_absorbed\":%llu}",event,token,which,(unsigned long long)(ULONG_PTR)t.window,IsWindowUnicode(t.window)?"true":"false",t.originalUnicode?"true":"false",(unsigned long long)(ULONG_PTR)a,(unsigned long long)(ULONG_PTR)b,t.installed?"true":"false",t.outerUnicode?"true":"false",(unsigned long long)(ULONG_PTR)t.saved,(unsigned long long)(ULONG_PTR)OriginalA,(unsigned long long)(ULONG_PTR)OriginalW,(unsigned long long)(ULONG_PTR)OuterA,(unsigned long long)(ULONG_PTR)OuterW,t.originalText,t.textMatch,t.privateCalls,t.downs,t.outerMoves,t.outerDowns,t.loopMoves,t.cycles,vi_cursor_spy::Total());
}
LRESULT CALLBACK Controls(HWND h,UINT m,WPARAM w,LPARAM l){unsigned which=(unsigned)(w&0xFF);if(m>=WM_APP+0x90&&m<=WM_APP+0x95){if(which<1||which>2||!Owned(targets[which-1].window)){Log("{\"event\":\"control_error\"}");return 0;}auto& t=targets[which-1];const auto token=(unsigned long long)(ULONG_PTR)l;
  if(m==WM_APP+0x90){Snapshot(which,token,"snapshot");return 0;}
  if(m==WM_APP+0x91){bool ok=!t.installed;t.outerUnicode=!t.originalUnicode;WNDPROC outer=t.outerUnicode?OuterW:OuterA;if(ok){SetLastError(0);t.saved=t.outerUnicode?(WNDPROC)SetWindowLongPtrW(t.window,GWLP_WNDPROC,(LONG_PTR)outer):(WNDPROC)SetWindowLongPtrA(t.window,GWLP_WNDPROC,(LONG_PTR)outer);ok=t.saved!=nullptr;t.installed=ok;}Log("{\"event\":\"installed\",\"token\":%llu,\"which\":%u,\"ok\":%s}",token,which,ok?"true":"false");Snapshot(which,token,"installed_snapshot");return 0;}
  if(m==WM_APP+0x92){WNDPROC outer=t.outerUnicode?OuterW:OuterA;WNDPROC top=t.outerUnicode?(WNDPROC)GetWindowLongPtrW(t.window,GWLP_WNDPROC):(WNDPROC)GetWindowLongPtrA(t.window,GWLP_WNDPROC);bool ok=t.installed&&t.saved&&top==outer;if(ok){auto prior=t.outerUnicode?(WNDPROC)SetWindowLongPtrW(t.window,GWLP_WNDPROC,(LONG_PTR)t.saved):(WNDPROC)SetWindowLongPtrA(t.window,GWLP_WNDPROC,(LONG_PTR)t.saved);ok=prior==outer;if(ok)t.installed=false;}Log("{\"event\":\"removed\",\"token\":%llu,\"which\":%u,\"ok\":%s}",token,which,ok?"true":"false");Snapshot(which,token,"removed_snapshot");return 0;}
  if(m==WM_APP+0x93){probeToken=token;const bool unicode=(w&0x100)!=0;char outA[512]{};wchar_t outW[256]{};DWORD_PTR result=0;probingText=true;const bool sent=(unicode?SendMessageTimeoutW(t.window,WM_GETTEXT,128,(LPARAM)outW,SMTO_ABORTIFHUNG,500,&result):SendMessageTimeoutA(t.window,WM_GETTEXT,128,(LPARAM)outA,SMTO_ABORTIFHUNG,500,&result))!=0;probingText=false;const bool matched=unicode?MatchW((LPARAM)outW):MatchA((LPARAM)outA);Log("{\"event\":\"text_probe\",\"token\":%llu,\"which\":%u,\"send_unicode\":%s,\"sent\":%s,\"output_matches\":%s,\"result\":%llu}",token,which,unicode?"true":"false",sent?"true":"false",matched?"true":"false",(unsigned long long)result);Snapshot(which,token,"text_snapshot");return 0;}
  if(m==WM_APP+0x94){filter=(w&0x100)?which:0;Log("{\"event\":\"filter\",\"token\":%llu,\"which\":%u,\"enabled\":%s}",token,which,filter?"true":"false");return 0;}
  if(m==WM_APP+0x95){DWORD_PTR result=0;const bool sent=SendMessageTimeoutW(t.window,kPrivateProbe,0,0,SMTO_ABORTIFHUNG,500,&result)!=0;Log("{\"event\":\"private_probe\",\"token\":%llu,\"which\":%u,\"sent\":%s,\"result\":%llu}",token,which,sent?"true":"false",(unsigned long long)result);return 0;}
 }
 return DefWindowProcW(h,m,w,l);
}
void ChooseText(){for(wchar_t cp:{(wchar_t)0xE9,(wchar_t)0xA3,(wchar_t)0x3042,(wchar_t)0x4F60}){wchar_t source[]={L's',L'e',L'n',L't',L' ',cp,0};char narrow[128]{};wchar_t roundtrip[128]{};int n=WideCharToMultiByte(CP_ACP,0,source,-1,narrow,128,nullptr,nullptr);if(n>0&&MultiByteToWideChar(CP_ACP,0,narrow,n,roundtrip,128)>0&&!wcscmp(source,roundtrip)){textW=source;textA=narrow;sentinelCodePoint=cp;return;}}textW=L"sentinel ASCII";textA="sentinel ASCII";}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc!=2){if(argv)LocalFree(argv);return 2;}output=_wfsopen(argv[1],L"w",_SH_DENYNO);LocalFree(argv);if(!output)return 3;
 if(!vi_cursor_spy::Install()||!vi_cursor_spy::VerifyBeforeProduct())return 4;uiThread=GetCurrentThreadId();ChooseText();
 WNDCLASSA a{};a.hInstance=instance;a.lpfnWndProc=OriginalA;a.lpszClassName="ViMixedOriginalA";if(!RegisterClassA(&a))return 5;
 WNDCLASSW b{};b.hInstance=instance;b.lpfnWndProc=OriginalW;b.lpszClassName=L"ViMixedOriginalW";if(!RegisterClassW(&b))return 5;
 WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Controls;c.lpszClassName=L"ViMixedControl";if(!RegisterClassW(&c))return 5;
 targets[0].originalUnicode=false;targets[1].originalUnicode=true;
 targets[0].window=CreateWindowExA(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,a.lpszClassName,"Owned ANSI",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,700,640,480,nullptr,nullptr,instance,nullptr);
 targets[1].window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,b.lpszClassName,L"Owned Unicode",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,700,640,480,nullptr,nullptr,instance,nullptr);
 control=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"Owned controls",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);if(!control||!targets[0].window||!targets[1].window)return 6;
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"ansi\":%llu,\"unicode\":%llu,\"control\":%llu,\"safe_spies\":true,\"native_write_trampolines_called\":false,\"code_page\":%u,\"sentinel_code_point\":%u}",GetCurrentProcessId(),uiThread,(unsigned long long)(ULONG_PTR)targets[0].window,(unsigned long long)(ULONG_PTR)targets[1].window,(unsigned long long)(ULONG_PTR)control,GetACP(),sentinelCodePoint);
 const auto deadline=GetTickCount64()+120000;while(GetTickCount64()<deadline){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)return 0;if(filter&&msg.hwnd==targets[filter-1].window&&msg.message==WM_MOUSEMOVE){++targets[filter-1].loopMoves;Log("{\"event\":\"loop_move_consumed\",\"which\":%u}",filter);continue;}TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}return 0;
}
