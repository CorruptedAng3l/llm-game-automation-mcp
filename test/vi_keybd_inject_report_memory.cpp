// Exact injection report and formatter; all transport/conversion is modeled.
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
using DWORD=unsigned long;using HANDLE=void*;
#include "vi_ready_json.h"
#include "vi_launch_injection_failure.h"
#include "json_escape.inl"
#include "inject_emitter.inl"
std::string modeledWire;bool modelAvailable=true;
static bool ReadinessTransport(DWORD,const std::string&,DWORD,std::string& out){out=modeledWire;return modelAvailable;}
static bool RequestInputReady(DWORD,bool,std::string& out,HANDLE){out=modeledWire;return modelAvailable;}
static std::string W2U(const std::wstring&){return "owned.dll";}
#include "vi_cli_cursor_guard.inl"
#include "inject_report.inl"
unsigned passed=0,failed=0;
void Check(bool value,const char* name){std::printf("%s %s\n",value?"PASS":"FAIL",name);value?++passed:++failed;}
std::string text(const std::string& value,const char* name){vi_ready_json::Reader parsed{value,0,{}};if(!parsed.Parse())return {};auto field=parsed.fields.find(name);if(field==parsed.fields.end())return {};vi_ready_json::Reader token{field->second.raw,0,{}};std::string decoded;return token.String(decoded)?decoded:std::string{};}
std::string report(int& result,bool hooks=false){std::string captured;result=ReportInputReady(nullptr,42,L"owned.dll",false,hooks,false,nullptr,&captured);return captured;}
int main(){
 const std::string old="{\"ok\":true,\"readiness_version\":1,\"hook_ready\":true,\"window_ready\":true,\"queue_ready\":true,\"cursor_hooks_ready\":true,\"cursor_guard_version\":1}";
 const auto good=old.substr(0,old.size()-1)+",\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true}";
 auto replace=[](std::string value,const char* a,const char* b){auto at=value.find(a);value.replace(at,std::string(a).size(),b);return value;};
 int code=0;modeledWire=good;auto reply=report(code);Check(code==0&&text(reply,"error").empty(),"matching new DLL report succeeds");
 modeledWire=old;reply=report(code);Check(code==2&&text(reply,"error").find("keybd_event guard capability")!=std::string::npos,"old DLL injection report names missing keybd capability exactly");
 modeledWire=replace(replace(good,"\"ok\":true","\"ok\":false"),"keybd_event_hooks_ready\":true","keybd_event_hooks_ready\":false");reply=report(code);Check(code==2&&text(reply,"error").find("keybd_event guard is unarmed")!=std::string::npos,"unarmed keybd report names coverage failure");
 modeledWire=replace(replace(replace(good,"\"ok\":true","\"ok\":false"),"window_ready\":true","window_ready\":false"),"queue_ready\":true","queue_ready\":false");reply=report(code);Check(code==2&&text(reply,"error")=="DLL loaded but input infrastructure readiness failed; inspect readiness","ordinary window queue failure is not mislabeled keybd failure");
 modeledWire=good;reply=report(code,true);vi_ready_json::Reader parsed{reply,0,{}};Check(code==0&&parsed.Parse()&&parsed.fields.at("ready").type=='z',"hooks-only report keeps application readiness null");
 modelAvailable=false;reply=report(code);Check(code==2&&text(reply,"error").find("transport/deadline")!=std::string::npos,"transport failure keeps existing diagnostic");modelAvailable=true;
 modeledWire=replace(good,"keybd_event_guard_version\":1","keybd_event_guard_version\":\"1\"");reply=report(code);Check(code==2&&text(reply,"error").find("typed keybd_event guard capability")!=std::string::npos,"wrong typed capability is refused before input readiness success");
 (void)CursorGuardSpecialCommand("status");(void)NeedsCursorGuard("{\"cmd\":\"release\"}");(void)RequireCursorGuard(0);
#if HAS_KEYBD
 (void)CursorGuardFailureText();
#endif
 std::printf("RESULT %u PASS %u FAIL\n",passed,failed);return failed?1:0;
}
