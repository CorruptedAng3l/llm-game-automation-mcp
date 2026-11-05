#include "../src/vi_capture_fence.h"
#include <cstdio>
int main(){unsigned passed=0,failed=0;auto check=[&](bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;};
 const std::string token="{\"hwnd\":\"0x1234\",\"adoption_generation\":\"2\",\"geometry_epoch\":\"3\",\"lifetime_cookie\":\"4\"}";
 check(vi_capture_fence::Matches(token,token),"exact current native token accepted");
 check(!vi_capture_fence::Matches("",token),"empty token refused");check(!vi_capture_fence::Matches("{}",token),"omitted scope members refused");
 auto changed=[&](const std::string& from,const std::string& to){auto value=token;const auto at=value.find(from);if(at==std::string::npos)return std::string();value.replace(at,from.size(),to);return value;};
 const auto geometry=changed("\"geometry_epoch\":\"3\"","\"geometry_epoch\":\"5\"");check(!geometry.empty()&&!vi_capture_fence::Matches(token,geometry),"changed geometry refused");
 const auto lifetime=changed("\"lifetime_cookie\":\"4\"","\"lifetime_cookie\":\"9\"");check(!lifetime.empty()&&!vi_capture_fence::Matches(token,lifetime),"changed window lifetime refused");
 check(!vi_capture_fence::Matches(token,""),"unavailable current scope refused");
 check(!vi_capture_fence::Matches(" "+token,token),"noncanonical token refused");
 auto zero=token;zero.insert(3,1,'\0');check(!vi_capture_fence::Matches(zero,zero),"embedded NUL refused even when bytes match");
 const std::string huge="{"+std::string(2048,'x')+"}";check(!vi_capture_fence::Matches(huge,huge),"oversized token refused");
 std::printf("CAPTURE_FENCE_RESULT %u %u\n",passed,failed);return failed?1:0;}
