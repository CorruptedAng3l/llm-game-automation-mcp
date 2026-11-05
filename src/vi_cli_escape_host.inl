// Explicit resident CLI mode. Never entered by claim/attach/inject automatically.
#include "vi_escape_host.h"
#include <memory>
#include <cwchar>
#include <climits>
#include <initializer_list>
#include <limits>
namespace vi_escape_cli {
static unsigned long long Time(const FILETIME& t){return ((unsigned long long)t.dwHighDateTime<<32)|t.dwLowDateTime;}
struct Handle {HANDLE value=nullptr;explicit Handle(HANDLE v=nullptr):value(v){}~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
static bool Image(HANDLE process,std::wstring& path){wchar_t text[32768]{};DWORD n=32768;if(!QueryFullProcessImageNameW(process,0,text,&n)||!n||n>=32768||text[n])return false;wchar_t full[32768]{};const DWORD size=GetFullPathNameW(text,32768,full,nullptr);if(!size||size>=32768)return false;path.assign(full,size);return !path.empty();}
struct Process {
 Handle handle;DWORD pid=0;unsigned long long creation=0;std::wstring image;
 explicit Process(HANDLE h):handle(h){}
 vi_escape::Witness Check()const{
  if(!handle.value||!pid)return vi_escape::Witness::Missing;
  const DWORD wait=WaitForSingleObject(handle.value,0);if(wait==WAIT_OBJECT_0)return vi_escape::Witness::Dead;if(wait!=WAIT_TIMEOUT)return vi_escape::Witness::QueryFailed;
  if(GetProcessId(handle.value)!=pid)return vi_escape::Witness::PidMismatch;
  FILETIME c{},e{},k{},u{};if(!GetProcessTimes(handle.value,&c,&e,&k,&u))return vi_escape::Witness::QueryFailed;if(Time(c)!=creation)return vi_escape::Witness::CreationMismatch;
  std::wstring current;if(!Image(handle.value,current))return vi_escape::Witness::ImageUnavailable;if(_wcsicmp(current.c_str(),image.c_str())!=0)return vi_escape::Witness::ImageMismatch;
  return vi_escape::Witness::Valid;
 }
};
static std::shared_ptr<Process> Pin(DWORD pid){
 auto p=std::make_shared<Process>(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));if(!p->handle.value)return {};
 FILETIME c{},e{},k{},u{};p->pid=pid;if(GetProcessId(p->handle.value)!=pid||!GetProcessTimes(p->handle.value,&c,&e,&k,&u)||!Image(p->handle.value,p->image))return {};p->creation=Time(c);if(p->Check()!=vi_escape::Witness::Valid)return {};return p;
}
static HWND Root(HWND h){return h?GetAncestor(h,GA_ROOT):nullptr;}
struct Window {
 HWND hwnd=nullptr;DWORD thread=0;Handle threadHandle;unsigned long long threadCreation=0;ATOM atom=0;bool unicode=false;std::atomic<bool> invalidated{false};std::shared_ptr<Process> process;
 explicit Window(HANDLE h):threadHandle(h){}
 vi_escape::Witness Check()const{
  if(invalidated)return vi_escape::Witness::Destroyed;
  if(!hwnd||!process)return vi_escape::Witness::Missing;const auto alive=process->Check();if(alive!=vi_escape::Witness::Valid)return alive;
  if(!IsWindow(hwnd)||Root(hwnd)!=hwnd)return vi_escape::Witness::WindowGone;
  DWORD pid=0;const DWORD currentThread=GetWindowThreadProcessId(hwnd,&pid);if(!currentThread)return vi_escape::Witness::QueryFailed;if(pid!=process->pid)return vi_escape::Witness::PidMismatch;if(currentThread!=thread)return vi_escape::Witness::ThreadMismatch;
  if(!threadHandle.value)return vi_escape::Witness::Missing;const DWORD wait=WaitForSingleObject(threadHandle.value,0);if(wait==WAIT_OBJECT_0)return vi_escape::Witness::Dead;if(wait!=WAIT_TIMEOUT)return vi_escape::Witness::QueryFailed;
  if(GetThreadId(threadHandle.value)!=thread)return vi_escape::Witness::ThreadMismatch;if(GetProcessIdOfThread(threadHandle.value)!=process->pid)return vi_escape::Witness::PidMismatch;
  FILETIME c{},e{},k{},u{};if(!GetThreadTimes(threadHandle.value,&c,&e,&k,&u))return vi_escape::Witness::QueryFailed;if(Time(c)!=threadCreation)return vi_escape::Witness::CreationMismatch;
  if((ATOM)GetClassLongPtrW(hwnd,GCW_ATOM)!=atom||(IsWindowUnicode(hwnd)!=FALSE)!=unicode)return vi_escape::Witness::MetadataChanged;
  return invalidated?vi_escape::Witness::Destroyed:vi_escape::Witness::Valid;
 }
};
static std::shared_ptr<Window> Observe(HWND h){
 h=Root(h);DWORD pid=0;const DWORD thread=h?GetWindowThreadProcessId(h,&pid):0;if(!thread||!pid||pid==GetCurrentProcessId())return {};
 auto process=Pin(pid);if(!process)return {};auto w=std::make_shared<Window>(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,thread));if(!w->threadHandle.value)return {};
 FILETIME c{},e{},k{},u{};if(!GetThreadTimes(w->threadHandle.value,&c,&e,&k,&u))return {};w->hwnd=h;w->thread=thread;w->threadCreation=Time(c);w->process=process;w->atom=(ATOM)GetClassLongPtrW(h,GCW_ATOM);w->unicode=IsWindowUnicode(h)!=FALSE;
 if(!w->atom||w->Check()!=vi_escape::Witness::Valid||Root(GetForegroundWindow())!=h)return {};return w;
}
struct Host {
 std::shared_ptr<Process> target;std::shared_ptr<Window> previous,decisionPrevious;HWND foregroundAtDecision=nullptr;vi_escape::Witness previousObservation=vi_escape::Witness::Missing;
 void ObservedForeground(HWND h){h=Root(h);if(!h||Root(GetForegroundWindow())!=h)return;DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==target->pid||pid==GetCurrentProcessId())return;
  if(previous&&previous->hwnd==h&&previous->Check()==vi_escape::Witness::Valid)return;
  previous=Observe(h);previousObservation=previous?vi_escape::Witness::Valid:vi_escape::Witness::QueryFailed; // No older guess after a failed newest observation.
 }
};
static Host* active=nullptr;
static void CALLBACK Event(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG child,DWORD,DWORD){if(!active)return;if(event==EVENT_SYSTEM_FOREGROUND){active->ObservedForeground(h);return;}if(object==OBJID_WINDOW&&child==CHILDID_SELF){for(const auto& w:{active->previous,active->decisionPrevious})if(w&&h==w->hwnd)w->invalidated=true;}}
static vi_escape::Rect Convert(const RECT& r){return {r.left,r.top,r.right,r.bottom};}
struct Operations {
 Host& host;std::shared_ptr<Window> selected;vi_escape::Witness selectedObservation;
 explicit Operations(Host& h):host(h),selected(h.previous),selectedObservation(h.previousObservation){host.decisionPrevious=selected;}
 ~Operations(){if(host.decisionPrevious==selected)host.decisionPrevious.reset();}
 vi_escape::Api Clear(){SetLastError(ERROR_SUCCESS);const BOOL ok=ClipCursor(nullptr);return {ok!=FALSE,ok?0u:GetLastError(),!ok};}
 vi_escape::Clip QueryClip(){vi_escape::Clip out;RECT r{};SetLastError(ERROR_SUCCESS);const BOOL ok=GetClipCursor(&r);out.query={ok!=FALSE,ok?0u:GetLastError(),!ok};if(ok)out.rectangle=Convert(r);
  const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if(w>0&&h>0&&(long long)x+w<=LONG_MAX&&(long long)y+h<=LONG_MAX){out.desktop={x,y,x+w,y+h};out.desktopKnown=true;}return out;
 }
 vi_escape::Witness Target(){return host.target?host.target->Check():vi_escape::Witness::Missing;}
 vi_escape::Witness Previous(){return selected?selected->Check():selectedObservation;}
 vi_escape::Foreground CurrentForeground(){host.foregroundAtDecision=Root(GetForegroundWindow());if(!host.foregroundAtDecision)return vi_escape::Foreground::Unknown;DWORD pid=0;if(!GetWindowThreadProcessId(host.foregroundAtDecision,&pid)||!pid)return vi_escape::Foreground::Unknown;return pid==host.target->pid?vi_escape::Foreground::Target:vi_escape::Foreground::External;}
 vi_escape::Handback HandBack(){vi_escape::Handback out;
  if(Target()!=vi_escape::Witness::Valid||Previous()!=vi_escape::Witness::Valid||Root(GetForegroundWindow())!=host.foregroundAtDecision){out.reason="scope_changed_before_handback";return out;}
  DWORD pid=0;if(!host.foregroundAtDecision||!GetWindowThreadProcessId(host.foregroundAtDecision,&pid)||pid!=host.target->pid){out.reason="foreground_no_longer_target";return out;}
  if(!IsWindowVisible(selected->hwnd)||IsIconic(selected->hwnd)){out.reason="previous_window_not_visible_or_minimized";return out;}
  out.attempted=true;out.call.ok=SetForegroundWindow(selected->hwnd)!=FALSE; // No documented GetLastError contract.
  HWND actual=Root(GetForegroundWindow());DWORD actualPid=0;if(actual)GetWindowThreadProcessId(actual,&actualPid);out.actualWindow=(std::uint64_t)(ULONG_PTR)actual;out.actualPid=actualPid;out.confirmationKnown=actual!=nullptr&&actualPid!=0;out.confirmed=actual==selected->hwnd&&Previous()==vi_escape::Witness::Valid;
  out.reason=out.call.ok?(out.confirmed?"foreground_confirmed":"foreground_unconfirmed"):"foreground_api_denied";return out;
 }
};
static const char* Bool(bool value){return value?"true":"false";}
static std::string ApiJson(const vi_escape::Api& a){return std::string("{\"ok\":")+Bool(a.ok)+",\"error\":"+(a.errorKnown?std::to_string(a.error):"null")+"}";}
static std::string RectJson(const vi_escape::Rect& r){return "["+std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right)+","+std::to_string(r.bottom)+"]";}
static std::string Hwnd(HWND h){char text[32];sprintf_s(text,"0x%llX",(unsigned long long)(ULONG_PTR)h);return text;}
static void Print(const vi_escape::Result& r,const Operations& op){const auto& p=op.selected;const char* scope=r.foreground==vi_escape::Foreground::Target?"target":r.foreground==vi_escape::Foreground::External?"external":"unknown";
 std::string text=std::string("{\"ok\":")+Bool(r.ok)+",\"mode\":\"escape-host\",\"event\":\"hotkey\",\"message_accepted\":"+Bool(r.accepted)+",\"reason\":\""+r.reason+"\",\"global_clear_attempted\":"+Bool(r.clearAttempted);
 text+=",\"clear_api\":"+ApiJson(r.clear)+",\"clip_query\":"+ApiJson(r.after.query)+",\"clip_after\":"+(r.after.query.ok?RectJson(r.after.rectangle):"null")+",\"virtual_desktop\":"+(r.after.desktopKnown?RectJson(r.after.desktop):"null");
 text+=std::string(",\"clear_confirmation_known\":")+Bool(r.clipConfirmationKnown)+",\"clear_confirmed_at_query\":"+Bool(r.clipConfirmed)+",\"target_witness\":\""+vi_escape::Name(r.target)+"\",\"previous_witness\":\""+vi_escape::Name(r.previous)+"\"";
 text+=",\"target_identity\":{\"pid\":"+std::to_string(op.host.target->pid)+",\"creation_utc_100ns\":\""+std::to_string(op.host.target->creation)+"\",\"image\":\""+JEsc(W2U(op.host.target->image))+"\"}";
 text+=",\"previous_identity\":"+(p?"{\"hwnd\":\""+Hwnd(p->hwnd)+"\",\"pid\":"+std::to_string(p->process->pid)+",\"thread\":"+std::to_string(p->thread)+",\"process_creation_utc_100ns\":\""+std::to_string(p->process->creation)+"\",\"thread_creation_utc_100ns\":\""+std::to_string(p->threadCreation)+"\",\"image\":\""+JEsc(W2U(p->process->image))+"\"}":"null");
 text+=std::string(",\"foreground_scope\":\"")+scope+"\",\"foreground_before_hwnd\":\""+Hwnd(op.host.foregroundAtDecision)+"\",\"foreground_attempted\":"+Bool(r.handback.attempted)+",\"foreground_api\":"+ApiJson(r.handback.call);
 text+=std::string(",\"foreground_confirmation_known\":")+Bool(r.handback.confirmationKnown)+",\"foreground_confirmed\":"+Bool(r.handback.confirmed)+",\"foreground_after_hwnd\":"+(r.handback.confirmationKnown?"\""+Hwnd((HWND)(ULONG_PTR)r.handback.actualWindow)+"\"":"null")+",\"foreground_after_pid\":"+(r.handback.confirmationKnown?std::to_string(r.handback.actualPid):"null");
 text+=",\"virtual_jobs_cancelled\":false,\"virtual_input_released\":false,\"guaranteed\":false}";printf("%s\n",text.c_str());fflush(stdout);
}
static bool Number(const std::string& text,unsigned long long& value){if(text.empty())return false;value=0;for(unsigned char c:text){if(c<'0'||c>'9'||value>((std::numeric_limits<unsigned long long>::max)()-(c-'0'))/10)return false;value=value*10+c-'0';}return true;}
static int Run(const std::vector<std::string>& raw){
 DWORD pid=0;unsigned long long timeout=28800000;bool havePid=false,haveTimeout=false,haveCommand=false;std::string error;
 for(size_t i=0;i<raw.size();++i){const auto& token=raw[i];if(token=="escape-host"&&!haveCommand){haveCommand=true;continue;}if((token=="--pid"&&!havePid)||(token=="--timeout-ms"&&!haveTimeout)){
   if(i+1>=raw.size()){error="escape-host option needs a decimal value";break;}unsigned long long number=0;if(!Number(raw[++i],number)){error="escape-host needs whole decimal PID/timeout values";break;}
   if(token=="--pid"){if(!number||number>MAXDWORD){error="escape-host PID must be1..4294967295";break;}pid=(DWORD)number;havePid=true;}
   else{if(number<1000||number>86400000){error="escape-host timeout-ms must be1000..86400000";break;}timeout=number;haveTimeout=true;}continue;
  }error="escape-host accepts exactly --pid N and optional --timeout-ms N; duplicate/unknown options are refused";break;
 }
 if(!havePid||!haveCommand||!error.empty()){printf("{\"ok\":false,\"mode\":\"escape-host\",\"registered\":false,\"error\":\"%s\"}\n",JEsc(error.empty()?"escape-host needs --pid N":error).c_str());return 2;}
 Host host;host.target=Pin(pid);if(!host.target){printf("{\"ok\":false,\"registered\":false,\"error\":\"strict original-handle target identity unavailable\",\"win32_error\":null}\n");return 2;}
 // No unclipping, foreground write or game IPC occurs anywhere in setup/teardown.
 active=&host;HWINEVENTHOOK fg=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,&Event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);const DWORD fgError=fg?0:GetLastError();
 HWINEVENTHOOK life=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_DESTROY,nullptr,&Event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);const DWORD lifeError=life?0:GetLastError();
 if(!fg||!life){if(fg)UnhookWinEvent(fg);if(life)UnhookWinEvent(life);active=nullptr;printf("{\"ok\":false,\"registered\":false,\"error\":\"foreground/lifetime observation registration failed\",\"foreground_hook_error\":%lu,\"lifetime_hook_error\":%lu}\n",fgError,lifeError);return 2;}
 host.ObservedForeground(GetForegroundWindow());MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
 if(!RegisterHotKey(nullptr,(int)vi_escape::HotkeyId,vi_escape::Modifiers|MOD_NOREPEAT,vi_escape::EndKey)){const DWORD e=GetLastError();UnhookWinEvent(fg);UnhookWinEvent(life);active=nullptr;printf("{\"ok\":false,\"registered\":false,\"error\":\"Ctrl+Alt+Shift+End registration failed; another host/application may own it\",\"win32_error\":%lu}\n",e);return 2;}
 const UINT_PTR timer=SetTimer(nullptr,0,1000,nullptr);if(!timer){const DWORD e=GetLastError();UnregisterHotKey(nullptr,(int)vi_escape::HotkeyId);UnhookWinEvent(fg);UnhookWinEvent(life);active=nullptr;printf("{\"ok\":false,\"registered\":false,\"error\":\"escape timeout timer registration failed\",\"win32_error\":%lu}\n",e);return 2;}
 printf("{\"ok\":true,\"mode\":\"escape-host\",\"event\":\"registered\",\"registered\":true,\"hotkey\":\"Ctrl+Alt+Shift+End\",\"target_pid\":%lu,\"host_pid\":%lu,\"host_thread\":%lu,\"previous_available\":%s,\"timeout_ms\":%llu,\"global_clear_only_on_hotkey\":true,\"message_authenticated\":false,\"virtual_jobs_cancelled\":false,\"guaranteed\":false}\n",pid,GetCurrentProcessId(),GetCurrentThreadId(),Bool(host.previous!=nullptr),timeout);fflush(stdout);
 const ULONGLONG started=GetTickCount64();bool messageError=false;DWORD messageErrorCode=0;while(GetTickCount64()-started<timeout){const BOOL got=GetMessageW(&message,nullptr,0,0);if(got<=0){messageError=got<0;if(messageError)messageErrorCode=GetLastError();break;}if(message.message==WM_HOTKEY){
   // WM_HOTKEY delivers ALT/CONTROL/SHIFT/WIN bits only. MOD_NOREPEAT is
   // registration behavior, not one of the documented delivered modifier bits.
   vi_escape::Event input{true,true,(unsigned)message.wParam,(unsigned)LOWORD(message.lParam),(unsigned)HIWORD(message.lParam)};Operations operations(host);Print(vi_escape::Handle(input,operations),operations);
  }else{TranslateMessage(&message);DispatchMessageW(&message);}}
 const bool timerRemoved=KillTimer(nullptr,timer)!=FALSE,keyRemoved=UnregisterHotKey(nullptr,(int)vi_escape::HotkeyId)!=FALSE,fgRemoved=UnhookWinEvent(fg)!=FALSE,lifeRemoved=UnhookWinEvent(life)!=FALSE;active=nullptr;
 printf("{\"ok\":%s,\"mode\":\"escape-host\",\"event\":\"stopped\",\"timer_removed\":%s,\"hotkey_unregistered\":%s,\"foreground_hook_removed\":%s,\"lifetime_hook_removed\":%s,\"message_error\":%s,\"message_error_code\":%lu,\"elapsed_ms\":%llu,\"desktop_cleanup_attempted\":false}\n",Bool(!messageError&&timerRemoved&&keyRemoved&&fgRemoved&&lifeRemoved),Bool(timerRemoved),Bool(keyRemoved),Bool(fgRemoved),Bool(lifeRemoved),Bool(messageError),messageErrorCode,GetTickCount64()-started);return messageError||!timerRemoved||!keyRemoved||!fgRemoved||!lifeRemoved?1:0;
}
} // namespace vi_escape_cli
