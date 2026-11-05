// Pure presentation of an already-failed launch injection/readiness stage.
// No API calls, PID lookup, retry, injection or readiness authorization here.
#pragma once
#include "vi_ready_json.h"
#include <cstdint>
namespace vi_launch_injection {
#define VI_LAUNCH_RETRY_POLICY 1
inline bool MayFollowPreLoader(std::uint32_t pid,std::uint64_t born,const std::string& raw){
 if(!pid||!born)return false;vi_ready_json::Reader reply{raw,0,{}};bool ok=true,injected=true,started=true;
 if(!reply.Parse()||!reply.Bool("ok",ok)||ok||!reply.Bool("injected",injected)||injected||!reply.Bool("loader_thread_started",started)||started)return false;
 const auto process=reply.fields.find("pid"),created=reply.fields.find("process_created"),stage=reply.fields.find("failure_stage");
 return process!=reply.fields.end()&&process->second.type=='n'&&process->second.raw==std::to_string(pid)&&created!=reply.fields.end()&&created->second.type=='"'&&created->second.raw=="\""+std::to_string(born)+"\""&&stage!=reply.fields.end()&&stage->second.type=='"'&&stage->second.raw=="\"create_remote_thread\"";
}
inline const char* ReadyValue(bool hooksOnly,bool requestedReady){return hooksOnly?"null":requestedReady?"true":"false";}
inline const char* ActivationHint(bool available,const std::string& appActive,const std::string& frameApi){
 if(!available||frameApi!="\"none\"")return nullptr;
 if(appActive=="-1")return "For the current adoption, no application-active notification and no intercepted Present stream are observed. Inspect app_active, autoactivate/fakefocus/blockfocus, minimization and renderer diagnostics; the startup cause is unconfirmed.";
 if(appActive=="0")return "The last observed application-active state is inactive, with no intercepted Present stream. Inspect activation settings and renderer diagnostics; this does not prove why startup is stalled.";
 return nullptr;
}
struct Observation {
 bool record=false,injectedKnown=false,injected=false,readinessKnown=false,hooks=false,window=false,queue=false;
 std::string reason="null";
};
inline Observation Observe(std::uint32_t pid,const std::string& raw){
 Observation out;vi_ready_json::Reader top{raw,0,{}};bool ok=false;
 if(!top.Parse()||!top.Bool("ok",ok))return out;
 const auto identity=top.fields.find("pid");if(identity==top.fields.end()||identity->second.type!='n'||identity->second.raw!=std::to_string(pid))return out;
 out.record=true;out.injectedKnown=top.Bool("injected",out.injected);
 const auto nested=top.fields.find("readiness");if(nested==top.fields.end()||nested->second.type!='{')return out;
 const auto readiness=vi_ready_json::Parse(nested->second.raw);if(!readiness.supported)return out;
 out.readinessKnown=true;out.hooks=readiness.hooks;out.window=readiness.window;out.queue=readiness.queue;
 vi_ready_json::Reader details{nested->second.raw,0,{}};if(details.Parse()){const auto why=details.fields.find("readiness_reason");if(why!=details.fields.end()&&why->second.type=='"')out.reason=why->second.raw;}
 return out;
}
inline const char* Bool(bool b){return b?"true":"false";}
inline std::string Failure(std::uint32_t pid,const std::string& raw){
 const auto state=Observe(pid,raw);const bool loaded=state.injectedKnown&&state.injected;
 std::string out="{\"ok\":false,\"pid\":"+std::to_string(pid)+",\"injected\":"+(state.injectedKnown?Bool(state.injected):"null")+",\"ready\":false,\"readiness_scope\":\"window_queue\",\"failure_stage\":\""+(loaded?"readiness":state.injectedKnown?"injection":"injection_or_readiness")+"\"";
 out+=std::string(",\"hooks_ready\":")+(state.readinessKnown?Bool(state.hooks):"null")+",\"window_ready\":"+(state.readinessKnown?Bool(state.window):"null")+",\"queue_ready\":"+(state.readinessKnown?Bool(state.queue):"null")+",\"readiness_reason\":"+state.reason;
 out+=",\"error\":\""+std::string(loaded?"DLL load completed; full input readiness remains unconfirmed for this original PID; inspect inject.readiness and query readiness later":"injection or readiness failed; inspect the original PID and nested inject reply")+"\"}";return out;
}
} // namespace vi_launch_injection
