// Distinct-class / retained outer-hook reproducer. Every desktop-input and
// activation API is absorbed below the product. Recursion is bounded in Outer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <mutex>
#include "vi_retargetrelease_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static constexpr UINT kProbe=WM_APP+0xB0;
static constexpr LRESULT kA=0xA01,kB=0xB02,kCycle=0xC03;
static FILE* out=nullptr;static HWND a=nullptr,b=nullptr,control=nullptr;static std::mutex logMx;
static std::atomic<WNDPROC> savedA{nullptr};static std::atomic<unsigned long long> callsA{0},callsB{0},wrongClass{0},cycles{0},outerCalls{0};static thread_local unsigned outerDepth=0;
struct KeyLatch{std::atomic<unsigned long long> downs{0},ups{0};std::atomic<bool> held{false};};
static KeyLatch latchA,latchB;
static std::atomic<unsigned long long> caseToken{0};
static std::atomic<bool> inputPaused{false};
static ULONGLONG pauseDeadline=0;
static unsigned pauseTimeouts=0;
static bool ReleaseIdentity(HWND);
static bool LatchKey(HWND,UINT,WPARAM,LPARAM,bool);
static void Log(const char* format,...){std::lock_guard<std::mutex> lock(logMx);va_list args;va_start(args,format);vfprintf(out,format,args);va_end(args);fputc('\n',out);fflush(out);}
static void Snapshot(unsigned long long token,const char* event){Log("{\"event\":\"%s\",\"token\":%llu,\"a_calls\":%llu,\"b_calls\":%llu,\"wrong_class\":%llu,\"cycles\":%llu,\"outer_calls\":%llu,\"native_writes_absorbed\":%llu,\"foreground_absorbed\":%llu}",event,token,callsA.load(),callsB.load(),wrongClass.load(),cycles.load(),outerCalls.load(),vi_cursor_spy::Total(),vi_cursor_spy::foregroundEffects.load());}
static LRESULT Probe(HWND h,WPARAM token,bool classA){wchar_t name[80]{};GetClassNameW(h,name,80);const bool expected=wcscmp(name,classA?L"ViRetargetA":L"ViRetargetB")==0;if(classA)++callsA;else++callsB;if(!expected)++wrongClass;Log("{\"event\":\"original_proc\",\"token\":%llu,\"procedure\":\"%s\",\"hwnd\":%llu,\"class_matches_procedure\":%s}",(unsigned long long)token,classA?"A":"B",(unsigned long long)(ULONG_PTR)h,expected?"true":"false");return classA?kA:kB;}
static LRESULT CALLBACK ProcA(HWND h,UINT m,WPARAM w,LPARAM l){if(LatchKey(h,m,w,l,true))return 0;if(m==kProbe)return Probe(h,w,true);if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK ProcB(HWND h,UINT m,WPARAM w,LPARAM l){if(LatchKey(h,m,w,l,false))return 0;if(m==kProbe)return Probe(h,w,false);if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcA(h,m,w,l);}
static LRESULT CALLBACK Outer(HWND h,UINT m,WPARAM w,LPARAM l){struct Depth{Depth(){++outerDepth;}~Depth(){--outerDepth;}} depth;++outerCalls;
 if(outerDepth>1){++cycles;Log("{\"event\":\"bounded_cycle\",\"token\":%llu,\"message\":%u,\"depth\":%u,\"hwnd\":%llu}",(unsigned long long)w,m,outerDepth,(unsigned long long)(ULONG_PTR)h);return m==kProbe?kCycle:0;}
 const WNDPROC next=savedA.load();return next?CallWindowProcW(next,h,m,w,l):DefWindowProcW(h,m,w,l);
}
// This test-only lower read hook remains inert until the explicit late control.
// Every mutation is restricted to A's exact own PID/TID/class/property identity.
using WindowLongRead=LONG_PTR(WINAPI*)(HWND,int);
using WindowLongWrite=LONG_PTR(WINAPI*)(HWND,int,LONG_PTR);
static WindowLongRead nativeRead=nullptr;
static WindowLongWrite nativeWrite=nullptr;
static void* readEntry=nullptr;
static unsigned char setterBytes[16]{};
static DWORD uiThread=0;
static int lifetimeStorage=0;
static constexpr wchar_t lifetimeProperty[]=L"vi.fixture.retarget.late.lifetime.v1";
static std::atomic<bool> lateArmed{false},lateInstalled{false};
static std::atomic<unsigned> lateOrdinal{0};
static std::atomic<unsigned long long> lateToken{0};
static ULONG_PTR Lifetime(){return reinterpret_cast<ULONG_PTR>(&lifetimeStorage);}
static bool ReleaseIdentity(HWND h){
 if(h!=a&&h!=b)return false;
 DWORD owner=0;wchar_t name[80]{};const bool first=h==a;
 return GetWindowThreadProcessId(h,&owner)==uiThread&&owner==GetCurrentProcessId()&&
  (IsWindowUnicode(h)!=FALSE)==first&&GetClassNameW(h,name,80)>0&&
  wcscmp(name,first?L"ViRetargetA":L"ViRetargetB")==0&&
  reinterpret_cast<ULONG_PTR>(GetPropW(h,lifetimeProperty))==(Lifetime()^(first?0u:1u));
}
static bool LatchKey(HWND h,UINT m,WPARAM w,LPARAM l,bool procedureA){
 if(w!=VK_F6||(m!=WM_KEYDOWN&&m!=WM_KEYUP))return false;
 const bool down=m==WM_KEYDOWN,identity=ReleaseIdentity(h),procedureMatches=procedureA?h==a:h==b;auto& latch=h==a?latchA:latchB;
 if(!procedureMatches)++wrongClass;
 if(identity&&procedureMatches){if(down)++latch.downs;else ++latch.ups;latch.held=down;}
 Log("{\"event\":\"key_%s\",\"token\":%llu,\"target\":\"%s\",\"procedure\":\"%s\",\"procedure_matches_window\":%s,\"hwnd\":%llu,\"pid\":%lu,\"thread\":%lu,\"identity_live\":%s,\"message\":%u,\"wparam\":%llu,\"lparam\":%llu,\"held\":%s,\"downs\":%llu,\"ups\":%llu,\"input_paused\":%s}",down?"down":"up",caseToken.load(),h==a?"A":"B",procedureA?"A":"B",procedureMatches?"true":"false",(unsigned long long)(ULONG_PTR)h,GetCurrentProcessId(),GetCurrentThreadId(),identity?"true":"false",m,(unsigned long long)w,(unsigned long long)(ULONG_PTR)l,latch.held?"true":"false",latch.downs.load(),latch.ups.load(),inputPaused?"true":"false");
 return true; // The synthetic application consumes its latched F6 messages.
}
using NativePostFn=BOOL(WINAPI*)(HWND,UINT,WPARAM,LPARAM);
static NativePostFn nativePost=nullptr;static void* postEntry=nullptr;
static std::atomic<unsigned long long> postSequence{0};
struct PostedUp{unsigned long long sequence=0,token=0;UINT message=0;WPARAM w=0;LPARAM l=0;HWND window=nullptr;bool accepted=false,paused=false;};
static std::mutex upMx;static PostedUp lastUp;
static BOOL WINAPI ObservePost(HWND h,UINT m,WPARAM w,LPARAM l){
 const DWORD entryError=GetLastError();const bool inspect=(h==a||h==b)&&w==VK_F6&&(m==WM_KEYDOWN||m==WM_KEYUP)&&ReleaseIdentity(h);
 SetLastError(entryError);const BOOL accepted=nativePost(h,m,w,l);const DWORD error=GetLastError();
 if(inspect){const auto sequence=++postSequence,token=caseToken.load();const bool paused=inputPaused.load();
  if(m==WM_KEYUP){std::lock_guard<std::mutex> lock(upMx);lastUp={sequence,token,m,w,l,h,accepted!=FALSE,paused};}
  Log("{\"event\":\"posted_%s\",\"token\":%llu,\"post_sequence\":%llu,\"accepted\":%s,\"post_error\":%lu,\"target\":\"%s\",\"hwnd\":%llu,\"pid\":%lu,\"target_thread\":%lu,\"caller_thread\":%lu,\"lifetime_cookie\":%llu,\"message\":%u,\"wparam\":%llu,\"lparam\":%llu,\"input_paused\":%s}",m==WM_KEYDOWN?"down":"up",token,sequence,accepted?"true":"false",accepted?0:error,h==a?"A":"B",(unsigned long long)(ULONG_PTR)h,GetCurrentProcessId(),uiThread,GetCurrentThreadId(),(unsigned long long)(Lifetime()^(h==a?0u:1u)),m,(unsigned long long)w,(unsigned long long)(ULONG_PTR)l,paused?"true":"false");
 }
 SetLastError(error);return accepted;
}
static bool InstallPostObserver(){
 postEntry=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"PostMessageW");if(!postEntry)return false;
 void* trampoline=nullptr;if(VIMH_CreateHook(postEntry,(void*)ObservePost,&trampoline)!=MH_OK||!trampoline)return false;
 nativePost=(NativePostFn)trampoline;return VIMH_EnableHook(postEntry)==MH_OK&&vi_cursor_spy::Reaches(postEntry,(void*)ObservePost);
}
static void ReleaseSnapshot(unsigned long long token,const char* event){
 BYTE keys[256]{};const BOOL keyboard=GetKeyboardState(keys);const SHORT async=GetAsyncKeyState(VK_F6),key=GetKeyState(VK_F6);
 Log("{\"event\":\"%s\",\"token\":%llu,\"case_token\":%llu,\"a_identity_live\":%s,\"b_identity_live\":%s,\"a_downs\":%llu,\"a_ups\":%llu,\"a_held\":%s,\"b_downs\":%llu,\"b_ups\":%llu,\"b_held\":%s,\"async_down\":%s,\"key_down\":%s,\"keyboard_ok\":%s,\"keyboard_down\":%s,\"input_paused\":%s,\"pause_timeouts\":%u,\"a_top_proc\":%llu,\"b_top_proc\":%llu,\"a_original_proc\":%llu,\"outer_proc\":%llu,\"cycles\":%llu,\"wrong_class\":%llu,\"native_writes_absorbed\":%llu,\"foreground_absorbed\":%llu}",event,token,caseToken.load(),ReleaseIdentity(a)?"true":"false",ReleaseIdentity(b)?"true":"false",latchA.downs.load(),latchA.ups.load(),latchA.held?"true":"false",latchB.downs.load(),latchB.ups.load(),latchB.held?"true":"false",(async&0x8000)?"true":"false",(key&0x8000)?"true":"false",keyboard?"true":"false",(keys[VK_F6]&0x80)?"true":"false",inputPaused?"true":"false",pauseTimeouts,(unsigned long long)(ULONG_PTR)GetWindowLongPtrW(a,GWLP_WNDPROC),(unsigned long long)(ULONG_PTR)GetWindowLongPtrA(b,GWLP_WNDPROC),(unsigned long long)(ULONG_PTR)ProcA,(unsigned long long)(ULONG_PTR)Outer,cycles.load(),wrongClass.load(),vi_cursor_spy::Total(),vi_cursor_spy::foregroundEffects.load());
}
static bool OwnA(){
 DWORD owner=0;wchar_t name[80]{};
 return a&&GetWindowThreadProcessId(a,&owner)==uiThread&&owner==GetCurrentProcessId()&&
  IsWindowUnicode(a)&&GetClassNameW(a,name,80)>0&&wcscmp(name,L"ViRetargetA")==0&&
  reinterpret_cast<ULONG_PTR>(GetPropW(a,lifetimeProperty))==Lifetime();
}
static bool SetterUnchanged(){unsigned char now[sizeof(setterBytes)]{};
 return nativeWrite&&vi_cursor_spy::Read(reinterpret_cast<void*>(nativeWrite),now,sizeof(now))&&memcmp(now,setterBytes,sizeof(now))==0;
}
static LONG_PTR WINAPI LateRead(HWND h,int index){
 if(h!=a||index!=GWLP_WNDPROC||!lateArmed.load())return nativeRead(h,index);
 const LONG_PTR current=nativeRead(h,index);
 const unsigned ordinal=++lateOrdinal;const auto token=lateToken.load();
 const bool identity=OwnA();
 Log("{\"event\":\"late_query\",\"token\":%llu,\"ordinal\":%u,\"hwnd\":%llu,\"pid\":%lu,\"thread\":%lu,\"caller_thread\":%lu,\"lifetime_cookie\":%llu,\"identity_live\":%s,\"observed_proc\":%llu}",token,ordinal,(unsigned long long)(ULONG_PTR)h,GetCurrentProcessId(),uiThread,GetCurrentThreadId(),(unsigned long long)Lifetime(),identity?"true":"false",(unsigned long long)(ULONG_PTR)current);
 if(ordinal!=2)return current;
 // Single attempt, even if the identity/procedure witness fails. Never recurse
 // through the hooked getter, and never remove any unrecognized outer layer.
 lateArmed=false;
 const auto expected=(LONG_PTR)ProcA;LONG_PTR prior=0,after=current;
 const bool setter=SetterUnchanged();bool ok=identity&&current==expected&&setter;
 if(ok){savedA=ProcA;SetLastError(0);prior=nativeWrite(a,GWLP_WNDPROC,(LONG_PTR)Outer);after=nativeRead(a,GWLP_WNDPROC);
  lateInstalled=prior==expected;ok=prior==expected&&after==(LONG_PTR)Outer&&OwnA();}
 Log("{\"event\":\"late_switch\",\"token\":%llu,\"ok\":%s,\"ordinal\":%u,\"hwnd\":%llu,\"pid\":%lu,\"thread\":%lu,\"caller_thread\":%lu,\"lifetime_cookie\":%llu,\"identity_live\":%s,\"setter_preproduct_unchanged\":%s,\"old_proc\":%llu,\"saved_proc\":%llu,\"prior_proc\":%llu,\"new_proc\":%llu,\"outer_proc\":%llu}",token,ok?"true":"false",ordinal,(unsigned long long)(ULONG_PTR)h,GetCurrentProcessId(),uiThread,GetCurrentThreadId(),(unsigned long long)Lifetime(),identity?"true":"false",setter?"true":"false",(unsigned long long)(ULONG_PTR)current,(unsigned long long)(ULONG_PTR)savedA.load(),(unsigned long long)(ULONG_PTR)prior,(unsigned long long)(ULONG_PTR)after,(unsigned long long)(ULONG_PTR)Outer);
 return after;
}
static bool InstallLateRead(){
 const auto module=GetModuleHandleW(L"user32.dll");if(!module)return false;
#ifdef _WIN64
 readEntry=(void*)GetProcAddress(module,"GetWindowLongPtrW");nativeWrite=(WindowLongWrite)GetProcAddress(module,"SetWindowLongPtrW");
#else
 readEntry=(void*)GetProcAddress(module,"GetWindowLongW");nativeWrite=(WindowLongWrite)GetProcAddress(module,"SetWindowLongW");
#endif
 if(!readEntry||!nativeWrite||!vi_cursor_spy::Read((void*)nativeWrite,setterBytes,sizeof(setterBytes)))return false;
 void* trampoline=nullptr;const auto made=VIMH_CreateHook(readEntry,(void*)LateRead,&trampoline);
 if(made!=MH_OK||!trampoline)return false;
 nativeRead=(WindowLongRead)trampoline; // publish before enabling any callback
 return VIMH_EnableHook(readEntry)==MH_OK&&vi_cursor_spy::Reaches(readEntry,(void*)LateRead)&&SetterUnchanged();
}
static void ArmLate(unsigned long long token){
 lateArmed=false;lateOrdinal=0;lateToken=token;
 const auto current=nativeRead?nativeRead(a,GWLP_WNDPROC):0;
 const bool entry=readEntry&&vi_cursor_spy::Reaches(readEntry,(void*)LateRead);
 const bool ok=!lateInstalled&&OwnA()&&entry&&SetterUnchanged()&&current==(LONG_PTR)ProcA;
 lateArmed=ok; // publish before the acknowledgement can be observed by the runner
 Log("{\"event\":\"late_armed\",\"token\":%llu,\"ok\":%s,\"hwnd\":%llu,\"pid\":%lu,\"thread\":%lu,\"lifetime_cookie\":%llu,\"expected_proc\":%llu,\"current_proc\":%llu,\"outer_proc\":%llu,\"read_entry_verified\":%s,\"switch_query_ordinal\":2}",token,ok?"true":"false",(unsigned long long)(ULONG_PTR)a,GetCurrentProcessId(),uiThread,(unsigned long long)Lifetime(),(unsigned long long)(ULONG_PTR)ProcA,(unsigned long long)(ULONG_PTR)current,(unsigned long long)(ULONG_PTR)Outer,entry?"true":"false");
}
static void CleanupLate(unsigned long long token){
 lateArmed=false;const bool identity=OwnA();const auto current=identity?nativeRead(a,GWLP_WNDPROC):0;
 bool removed=false;bool ok=identity&&!lateInstalled;
 if(identity&&lateInstalled){
  if(current==(LONG_PTR)Outer&&savedA==ProcA&&SetterUnchanged()){
   removed=nativeWrite(a,GWLP_WNDPROC,(LONG_PTR)ProcA)==(LONG_PTR)Outer;
   ok=removed&&nativeRead(a,GWLP_WNDPROC)==(LONG_PTR)ProcA&&OwnA();if(ok)lateInstalled=false;
  }else if(current==(LONG_PTR)ProcA){lateInstalled=false;ok=true;}
 }
 const auto finalProc=OwnA()?nativeRead(a,GWLP_WNDPROC):0;const bool remaining=finalProc==(LONG_PTR)Outer;
 ok=ok&&OwnA()&&!remaining;
 Log("{\"event\":\"late_cleanup\",\"token\":%llu,\"ok\":%s,\"removed\":%s,\"identity_live\":%s,\"hwnd\":%llu,\"lifetime_cookie\":%llu,\"observed_proc\":%llu,\"final_proc\":%llu,\"remaining_test_outer\":%s}",token,ok?"true":"false",removed?"true":"false",identity?"true":"false",(unsigned long long)(ULONG_PTR)a,(unsigned long long)Lifetime(),(unsigned long long)(ULONG_PTR)current,(unsigned long long)(ULONG_PTR)finalProc,remaining?"true":"false");
}
static LRESULT CALLBACK Controls(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+0xA0){caseToken=(unsigned long long)w;ReleaseSnapshot((unsigned long long)w,"case_begin");return 0;}
 if(m==WM_APP+0xA1){inputPaused=true;pauseDeadline=GetTickCount64()+15000;ReleaseSnapshot((unsigned long long)w,"queue_paused");return 0;}
 if(m==WM_APP+0xA2){
  MSG pending{};const bool identity=ReleaseIdentity(a);const BOOL found=identity?PeekMessageW(&pending,a,WM_KEYUP,WM_KEYUP,PM_NOREMOVE|PM_NOYIELD):FALSE;PostedUp posted;{std::lock_guard<std::mutex> lock(upMx);posted=lastUp;}
  const bool exact=found&&pending.hwnd==a&&pending.message==posted.message&&pending.wParam==posted.w&&pending.lParam==posted.l&&posted.window==a&&posted.token==caseToken&&posted.accepted&&posted.paused;
  Log("{\"event\":\"pending_release\",\"token\":%llu,\"case_token\":%llu,\"input_paused\":%s,\"found\":%s,\"exact_accepted_tuple\":%s,\"post_sequence\":%llu,\"hwnd\":%llu,\"message\":%u,\"wparam\":%llu,\"lparam\":%llu}",(unsigned long long)w,caseToken.load(),inputPaused?"true":"false",found?"true":"false",exact?"true":"false",posted.sequence,(unsigned long long)(ULONG_PTR)pending.hwnd,pending.message,(unsigned long long)pending.wParam,(unsigned long long)(ULONG_PTR)pending.lParam);ReleaseSnapshot((unsigned long long)w,"pending_snapshot");return 0;
 }
 if(m==WM_APP+0xA3){inputPaused=false;const BOOL posted=PostMessageW(control,WM_APP+0xA4,w,0);Log("{\"event\":\"queue_resumed\",\"token\":%llu,\"fence_posted\":%s}",(unsigned long long)w,posted?"true":"false");return 0;}
 if(m==WM_APP+0xA4){ReleaseSnapshot((unsigned long long)w,"queue_drained");return 0;}
 if(m==WM_APP+0xA5){ReleaseSnapshot((unsigned long long)w,"release_snapshot");return 0;}
 if(m==WM_APP+0x96){ArmLate((unsigned long long)w);return 0;}
 if(m==WM_APP+0x97){CleanupLate((unsigned long long)w);return 0;}
 if(m==WM_APP+0x90){const auto current=(WNDPROC)GetWindowLongPtrW(a,GWLP_WNDPROC);bool ok=current&&current!=Outer;if(ok){savedA=current;SetLastError(0);const auto prior=(WNDPROC)SetWindowLongPtrW(a,GWLP_WNDPROC,(LONG_PTR)Outer);ok=prior==current;}Log("{\"event\":\"outer_installed\",\"token\":%llu,\"ok\":%s,\"saved_proc\":%llu}",(unsigned long long)w,ok?"true":"false",(unsigned long long)(ULONG_PTR)savedA.load());return 0;}
 if(m==WM_APP+0x91){ShowWindow(b,SW_SHOWNOACTIVATE);Log("{\"event\":\"b_shown\",\"token\":%llu,\"ok\":true}",(unsigned long long)w);return 0;}
 if(m==WM_APP+0x92||m==WM_APP+0x93){const HWND target=m==WM_APP+0x92?a:b;DWORD owner=0;bool ok=GetWindowThreadProcessId(target,&owner)==GetCurrentThreadId()&&owner==GetCurrentProcessId();DWORD_PTR result=0;if(ok)ok=SendMessageTimeoutW(target,kProbe,w,0,SMTO_ABORTIFHUNG,500,&result)!=0;Log("{\"event\":\"probe\",\"token\":%llu,\"ok\":%s,\"target\":\"%s\",\"result\":%llu}",(unsigned long long)w,ok?"true":"false",target==a?"A":"B",(unsigned long long)result);Snapshot((unsigned long long)w,"snapshot");return 0;}
 if(m==WM_APP+0x94){Snapshot((unsigned long long)w,"snapshot");return 0;}
 if(m==WM_APP+0x95){const auto current=(WNDPROC)GetWindowLongPtrW(a,GWLP_WNDPROC);const auto saved=savedA.load();bool ok=current==Outer&&saved;
  if(ok)ok=(WNDPROC)SetWindowLongPtrW(a,GWLP_WNDPROC,(LONG_PTR)saved)==Outer;
  Log("{\"event\":\"outer_removed\",\"token\":%llu,\"ok\":%s}",(unsigned long long)w,ok?"true":"false");return 0;}

 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count!=2)return 2;out=_wfsopen(args[1],L"w",_SH_DENYNO);LocalFree(args);if(!out)return 3;
 if(!vi_cursor_spy::Install()||!vi_cursor_spy::VerifyBeforeProduct()||!InstallLateRead()||!InstallPostObserver()){Log("{\"event\":\"fatal\",\"safe_spies\":false}");return 4;}
 WNDCLASSW aw{};aw.hInstance=instance;aw.lpfnWndProc=ProcA;aw.lpszClassName=L"ViRetargetA";if(!RegisterClassW(&aw))return 5;
 WNDCLASSA bw{};bw.hInstance=instance;bw.lpfnWndProc=ProcB;bw.lpszClassName="ViRetargetB";if(!RegisterClassA(&bw))return 6;
 WNDCLASSW cw{};cw.hInstance=instance;cw.lpfnWndProc=Controls;cw.lpszClassName=L"ViRetargetControl";if(!RegisterClassW(&cw))return 7;
 a=CreateWindowExW(WS_EX_NOACTIVATE,aw.lpszClassName,L"Retarget A Unicode",WS_OVERLAPPEDWINDOW,-28000,600,640,480,nullptr,nullptr,instance,nullptr);b=CreateWindowExA(WS_EX_NOACTIVATE,bw.lpszClassName,"Retarget B ANSI",WS_OVERLAPPEDWINDOW,-28000,600,640,480,nullptr,nullptr,instance,nullptr);control=CreateWindowExW(WS_EX_NOACTIVATE,cw.lpszClassName,L"Owned controls",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);if(!a||!b||!control)return 8;ShowWindow(a,SW_SHOWNOACTIVATE);
 uiThread=GetCurrentThreadId();if(!SetPropW(a,lifetimeProperty,(HANDLE)Lifetime())||!SetPropW(b,lifetimeProperty,(HANDLE)(Lifetime()^1u))||!OwnA()||!ReleaseIdentity(b))return 9;
 Log("{\"event\":\"ready\",\"pid\":%lu,\"thread\":%lu,\"a\":%llu,\"b\":%llu,\"control\":%llu,\"safe_spies\":true,\"write_trampolines_called\":false,\"outer_recursion_limit\":2,\"late_read_spy_verified\":true,\"late_switch_query_ordinal\":2,\"lifetime_cookie\":%llu}",GetCurrentProcessId(),GetCurrentThreadId(),(unsigned long long)(ULONG_PTR)a,(unsigned long long)(ULONG_PTR)b,(unsigned long long)(ULONG_PTR)control,(unsigned long long)Lifetime());
 Log("{\"event\":\"release_ready\",\"post_observer_verified\":true,\"pause_mode\":\"control-filtered posted-input pause; sent callbacks remain serviced\",\"key\":117,\"pause_limit_ms\":15000}");
 const auto end=GetTickCount64()+120000;while(GetTickCount64()<end){
  if(inputPaused&&GetTickCount64()>=pauseDeadline){inputPaused=false;++pauseTimeouts;Log("{\"event\":\"pause_timeout\",\"token\":%llu}",caseToken.load());}
  MSG message{};const BOOL found=inputPaused?PeekMessageW(&message,control,WM_APP+0x90,WM_APP+0xBF,PM_REMOVE):PeekMessageW(&message,nullptr,0,0,PM_REMOVE);
  if(found){if(message.message==WM_QUIT)return 0;TranslateMessage(&message);DispatchMessageW(&message);}else Sleep(1);
 }return 0;
}
