#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include "vi_ready_json.h"
using DWORD=unsigned long;
struct Args{std::vector<std::pair<std::string,std::string>> opt;};
static bool HasOpt(const Args& a,const char* name){for(const auto& o:a.opt)if(o.first==name)return true;return false;}
static void Put(std::string& text,const std::string& key,const std::string& value){text+=",\""+key+"\":"+value;}
static void PutS(std::string& text,const std::string& key,const std::string& value){text+=",\""+key+"\":\""+value+"\"";}
static std::string JEsc(const std::string& value){return value;}
static std::string JobRawBytes(const std::string& value){return value;}
static bool ExpectUnsigned(const std::string& value,unsigned maximum,unsigned& out){char* end=nullptr;const auto n=strtoul(value.c_str(),&end,10);if(value.empty()||!end||*end||n>maximum)return false;out=(unsigned)n;return true;}
static std::string capability;static bool transport=true;static unsigned probes=0;
static bool BoundedJobRequest(DWORD,const std::string&,DWORD,std::string& response){++probes;response=capability;return transport;}
#include "vi_cli_capture_scope.inl"
int main(){unsigned pass=0,fail=0;auto check=[&](bool v,const char* name){printf("%s %s\n",v?"PASS":"FAIL",name);v?++pass:++fail;};
 const std::string command="{\"cmd\":\"click\",\"x\":1,\"y\":2,\"expected_capture_scope\":\"{}\"}";
 capability="{\"ok\":true,\"pid\":123,\"pointer_scope_version\":1}";std::string response,request=command;check(!CaptureScopePreflight(123,request,response)&&probes==1&&request==command,"typed matching native scope capability admits original pointer request once");
 for(const char* bad:{"{\"ok\":true}","{\"ok\":true,\"pid\":124,\"pointer_scope_version\":1}","{\"ok\":true,\"pid\":123,\"pointer_scope_version\":\"1\"}","{\"ok\":true,\"pid\":123,\"pointer_scope_version\":2}","{\"ok\":false,\"pid\":123,\"pointer_scope_version\":1}","{\"ok\":true,\"pid\":123,\"pointer_scope_version\":1,\"pointer_scope_version\":1}"}){capability=bad;request=command;response.clear();check(CaptureScopePreflight(123,request,response)&&response.find("\"input_injected\":false")!=std::string::npos,"unsupported/malformed/mismatched capability refuses before coordinate delivery");}
 transport=false;request=command;check(CaptureScopePreflight(123,request,response),"unavailable capability transport refuses pointer delivery");transport=true;probes=0;
 request="{\"cmd\":\"click\",\"x\":1,\"y\":2}";check(!CaptureScopePreflight(123,request,response)&&probes==0,"ordinary client coordinates retain existing behavior");
 request="{\"cmd\":\"seq\",\"expected_capture_scope\":\"{}\"}";check(!CaptureScopePreflight(123,request,response)&&probes==0,"sequence scope follows its separate admission capability");
 request="{\"cmd\":\"mouseup\",\"expected_capture_scope\":\"{}\"}";check(CaptureScopePreflight(123,request,response)&&response.find("\"cleanup_attempted\":false")!=std::string::npos,"unsupported scoped release does not guess cleanup ownership");
 printf("pointer capability: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;}
