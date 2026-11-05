// Actual production job cancellation and CLI refusal paths. No Init/window/worker.
#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
#include "vi_ready_json.h"
static std::string readinessResponse;
static bool ReadinessTransport(DWORD,const std::string&,unsigned,std::string& response){response=readinessResponse;return true;}
#include "vi_cli_cursor_guard.inl"
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 unsigned pass=0,fail=0;auto check=[&](bool value,const char* name){printf("%s %s\n",value?"PASS":"FAIL",name);value?++pass:++fail;};
 g_pid=GetCurrentProcessId();g_hwnd=nullptr;
 for(bool frame:{false,true}){
  for(bool detected:{false,true}){
   g_jobs.clear();auto job=std::make_shared<InputJob>();job->id=1;job->frames=frame;job->accepted=HousekeepingNow();g_jobs.push_back(job);
   g_acProduct=detected?"synthetic test finding":nullptr;
   if(frame)JobsFrame();else JobsTick(false);
   check(job->done&&job->state=="cancelled"&&job->reason==(detected?"anticheat_detected":"target_changed")&&job->transitions==0&&job->updates==0,
    frame?(detected?"frame checkpoint cancels before running input on detection":"frame checkpoint positive no-detection control"):
          (detected?"time checkpoint cancels before running input on detection":"time checkpoint positive no-detection control"));
  }
 }
 g_jobs.clear();g_acProduct=nullptr;
 readinessResponse="{\"ok\":false,\"anticheat_refused\":true,\"error\":\"synthetic refusal\",\"anticheat\":{\"product\":\"fixture\"}}";
 std::string refusal;check(!RequireCursorGuard(g_pid,&refusal)&&refusal==readinessResponse,"exact typed anti-cheat refusal survives CLI safety preflight");
 readinessResponse="{\"ok\":false,\"anticheat_refused\":\"true\"}";refusal.clear();check(!RequireCursorGuard(g_pid,&refusal)&&refusal.empty(),"untyped refusal flag cannot claim anti-cheat passthrough");
 readinessResponse="{\"ok\":true,\"hook_ready\":true,\"cursor_hooks_ready\":true,\"keybd_event_hooks_ready\":true,\"keybd_event_guard_version\":1,\"cursor_guard_version\":1}";
 check(RequireCursorGuard(g_pid),"typed healthy readiness remains accepted");
 g_realFg=false;g_ownerSeq=0;g_agentSeq=0;NoteOwnerInput(false);check(!OwnerCursorActive(),"background activity does not stand down virtual cursor");
 g_realFg=true;check(OwnerCursorActive(),"newer foreground owner activity stands down virtual cursor");
 NoteAgentInput();check(!OwnerCursorActive(),"a later agent input supersedes earlier owner activity");
 NoteOwnerInput(true);check(OwnerCursorActive()&&g_ownerHolding,"deliberate later owner activity is retained");
 check(vi_launch_terminal::Verify()&&!g_jobWorker&&!g_hwnd,"terminal writers retained without window, Init or worker");
 printf("public safeguards: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;
}
