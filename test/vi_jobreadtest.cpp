// Actual production reader bookkeeping; no product Init, window, device or worker.
#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
#include <thread>
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;}
static std::unique_ptr<InputJob> KeyJob(std::vector<int> keys){auto job=std::make_unique<InputJob>();job->id=42;job->keys=std::move(keys);job->started=true;JobReadsStart(*job,HousekeepingNow());return job;}
static std::string Finish(InputJob& job){JobFinish(job,"completed","");return JobReplyLocked(job);}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;g_pid=GetCurrentProcessId();
 g_keyTrace[VK_F6].gaks.record(0x8000);auto untouched=KeyJob({VK_F6});
 g_keyTrace[VK_F7].gaks.record(0x8000);g_keyTrace[VK_F6].delivery(true);
 auto result=Finish(*untouched);Check(result.find("\"outcome\":\"not_observed\"")!=std::string::npos,"historical/other-key polls and message dispatch alone do not establish requested polling return");
 g_keyTrace[VK_F6].gaks.record(0x8000);Check(JobReplyLocked(*untouched)==result,"later key polling cannot change a frozen terminal receipt");
 auto high=KeyJob({VK_F6});g_keyTrace[VK_F6].di_state.record(0x80);result=Finish(*high);Check(result.find("\"all_requested_observed\":true")!=std::string::npos,"requested DirectInput high-state return is observed");
 auto low=KeyJob({VK_F6});g_keyTrace[VK_F6].gkbs.record(0);result=Finish(*low);Check(result.find("\"outcome\":\"not_observed\"")!=std::string::npos&&result.find("\"poll_reads\":\"1\"")!=std::string::npos,"up-only polling remains distinct from reading the virtual down state");
 auto chord=KeyJob({VK_F6,VK_F7});g_keyTrace[VK_F6].raw_buffer.record(0,true);result=Finish(*chord);Check(result.find("\"outcome\":\"partial\"")!=std::string::npos,"one chord member read cannot establish the whole chord");
 auto duplicate=KeyJob({VK_F6,VK_F6});Check(duplicate->readObservation.keys.size()==1,"duplicate requested names keep one key observation");Finish(*duplicate);
 const int modifierPairs[][2]={{VK_SHIFT,VK_LSHIFT},{VK_CONTROL,VK_LCONTROL},{VK_MENU,VK_LMENU},{VK_LSHIFT,VK_SHIFT},{VK_LCONTROL,VK_CONTROL},{VK_LMENU,VK_MENU},{VK_RSHIFT,VK_SHIFT},{VK_RCONTROL,VK_CONTROL},{VK_RMENU,VK_MENU}};
 for(const auto& pair:modifierPairs){
  auto alias=KeyJob({pair[0]});g_keyTrace[pair[1]].gaks.record(0x8000);result=Finish(*alias);
  const std::string exact="\"vk\":"+std::to_string(pair[0])+",\"measured\":false,\"poll_reads\":\"0\",\"down_returns\":\"0\"";
  Check(result.find("\"outcome\":\"unavailable\"")!=std::string::npos&&result.find("\"modifier_alias_ambiguous\":true")!=std::string::npos&&result.find("\"all_requested_observed\":null")!=std::string::npos&&result.find("\"keys_observed\":0")!=std::string::npos&&result.find(exact)!=std::string::npos,"generic/side modifier alias high return is unavailable without inventing exact-key consumption");
  g_keyTrace[pair[0]].gaks.record(0x8000);Check(JobReplyLocked(*alias)==result,"later exact modifier poll cannot rewrite frozen alias ambiguity");
 }
 auto exactModifier=KeyJob({VK_SHIFT});g_keyTrace[VK_SHIFT].gks.record(0x8000);g_keyTrace[VK_LSHIFT].gaks.record(0x8000);result=Finish(*exactModifier);
 Check(result.find("\"all_requested_observed\":true")!=std::string::npos&&result.find("\"outcome\":\"observed\"")!=std::string::npos,"exact modifier high remains positive when an alias also returns high");
 auto noModifierPoll=KeyJob({VK_CONTROL});result=Finish(*noModifierPoll);Check(result.find("\"outcome\":\"not_observed\"")!=std::string::npos,"historical modifier alias high without new polls remains measured absence");
 auto lowAlias=KeyJob({VK_MENU});g_keyTrace[VK_LMENU].gaks.record(0);g_keyTrace[VK_RMENU].gks.record(0);result=Finish(*lowAlias);Check(result.find("\"outcome\":\"not_observed\"")!=std::string::npos,"up-only modifier alias polls do not make exact absence ambiguous");
 SetVkState(VK_RSHIFT,true);auto oppositeHeld=KeyJob({VK_LSHIFT});g_keyTrace[VK_SHIFT].gaks.record(0x8000);result=Finish(*oppositeHeld);SetVkState(VK_RSHIFT,false);
 Check(result.find("\"modifier_alias_ambiguous\":true")!=std::string::npos&&result.find("\"outcome\":\"unavailable\"")!=std::string::npos,"generic poll with an opposite pre-held side cannot prove requested side consumption");
 auto busyAlias=KeyJob({VK_CONTROL});g_keyTrace[VK_LCONTROL].gaks.writing=1;result=Finish(*busyAlias);g_keyTrace[VK_LCONTROL].gaks.writing=0;
 Check(result.find("\"outcome\":\"unavailable\"")!=std::string::npos,"unavailable alias snapshot cannot prove absence for an unread exact modifier");
 auto exactBusyAlias=KeyJob({VK_MENU});g_keyTrace[VK_MENU].gaks.record(0x8000);g_keyTrace[VK_LMENU].gaks.writing=1;result=Finish(*exactBusyAlias);g_keyTrace[VK_LMENU].gaks.writing=0;
 Check(result.find("\"all_requested_observed\":true")!=std::string::npos,"exact modifier high is sufficient despite an unavailable alias snapshot");
 auto noKeys=KeyJob({});result=Finish(*noKeys);Check(result.find("\"outcome\":\"not_applicable\"")!=std::string::npos,"wait-only sequence has no invented reader requirement");
 InputJob queued;result=JobReplyLocked(queued,true);Check(result.find("\"outcome\":\"pending\"")!=std::string::npos,"queued admission does not claim a completed read verdict");
 int axes[6]={};InputJob pad;pad.id=99;pad.pad=true;pad.kind=InputJob::Pad;pad.started=true;pad.resources={256};pad.generation[256]=7;g_resourceOwner[256]=99;g_resourceGeneration[256]=7;
 JobReadsStart(pad,HousekeepingNow());JobPadPublish(0x1000,0,axes,true,pad.id);g_padReturn.count=1000;
 XST state{};state.gp.b=0x1000;{std::lock_guard<std::mutex> lock(g_padStateMx);PadRecordReturnLocked(0,state,true);}
 result=Finish(pad);Check(result.find("\"pad_returns\":\"1\"")!=std::string::npos&&result.find("\"outcome\":\"observed\"")!=std::string::npos,"pad receipt matches job-owned publication rather than global historical count");
 Check(g_padReadOwner==0&&g_padButtons==0,"pad evidence freezes before exact job cleanup clears publication");
 {std::lock_guard<std::mutex> lock(g_padStateMx);PadRecordReturnLocked(0,state,true);}Check(JobReplyLocked(pad)==result,"cleanup neutral/foreign returns cannot overwrite the terminal pad verdict");
 InputJob absent;absent.id=100;absent.pad=true;absent.kind=InputJob::Pad;absent.started=true;JobReadsStart(absent,HousekeepingNow());JobPadPublish(0x1000,0,axes,true,absent.id);result=Finish(absent);Check(result.find("\"outcome\":\"not_observed\"")!=std::string::npos,"unread finite pad job is reported explicitly");
 InputJob overflow;overflow.id=101;overflow.pad=true;overflow.started=true;JobReadsStart(overflow,HousekeepingNow());JobPadPublish(0x1000,0,axes,true,overflow.id);g_padReadReturns=(std::numeric_limits<unsigned long long>::max)();{std::lock_guard<std::mutex> lock(g_padStateMx);PadRecordReturnLocked(0,state,true);}result=Finish(overflow);Check(result.find("\"measured\":false")!=std::string::npos,"overflow is unavailable evidence rather than a zero-read verdict");
 vi_input_reads::Sample before,after;before.reads[0]=2;before.down[0]=2;after.reads[0]=1;after.down[0]=1;Check(!vi_input_reads::Difference(before,after).valid,"regressed counters are not absence evidence");
 ChannelTrace concurrent;concurrent.writing=1;long long count=0,down=0;Check(!concurrent.snapshot(count,down),"busy reader snapshot is bounded and unavailable");concurrent.writing=0;
 std::thread producer([&](){for(unsigned n=0;n<10000;++n)concurrent.record(0x80);});bool coherent=true;for(unsigned n=0;n<1000;++n)if(concurrent.snapshot(count,down)&&count!=down)coherent=false;producer.join();Check(coherent&&concurrent.snapshot(count,down)&&count==10000&&down==10000,"concurrent snapshots retain paired actual reader counts");
 Check(vi_launch_terminal::Verify()&&!g_hwnd&&!g_jobWorker,"terminal writers retained; no product window or worker created");
 printf("job reader observation: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
