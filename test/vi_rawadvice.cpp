// Actual doctor JSON field reader and advice branch, no native APIs or target.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "actual_doctor_fields.inl"
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;}
static std::vector<std::string> Advice(const std::string& st,bool available=true){
 (void)available; // Also compile the controlled omitted-adapter negative under /WX.
 using namespace vi_doctor;std::vector<std::string> suggestions;
 const std::string rawRegistration=Field(st,"raw_registration"),rawDelivery=Field(st,"raw_delivery");
#include "actual_raw_advice.inl"
 return suggestions;
}
int main(){
 const std::string good=R"({"ok":true,"rawmode":1,"raw_mouse":true,"raw_buffer_routing":{"version":1,"mouse_thread":42,"mouse_recent_buffer_reader":true},"raw_registration":{"query_ok":true,"mouse_target":"0x123","mouse_target_owned":true,"fallback_target_owned":true},"raw_delivery":{"buffer_supported":true,"last_backend":"post","recent_buffer_read":false}})";
 auto replace=[&](const std::string& from,const std::string& to){auto s=good;const auto at=s.find(from);if(at==std::string::npos)return std::string{};s.replace(at,from.size(),to);return s;};
 auto hints=Advice(good);Check(hints.size()==1&&hints[0].find("raw=auto")!=std::string::npos&&hints[0].find("raw=buffer")!=std::string::npos,"exact owned destination recent reader suggests explicit deliberate mode test");
 Check(hints.size()==1&&hints[0].find("No mode was changed or input repeated")!=std::string::npos&&hints[0].find("does not prove application consumption")!=std::string::npos,"advice keeps mutation and consumption unknowns explicit");
 Check(Advice(good,false).empty(),"unavailable status gives no route advice");
 const std::pair<std::string,std::string> negatives[]={
  {"\"ok\":true","\"ok\":false"},{"\"rawmode\":1","\"rawmode\":0"},{"\"rawmode\":1","\"rawmode\":2"},{"\"rawmode\":1","\"rawmode\":\"1\""},
  {"\"raw_mouse\":true","\"raw_mouse\":false"},{"\"buffer_supported\":true","\"buffer_supported\":false"},{"\"version\":1","\"version\":2"},
  {"\"mouse_recent_buffer_reader\":true","\"mouse_recent_buffer_reader\":false"},{"\"mouse_thread\":42","\"mouse_thread\":\"42\""},{"\"mouse_thread\":42","\"mouse_thread\":0"},{"\"mouse_thread\":42","\"mouse_thread\":4294967296"},
  {"\"query_ok\":true","\"query_ok\":false"},{"\"mouse_target_owned\":true","\"mouse_target_owned\":false"}};
 for(const auto& entry:negatives){const auto changed=replace(entry.first,entry.second);Check(!changed.empty()&&Advice(changed).empty(),"unknown/foreign/global-only/unsupported or different explicit mode gives no advice");}
 auto fallback=replace("\"mouse_target\":\"0x123\",\"mouse_target_owned\":true","\"mouse_target\":\"0x0\",\"mouse_target_owned\":false");
 Check(!fallback.empty()&&Advice(fallback).size()==1,"owned adopted-window fallback can qualify the actual destination");
 Check(Advice("{}").empty(),"missing legacy metadata stays unknown");
 std::printf("raw route advice native: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
