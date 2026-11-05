// White-box actual native endpoint/state tests. No windows or product Init.
// Terminal spies absorb every cursor/input/foreground/queue/visibility writer.
#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
static unsigned passed=0,failed=0;
static void Check(bool b,const char* s){printf("%s %s\n",b?"PASS":"FAIL",s);b?++passed:++failed;}
static JObj Request(const std::shared_ptr<InputJob>& job,double serial,bool abort=false,const char* reason=""){
 JObj j;j["id"]={1,JobId(*job)};j["owner"]={1,"unit"};j["observation_seq"]={2,{},serial};j["abort"]={3,{},0,abort};if(*reason)j["reason"]={1,reason};return j;
}
static std::shared_ptr<InputJob> Seed(){
 g_jobs.clear();memset((void*)g_vk,0,sizeof g_vk);memset(g_resourceOwner,0,sizeof g_resourceOwner);memset(g_resourceGeneration,0,sizeof g_resourceGeneration);
 auto j=std::make_shared<InputJob>();j->kind=InputJob::Sequence;j->id=++g_nextJob;j->owner="unit";j->started=true;j->state="running";j->keys={VK_SPACE};j->resources={VK_SPACE};vi_key_sequence::Step s;s.keys={VK_SPACE};s.ms=5000;j->sequence={s};j->duration=5000;j->claimEpoch=g_jobClaimEpoch;j->accepted=j->start=HousekeepingNow();j->sequenceGuard.Start(4000,j->accepted);j->qpcFrequency=vi_qpc::Frequency();j->completion=CreateEventW(nullptr,TRUE,FALSE,nullptr);g_resourceOwner[VK_SPACE]=j->id;g_resourceGeneration[VK_SPACE]=j->generation[VK_SPACE]=3;g_vk[VK_SPACE]=0x80;g_jobs.push_back(j);return j;
}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 g_pid=GetCurrentProcessId();g_jobIdentity=12345;g_hwnd=nullptr;
 vi_sequence_guard::State pure;pure.Start(1000,100);Check(!pure.Expired(1099)&&pure.Expired(1100),"observer deadline expires at exact boundary");
 Check(pure.Refresh(1100,1)==vi_sequence_guard::Verdict::Expired&&pure.serial==0&&pure.deadline==1100,"late observer can never revive expired guard");
 pure.Start(1000,100);Check(pure.Refresh(200,2)==vi_sequence_guard::Verdict::SerialMismatch&&pure.deadline==1100,"out-of-order observation cannot extend expiry");
 Check(pure.Refresh(200,1)==vi_sequence_guard::Verdict::Renewed&&pure.deadline==1200&&pure.serial==1,"exact next serial extends expiry from observation time");
 pure.Start(1000,~std::uint64_t(0)-10);Check(pure.deadline==~std::uint64_t(0),"guard deadline addition saturates without wraparound");
 auto job=Seed();auto req=Request(job,1);req["owner"]={1,"other"};const auto old=job->sequenceGuard.deadline;auto reply=SequenceObserve(req);
 Check(reply.find("owner mismatch")!=std::string::npos&&!job->done&&job->sequenceGuard.serial==0&&job->sequenceGuard.deadline==old&&g_vk[VK_SPACE],"wrong owner cannot renew, cancel or release a job");
 req=Request(job,1);reply=SequenceObserve(req);Check(reply.find("\"guard_ack\":true")!=std::string::npos&&job->sequenceGuard.serial==1&&!job->done&&g_vk[VK_SPACE],"actual endpoint acknowledges exact owner/id/serial without changing input");
 const auto deadline=job->sequenceGuard.deadline;reply=SequenceObserve(req);Check(reply.find("stale or out of order")!=std::string::npos&&job->sequenceGuard.deadline==deadline&&job->sequenceGuard.serial==1,"duplicate acknowledgement request is not a guard replay");
 req=Request(job,3);reply=SequenceObserve(req);Check(reply.find("stale or out of order")!=std::string::npos&&job->sequenceGuard.deadline==deadline,"skipped serial cannot renew guard");
 req=Request(job,2,true,"unknown");reply=SequenceObserve(req);Check(!job->done&&job->sequenceGuard.serial==1&&reply.find("unknown sequence abort reason")!=std::string::npos,"unrecognized abort reason changes no input or guard");
 req=Request(job,2,true,"text");reply=SequenceObserve(req);Check(job->done&&job->reason=="sequence_abort_text"&&!g_vk[VK_SPACE]&&g_resourceOwner[VK_SPACE]==0&&job->transitions==1&&WaitForSingleObject(job->completion,0)==WAIT_OBJECT_0,"exact abort releases only owned virtual key and signals native completion");
 g_resourceOwner[VK_SPACE]=999;g_resourceGeneration[VK_SPACE]=10;g_vk[VK_SPACE]=0x80;req=Request(job,3,true,"text");reply=SequenceObserve(req);
 Check(g_vk[VK_SPACE]&&g_resourceOwner[VK_SPACE]==999&&g_resourceGeneration[VK_SPACE]==10&&job->transitions==1,"terminal old-job acknowledgement cannot release newer resource generation");
 job=Seed();job->sequenceGuard.deadline=HousekeepingNow()-1;reply=SequenceObserve(Request(job,1));
 Check(job->done&&job->reason=="sequence_observer_expired"&&!g_vk[VK_SPACE]&&job->sequenceGuard.serial==0,"expired native observer performs generation-aware cleanup and refuses revival");
 job=Seed();job->sequenceGuard.deadline=HousekeepingNow()-1;g_resourceGeneration[VK_SPACE]++;
 reply=SequenceObserve(Request(job,1));Check(job->done&&g_vk[VK_SPACE]&&g_resourceOwner[VK_SPACE]==job->id,"expiry cleanup leaves key held by a newer generation untouched");
 job=Seed();++g_jobClaimEpoch;reply=SequenceObserve(Request(job,1));Check(job->done&&job->reason=="claim_expired_or_changed"&&!g_vk[VK_SPACE],"observer cannot bypass original claim epoch fence");
 job=Seed();job->captureBound=true;reply=SequenceObserve(Request(job,1));Check(job->done&&job->reason=="sequence_capture_scope_changed"&&!g_vk[VK_SPACE],"observer cannot renew across invalid capture scope");
 job=Seed();const auto count=g_jobs.size();const auto owner=g_resourceOwner[VK_SPACE],generation=g_resourceGeneration[VK_SPACE];auto incoming=std::make_shared<InputJob>();incoming->kind=InputJob::Keys;incoming->resources={VK_SPACE};JObj schedule;schedule["expected_capture_scope"]={1,"{}"};schedule["replace"]={3,{},0,true};reply=ScheduleInputJob(incoming,schedule);
 Check(reply.find("\"input_injected\":false")!=std::string::npos&&g_jobs.size()==count&&!job->done&&g_vk[VK_SPACE]&&g_resourceOwner[VK_SPACE]==owner&&g_resourceGeneration[VK_SPACE]==generation&&!g_jobWorker,"scope admission mismatch refuses before replacement, worker creation or input mutations");
 g_claimOwner="unit";g_claimTtlMs=60000;g_claimExpiry=HousekeepingNow()+30000;g_jobClaimExpiry=g_claimExpiry;const auto expiry=g_claimExpiry;JObj observe;observe["owner"]={1,"unit"};observe["observation_only"]={3,{},0,true};
 Check(ClaimGate("frame-hash",observe).empty()&&g_claimExpiry==expiry&&g_jobClaimExpiry==expiry,"observation-only hash cannot renew inactivity claim");
 Check(ClaimGate("capture",observe).empty()&&g_claimExpiry==expiry&&g_jobClaimExpiry==expiry,"observation-only OCR capture cannot renew inactivity claim");
 observe["owner"]={1,"other"};Check(!ClaimGate("frame-hash",observe).empty()&&g_claimExpiry==expiry,"observation-only flag preserves competing-owner refusal");
 const auto qpc=vi_qpc::Now(),frequency=vi_qpc::Frequency();
 for(const auto& anchor:std::vector<std::pair<std::string,std::string>>{{std::to_string(qpc+frequency),std::to_string(frequency)},{std::to_string(qpc-7*frequency),std::to_string(frequency)},{std::to_string(qpc),std::to_string(frequency+1)},{"18446744073709551616",std::to_string(frequency)}}){JObj j;j["script"]={1,"key space 20\n"};j["expected_capture_scope"]={1,"{}"};j["start_qpc_ticks"]={1,anchor.first};j["start_qpc_frequency"]={1,anchor.second};const auto before=g_jobs.size();reply=ScheduleKeySequence(j);Check(reply.find("\"input_injected\":false")!=std::string::npos&&before==g_jobs.size(),"actual event admission refuses future/stale/mismatched/overflow QPC without input");}
 job=Seed();job->qpcAnchor=vi_qpc::Now()-frequency;job->qpcNext=job->qpcAnchor;job->sequence[0].ms=20;job->phase=2;job->index=0;job->latenessLimit=1;JobRunSequence(*job,HousekeepingNow());Check(job->done&&job->reason=="sequence_max_lateness_exceeded"&&!g_vk[VK_SPACE]&&g_resourceOwner[VK_SPACE]==0,"lateness abort performs necessary owned-key cleanup without replaying the overdue edge");
 auto imageJob=Seed();reply=SequenceObserve(Request(imageJob,1,true,"image"));Check(imageJob->done&&imageJob->reason=="sequence_abort_image"&&!g_vk[VK_SPACE]&&imageJob->qpcFinished>0,"image observer abort uses exact generation-aware cleanup and retains its native terminal QPC");
 Check(vi_launch_terminal::Verify(),"terminal input/foreground/queue/visibility guards remain installed");
 printf("sequence guard memory: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
