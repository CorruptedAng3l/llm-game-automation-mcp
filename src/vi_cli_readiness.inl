// Unlike legacy Request, readiness includes a bounded receive deadline. A hung
// pipe peer cannot make the injector claim success or wait indefinitely.
#include "vi_ready_json.h"
#include "vi_launch_injection_failure.h"
static bool InjectHooksOnly(const Args& args,bool& value,std::string& error) {
 value=false;unsigned seen=0;
 for(const auto& option:args.opt)if(option.first=="hooks-only") {
  if(++seen>1){error="--hooks-only may appear only once";return false;}
  if(option.second!="true"&&option.second!="false"){error="--hooks-only must be true or false (use --hooks-only=true or --hooks-only=false)";return false;}
  value=option.second=="true";
 }
 if(seen&&!args.pos.empty()){error="inject takes no positional arguments; use --hooks-only=true or --hooks-only=false for an explicit boolean";return false;}
 return true;
}
static bool ReadinessTransport(DWORD pid,const std::string& request,DWORD budget,std::string& response,HANDLE target=nullptr) {
 return BoundedCliRequest(pid,request,budget,response,target);
}
static bool RequestInputReady(DWORD pid,bool hooksOnly,std::string& response,HANDLE target=nullptr) {
 const auto wait=CliBudget(hooksOnly?5000:g_cliReadinessWaitMs);if(!wait)return false;
 return ReadinessTransport(pid,std::string("{\"cmd\":\"readiness\",\"wait_ms\":")+std::to_string(wait)+",\"hooks_only\":"+(hooksOnly?"true":"false")+"}",6500,response,target);
}
#include "vi_cli_cursor_guard.inl"
static int ReportInputReady(FILE* output,DWORD pid,const std::wstring& dll,bool quiet,bool hooksOnly,bool already,HANDLE target=nullptr,std::string* captured=nullptr) {
 std::string response;const bool received=RequestInputReady(pid,hooksOnly,response,target);
 const auto reply=vi_ready_json::Parse(response);const bool supported=received&&reply.supported;
 const bool ok=supported&&reply.ok&&reply.hooks&&CursorGuardHandshakeReady(response)&&(hooksOnly||(reply.window&&reply.queue));
 std::string failure;if(!ok){if(!received)failure=",\"error\":\"DLL loaded but readiness transport/deadline failed; input readiness is unconfirmed\"";
 else if(!supported)failure=",\"error\":\"DLL pipe exists but readiness v1 is unsupported; restart the owned target with the matching updated DLL\"";
 else if(!CursorKeybdCapabilityReady(response))failure=",\"error\":\""+JEsc(CursorGuardHandshakeFailureText(response))+"\"";
 else failure=",\"error\":\"DLL loaded but input infrastructure readiness failed; inspect readiness\"";}

 if(captured||!quiet||!ok)EmitInjectReply(output,captured,"{\"ok\":%s,\"pid\":%lu,\"injected\":true,\"ready\":%s,\"readiness_scope\":\"%s\",\"hooks_ready\":%s,\"%s\":\"%s\",\"already_injected\":%s,\"readiness\":%s%s%s}\n",ok?"true":"false",pid,vi_launch_injection::ReadyValue(hooksOnly,ok),hooksOnly?"hooks":"window_queue",supported?(reply.hooks?"true":"false"):"null",already?"requested_dll":"dll",JEsc(W2U(dll)).c_str(),already?"true":"false",supported?response.c_str():"null",already?",\"note\":\"already injected\"":"",failure.c_str());
 return ok?0:2;
}
