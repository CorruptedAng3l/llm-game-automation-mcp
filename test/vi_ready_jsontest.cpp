#include "../src/vi_ready_json.h"
#include <cstdio>
int main(){int passed=0,failed=0;auto check=[&](bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(ok)++passed;else ++failed;};
 const std::string good=R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true,"application_ready":null})";
 auto reply=vi_ready_json::Parse(good);check(reply.supported&&reply.ok&&reply.hooks&&reply.window&&reply.queue,"complete typed readiness accepted");
 for(const auto& bad: {R"({"readiness_version":"1","ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true})",
  R"({"readiness_version":1,"ok":"true","hook_ready":true,"window_ready":true,"queue_ready":true})",
  R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true})",
  R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true,"ok":false})",
  R"({"nested":{"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true}})",
  R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true}trailing)",
  R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true,"extra":})",
  R"({"readiness_version":1,"ok":true,"hook_ready":true,"window_ready":true,"queue_ready":true,"extra":"\q"})"})check(!vi_ready_json::Parse(bad).supported,"malformed, nested, duplicate or mistyped readiness refused");
 reply=vi_ready_json::Parse(R"({"readiness_version":1,"ok":false,"hook_ready":true,"window_ready":true,"queue_ready":false})");
 check(reply.supported&&!reply.ok&&reply.hooks&&reply.window&&!reply.queue,"typed readiness failure remains supported with honest state");
 std::printf("%d PASS / %d FAIL\n",passed,failed);return failed?1:0;}
