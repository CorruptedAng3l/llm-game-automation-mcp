// Exact pure presentation helper; no Windows/loader/pipe/window APIs.
#include "../src/vi_launch_injection_failure.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace {
int passed=0,failed=0;
void Check(bool b,const char* n){if(b)++passed;else{++failed;std::printf("FAIL %s\n",n);}}
std::string Readiness(const char* reason="queue_not_progressing",bool hooks=true){return std::string("{\"ok\":false,\"readiness_version\":1,\"hook_ready\":")+(hooks?"true":"false")+",\"window_ready\":true,\"queue_ready\":false,\"readiness_reason\":\""+reason+"\"}";}
std::string Inject(const std::string& readiness){return "{\"ok\":false,\"pid\":123,\"injected\":true,\"already_injected\":true,\"readiness\":"+readiness+"}";}
bool Field(const std::string& text,const char* key,bool expected){vi_ready_json::Reader p{text,0,{}};bool value=false;return p.Parse()&&p.Bool(key,value)&&value==expected;}
}
int main(int argc,char** argv){
 if(argc==2&&std::strcmp(argv[1],"--await-start")==0){char line[8]{};if(!std::fgets(line,sizeof(line),stdin)||std::strcmp(line,"RUN\n")!=0)return 2;}else if(argc!=1)return 2;
 using namespace vi_launch_injection;
 for(const char* reason:{"queue_not_progressing","window_not_adopted_or_subclassed","hooks_not_armed","readiness_changed_before_reply"}){
  const auto raw=Inject(Readiness(reason));const auto observed=Observe(123,raw);const auto reply=Failure(123,raw);
  Check(observed.record&&observed.injectedKnown&&observed.injected&&observed.readinessKnown,"exact original PID loaded phase observed");
  Check(Field(reply,"ok",false)&&Field(reply,"injected",true)&&Field(reply,"ready",false),"loaded diagnostic remains unsuccessful/unready");
  Check(reply.find(reason)!=std::string::npos&&reply.find("DLL load completed")!=std::string::npos&&reply.find("still loading")==std::string::npos,"specific reason retained without inventing loading cause");
 }
 {const auto raw=Inject(Readiness("required_keyboard_or_focus_hook_failed",false));const auto reply=Failure(123,raw);Check(Field(reply,"hooks_ready",false)&&Field(reply,"ready",false)&&Field(reply,"injected",true),"loaded DLL does not manufacture armed hooks");}
 for(const auto& raw:{std::string(),std::string("not json"),std::string("{\"ok\":false,\"pid\":124,\"injected\":true}"),std::string("{\"ok\":false,\"pid\":123,\"injected\":\"true\"}"),std::string("{\"ok\":false,\"pid\":123,\"pid\":123,\"injected\":true}")}){
  const auto r=Observe(123,raw);const auto text=Failure(123,raw);Check(!r.injectedKnown&&text.find("\"injected\":null")!=std::string::npos&&Field(text,"ready",false),"unknown/mismatched/malformed/wrong-type evidence stays unknown, not injected success");
 }
 {const auto raw=std::string("{\"ok\":false,\"pid\":123,\"injected\":false}");Check(Field(Failure(123,raw),"injected",false),"explicit failed injection remains distinct from loaded readiness failure");}
 {const auto raw=std::string("{\"ok\":false,\"pid\":123,\"injected\":true,\"readiness\":null}");const auto text=Failure(123,raw);Check(Field(text,"injected",true)&&text.find("\"hooks_ready\":null")!=std::string::npos,"transport/unsupported readiness leaves hook evidence unknown while preserving observed load");}
 Check(std::strcmp(ReadyValue(true,true),"null")==0&&std::strcmp(ReadyValue(true,false),"null")==0,"hooks-only never asserts full window/queue readiness");
 Check(std::strcmp(ReadyValue(false,true),"true")==0&&std::strcmp(ReadyValue(false,false),"false")==0,"full handshake emits its existing success/failure unchanged");
 {const auto text=Failure(123,Inject(Readiness()));Check(text.find("\"pid\":123")!=std::string::npos&&text.find("\"failure_stage\":\"readiness\"")!=std::string::npos,"original PID and failure stage retained");}
 Check(ActivationHint(true,"-1","\"none\"")&&std::strstr(ActivationHint(true,"-1","\"none\""),"current adoption"),"doctor labels missing observation for current adoption, not never-activated lifetime");
 Check(ActivationHint(true,"0","\"none\"")&&std::strstr(ActivationHint(true,"0","\"none\""),"last observed"),"doctor distinguishes last inactive observation from missing observation");
 Check(!ActivationHint(true,"1","\"none\"")&&!ActivationHint(false,"-1","\"none\"")&&!ActivationHint(true,"-1","\"d3d11\"")&&!ActivationHint(true,"\"-1\"","\"none\""),"doctor does not infer activation cause from ready, unavailable, presenting or wrong-type evidence");
 std::printf("launch stage model: %d PASS / %d FAIL\n",passed,failed);return failed?1:0;
}
