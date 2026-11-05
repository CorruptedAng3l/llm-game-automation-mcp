// Included after the JSON helpers. Readiness proves infrastructure only, never
// a host's startup or the application's acceptance of an input command.
// A press command arms once per adopted window. Fresh readiness probes remain
// independent, so later clicks retain their own hover/renderer deadlines.
std::atomic<HWND> g_commandReadyWindow{nullptr};
std::atomic<unsigned long long> g_commandReadyGeneration{0};
struct InputReadySnapshot {
 HWND window=nullptr;WNDPROC outer=nullptr;unsigned long long generation=0;bool ready=false;
};
InputReadySnapshot InputReadyWindow() {
 InputReadySnapshot result;std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 result.window=g_hwnd;result.generation=g_inputReadyGeneration;
 DWORD owner=0;RECT client{};
 result.ready=g_jobRetargetDepth==0&&g_wndConfigCommitDepth==0&&result.window&&g_origWndProc&&WndBindingReady(result.window)&&IsWindow(result.window)&&
  GetWindowThreadProcessId(result.window,&owner)&&owner==g_pid&&GetClientRect(result.window,&client)&&client.right>0&&client.bottom>0;
 if(result.ready){result.outer=CurrentWindowProcedure(result.window);result.ready=result.outer!=nullptr;}
 return result;
}
bool InputReadySame(const InputReadySnapshot& old) {
 auto current=InputReadyWindow();return current.ready&&current.window==old.window&&current.outer==old.outer&&current.generation==old.generation;
}
std::string WaitInputReady(unsigned waitMs,bool hooksOnly) {
 const auto began=HousekeepingNow();InputReadySnapshot window;ULONG_PTR ticket=0;bool queue=false;std::string reason;
 for(;;) {
  const bool hooks=g_inputHooksReady.load()&&g_inputHookInstallFinished.load()&&CursorInputProtectionReady();
  if(hooks&&hooksOnly){reason.clear();break;}
  if(!hooks)reason=!CursorApiReady(vi_cursor_guard::KeybdEvent)?(g_cursorInstallFinished?"keybd_event_hook_coverage_failed_or_changed":"keybd_event_hooks_not_armed"):!CursorProtectionReady()?(g_cursorInstallFinished?"cursor_hook_coverage_failed_or_changed":"cursor_hooks_not_armed"):(g_inputHookInstallFinished?"required_keyboard_or_focus_hook_failed":"hooks_not_armed");
  else {
   auto latest=InputReadyWindow();
   if(!latest.ready){window=latest;ticket=0;reason="window_not_adopted_or_subclassed";}
   else {
    if(!ticket||!InputReadySame(window)) {
     window=latest;ticket=++g_inputReadySequence;if(!ticket)ticket=++g_inputReadySequence;
     if(!PostOurs(window.window,g_inputReadyMessage,ticket,(LPARAM)g_inputReadyCookie)){ticket=0;reason="queue_barrier_post_failed";}
    }
    if(ticket) {
     queue=g_inputReadyAck.load()>=ticket&&InputReadySame(window);
     if(queue){reason.clear();break;}reason="queue_not_progressing";
    }
   }
  }
  if(HousekeepingNow()-began>=waitMs)break;
  Sleep(5);
 }
 if(hooksOnly)window=InputReadyWindow();
 const bool hooks=g_inputHooksReady.load()&&g_inputHookInstallFinished.load()&&CursorInputProtectionReady();
 const bool adopted=window.ready&&InputReadySame(window);
 const bool ok=hooks&&(hooksOnly||(adopted&&queue));
 JOut out;out.bol("ok",ok);out.num("pid",g_pid);out.num("readiness_version",1);out.str("version",VI_VERSION);
 out.str("scope",hooksOnly?"hooks":"window_queue");out.bol("hook_ready",hooks);out.bol("hook_install_complete",g_inputHookInstallFinished);
 out.bol("window_ready",adopted);out.bol("queue_ready",adopted&&queue);out.num("waited_ms",(double)(HousekeepingNow()-began));
 out.num("cursor_guard_version",1);out.bol("cursor_hooks_ready",CursorProtectionReady());out.num("keybd_event_guard_version",1);out.bol("keybd_event_hooks_ready",CursorApiReady(vi_cursor_guard::KeybdEvent));out.bol("translator_hook_ready",g_translationInstalled);out.bol("posted_dispatch_hooks_ready",g_postedDispatchReady);out.bol("isolated_character_translation_ready",g_translationInstalled&&g_postedDispatchReady&&g_translationNoState);
 out.num("window_generation",(double)window.generation);char hwnd[32];sprintf_s(hwnd,"0x%llX",(unsigned long long)(ULONG_PTR)window.window);out.str("hwnd",hwnd);
 out.raw("application_ready","null");out.str("application_readiness","not measured: host startup and semantic input acceptance remain application-observed");
 if(!ok){if(reason.empty())reason="readiness_changed_before_reply";out.str("readiness_reason",reason);out.str("error","input infrastructure is not ready: "+reason+"; no input was injected");out.bol("input_injected",false);}
 return out.done();
}
std::string InputReadyCommand(const JObj& request) {
 if(JHas(request,"hooks_only")&&request.find("hooks_only")->second.t!=3)return ErrJ("hooks_only must be a JSON boolean");
 double wait=JNum(request,"wait_ms",1000);if(JHas(request,"wait_ms")&&request.find("wait_ms")->second.t!=2)return ErrJ("wait_ms must be an integer0..10000");
 if(!std::isfinite(wait)||wait<0||wait>10000||std::floor(wait)!=wait)return ErrJ("wait_ms must be an integer0..10000");
 return WaitInputReady((unsigned)wait,JBool(request,"hooks_only",false));
}
std::string CommandInputReadiness(const std::string& cmd,const JObj& request) {
 auto window=InputReadyWindow();
 if(g_inputHooksReady&&g_inputHookInstallFinished&&CursorInputProtectionReady()&&window.ready&&
    g_commandReadyWindow==window.window&&g_commandReadyGeneration==window.generation)return OkJ();
 unsigned cap=5000;
 // First-time arming must not wait out an explicitly short click-hover cap.
 if((cmd=="click"||cmd=="mousedown")&&JHas(request,"hover_timeout_ms")) {
  auto timeout=request.find("hover_timeout_ms");
  if(timeout->second.t==2&&std::isfinite(timeout->second.n)&&timeout->second.n>=1&&timeout->second.n<=10000)
   cap=(std::min)(cap,(unsigned)std::ceil(timeout->second.n));
 }
 std::string response=WaitInputReady(cap,false);JObj state;
 if(ParseJson(response,state)&&JBool(state,"ok",false)) {
  window=InputReadyWindow();
  const auto generation=JNum(state,"window_generation",-1);
  const auto handle=(HWND)(ULONG_PTR)strtoull(JStr(state,"hwnd").c_str(),nullptr,0);
  if(!window.ready||generation!=(double)window.generation||handle!=window.window) {
   JOut failure;failure.bol("ok",false);failure.bol("input_injected",false);
   failure.str("error","input target changed while arming; no input was injected");return failure.done();
  }
  g_commandReadyWindow=window.window;g_commandReadyGeneration=window.generation;
 }
 return response;
}
bool InputNeedsReady(const std::string& cmd,const JObj& request) {
 (void)request;return cmd=="key"||cmd=="hold"||cmd=="pulse"||cmd=="down"||cmd=="type"||cmd=="seq"||cmd=="sequence"||cmd=="click"||cmd=="mousedown"||cmd=="drag"||cmd=="placement-drag";
}
