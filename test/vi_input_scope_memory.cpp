// Admission-only white-box test; product Init never runs and no window exists.
#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 g_pid=GetCurrentProcessId();g_hwnd=nullptr;g_jobIdentity=123;
 auto prior=std::make_shared<InputJob>();prior->id=1;prior->resources={VK_SPACE};g_jobs.push_back(prior);g_resourceOwner[VK_SPACE]=1;g_resourceGeneration[VK_SPACE]=7;g_vk[VK_SPACE]=0x80;
 unsigned pass=0,fail=0;auto check=[&](bool b,const char* name){printf("%s %s\n",b?"PASS":"FAIL",name);b?++pass:++fail;};
 const std::vector<std::pair<JV,const char*>> cases={{{2,{},2},"non-string"},{{1,""},"empty"},{{1,"{}"},"unrecognized/no window"},{{1,std::string(2049,'x')},"oversized"},{{1,std::string("x\0y",3)},"embedded NUL"}};
 for(const auto& entry:cases){auto incoming=std::make_shared<InputJob>();incoming->kind=InputJob::Keys;incoming->resources={VK_SPACE};JObj j;j["expected_capture_scope"]=entry.first;j["replace"]={3,{},0,true};const auto result=ScheduleInputJob(incoming,j);
  check(result.find("\"input_injected\":false")!=std::string::npos&&result.find("\"ok\":false")!=std::string::npos&&!prior->done&&g_jobs.size()==1&&g_resourceOwner[VK_SPACE]==1&&g_resourceGeneration[VK_SPACE]==7&&g_vk[VK_SPACE]&&!g_jobWorker,entry.second);}
 auto bound=std::make_shared<InputJob>();bound->captureBound=true;check(JobReplyLocked(*bound,true).find("\"capture_scope_checked\":true")!=std::string::npos,"bound admission has typed receipt");
 check(vi_launch_terminal::Verify(),"terminal writers remain guarded; no window or worker was created");
 printf("input scope memory: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;
}
