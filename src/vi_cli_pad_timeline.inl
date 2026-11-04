// Unknown options were ignored by older DLLs. Probe before the original input.
#include "vi_ready_json.h"
static bool NeedsPadTimelineV2(const std::string& request){
 // Probe any direct stagger field, including escaped field/command spellings.
 // Invalid or oversized JSON fails closed; the target still validates the input.
 vi_ready_json::Reader parsed{request,0,{}};if(!parsed.Parse())return true;
 return parsed.fields.find("stagger_ms")!=parsed.fields.end();
}
static Reply RequirePadTimelineV2(DWORD pid){
 std::string response;bool ok=false;const bool received=BoundedJobRequest(pid,"{\"cmd\":\"ping\",\"pad_timeline_support\":true}",1000,response);
 vi_ready_json::Reader parsed{response,0,{}};
 if(received&&parsed.Parse()&&parsed.Bool("ok",ok)&&ok){auto version=parsed.fields.find("pad_timeline_version");unsigned number=0;if(version!=parsed.fields.end()&&version->second.type=='n'&&ExpectUnsigned(version->second.raw,1000,number)&&number>=2)return {true,"{\"ok\":true}"};}
 return {false,"{\"ok\":false,\"input_injected\":false,\"pad_timeline_support_checked\":true,\"error\":\"target lacks pad-timeline stagger support or its status was unavailable; restart the owned target with the matching updated DLL; original input was not sent\"}"};
}
