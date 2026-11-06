// Capacity-only control over actual extracted public sequence ACK parsing.
#include <fstream>
#include <cstdio>
#include <string>
#include "../src/vi_ready_json.h"
struct Reply{bool ok=false;std::string json;};
#include "actual_ack_reader.inl"
static unsigned passed=0,failed=0;
static void Check(bool value,const char* what){printf("%s %s\n",value?"PASS":"FAIL",what);value?++passed:++failed;}
static std::string Fields(unsigned count){std::string out="{";for(unsigned i=0;i<count;++i){if(i)out+=',';out+="\"x"+std::to_string(i)+"\":0";}return out+"}";}
int wmain(int count,wchar_t** args){
 if(count!=2)return 2;std::ifstream file(args[1],std::ios::binary);char buffer[65537]{};file.read(buffer,sizeof(buffer));const auto bytes=file.gcount();if(!file.eof()||bytes<=0||bytes>65536)return 3;const std::string raw(buffer,(size_t)bytes);
 vi_ready_json::Reader normal{raw,0,{}};Check(!normal.Parse(),"unchanged default64 reader rejects the actual65-field ACK");
 vi_ready_json::Reader full{raw,0,{}};full.maxTopLevelFields=128;Check(full.Parse()&&full.fields.size()==65,"explicit bounded128 reader accepts the complete actual65-field ACK");
 const auto reply=ActualAck(raw,true);Check(reply.ok&&reply.json==raw,"actual production sequence ACK fragment accepts original native receipt without loss");
 Check(!ActualAck(raw,false).ok,"transport uncertainty never becomes a successful ACK");
 Check(!ActualAck("{\"ok\":true,"+raw.substr(1),true).ok,"duplicate field still refuses after capacity expansion");
 auto wrong=raw;const auto at=wrong.find("\"accepted\":true");if(at==std::string::npos)return 4;wrong.replace(at,15,"\"accepted\":\"true\"");Check(!ActualAck(wrong,true).ok,"string accepted flag cannot satisfy exact boolean success");
 const std::string ping="{\"ok\":true,\"pid\":1}";vi_ready_json::Reader tiny{ping,0,{}};Check(tiny.maxTopLevelFields==64&&tiny.Parse(),"tiny/default handshake remains limited to64 fields");
 auto maximum=Fields(128);vi_ready_json::Reader bounded{maximum,0,{}};bounded.maxTopLevelFields=128;Check(bounded.Parse(),"full-reply capacity remains finite at128 direct fields");
 auto over=Fields(129);vi_ready_json::Reader capped{over,0,{}};capped.maxTopLevelFields=128;Check(!capped.Parse(),"129th direct field still refuses");
 auto nested="{\"nested\":"+Fields(65)+"}";vi_ready_json::Reader child{nested,0,{}};child.maxTopLevelFields=128;Check(!child.Parse(),"nested object limit remains64 rather than expanding with root capacity");
 printf("key drive ACK capacity: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}