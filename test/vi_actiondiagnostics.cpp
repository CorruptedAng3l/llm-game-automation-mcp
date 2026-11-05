#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
#include "vi_ready_json.h"
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 unsigned pass=0,fail=0;auto check=[&](bool v,const char* n){printf("%s %s\n",v?"PASS":"FAIL",n);v?++pass:++fail;};
 C.wm_mouse=2408;C.wm_mouse_dropped=2091;C.wm_mouse_owned=300;C.wm_mouse_owned_delivered=300;C.wm_mouse_owned_dropped=0;C.wm_mouse_unrecognized_dropped=2091;
 auto reply=ActionMouseDiagnostics("{\"ok\":true,\"input_injected\":true}","click");vi_ready_json::Reader j{reply,0,{}};bool ok=false,injected=false;
 check(j.Parse()&&j.Bool("ok",ok)&&ok&&j.Bool("input_injected",injected)&&injected&&j.fields.at("mouse_message_health_version").raw=="1","actual reply attachment retains original input success");
 check(reply.find("\"wm_mouse_dropped\":2091")!=std::string::npos&&reply.find("\"wm_mouse_owned_delivered\":300")!=std::string::npos,"reply distinguishes owned delivery from aggregate suppression");
 check(ActionMouseDiagnostics("{\"ok\":true}","status")=="{\"ok\":true}","read-only status not rewritten");
 g_acProduct="synthetic finding";check(ActionMouseDiagnostics("{\"ok\":false,\"anticheat_refused\":true}","click")=="{\"ok\":false,\"anticheat_refused\":true}","anti-cheat refusal remains exact");g_acProduct=nullptr;
 check(vi_launch_terminal::Verify(),"terminal desktop guards retained");printf("action diagnostics: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;}
