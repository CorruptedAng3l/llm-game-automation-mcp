// Included after the existing job scheduler. The shared worker never sleeps under
// the input mutex; cancellation uses the same generations as all timed input.
void JobRunSequence(InputJob& job,ULONGLONG now) {
 (void)now;const auto sampled=vi_qpc::Now();if(!sampled||!job.qpcFrequency){JobFinish(job,"failed","sequence_qpc_unavailable");return;}
 if((job.qpcAnchor&&sampled<job.qpcAnchor)||(job.qpcLast&&sampled<job.qpcLast)){JobFinish(job,"failed","sequence_qpc_regressed");return;}
 if(!job.qpcNativeStart)job.qpcNativeStart=sampled;
 if(!job.qpcAnchor){job.qpcAnchor=sampled;job.qpcNext=sampled+vi_qpc::Milliseconds(job.sequence.empty()?0:job.sequence[0].at,job.qpcFrequency);}
 for(size_t progress=0;progress<=job.sequence.size();++progress){
 if(sampled<job.qpcNext)return;
 if(job.sequenceStep>=job.sequence.size()){JobFinish(job,"completed","");return;}
 const auto& step=job.sequence[job.sequenceStep];
 auto deadline=[&](std::uint64_t offset){return job.qpcAnchor+vi_qpc::Milliseconds(step.at+offset,job.qpcFrequency);};
 auto applied=[&](std::uint64_t target,bool edge=false,int vk=0,bool down=false){job.qpcLast=vi_qpc::Now();if(edge){if(!job.qpcFirst)job.qpcFirst=job.qpcLast;JobSequenceRecordEdge(job,vk,down,job.qpcLast);}job.qpcLastDeadline=target;const auto late=job.qpcLast>target?job.qpcLast-target:0;job.qpcMaxLate=(std::max)(job.qpcMaxLate,late);job.maxLate=(ULONGLONG)std::ceil(vi_qpc::DeltaMs(job.qpcMaxLate,0,job.qpcFrequency));};
 auto overdue=[&](std::uint64_t target){const auto at=vi_qpc::Now();if(job.latenessLimit&&at>target&&at-target>vi_qpc::Milliseconds(job.latenessLimit,job.qpcFrequency)){JobFinish(job,"cancelled","sequence_max_lateness_exceeded");return true;}return false;};
 if(job.phase==0){job.index=0;job.phase=step.keys.empty()?3:1;if(step.keys.empty()){job.qpcNext=deadline(step.ms);if(sampled<job.qpcNext)return;}}
 if(job.phase==1){const auto target=deadline(15u*job.index);if(overdue(target))return;KeyEvent(step.keys[job.index],true);++job.transitions;applied(target,true,step.keys[job.index],true);if(++job.index<(int)step.keys.size())job.qpcNext=deadline(15u*job.index);else{job.phase=2;job.index=(int)step.keys.size()-1;job.qpcNext=deadline(15u*(step.keys.size()-1)+step.ms);}return;}
 if(job.phase==2){const auto target=deadline(15u*(2*(step.keys.size()-1)-job.index)+step.ms);if(overdue(target))return;KeyEvent(step.keys[job.index],false);++job.transitions;applied(target,true,step.keys[job.index],false);if(--job.index>=0){job.qpcNext=deadline(15u*(2*(step.keys.size()-1)-job.index)+step.ms);return;}}
 if(job.phase==3)applied(deadline(step.ms));
 ++job.sequenceStep;job.phase=0;job.qpcNext=job.sequenceStep<job.sequence.size()?job.qpcAnchor+vi_qpc::Milliseconds(job.sequence[job.sequenceStep].at,job.qpcFrequency):deadline(step.ms+30u*(step.keys.empty()?0:step.keys.size()-1));
 if(job.sequenceStep>=job.sequence.size()){JobFinish(job,"completed","");return;}
 if(!step.keys.empty())return;
 }
}
std::string ScheduleKeySequence(const JObj& request) {
 auto noInput=[](const char* error){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error",error);return out.done();};
 vi_key_sequence::Plan plan;std::string error;
 auto script=request.find("script");if(script==request.end()||script->second.t!=1)return ErrJ("sequence requires a string script; no input was injected");
 if(!vi_key_sequence::Parse(JStr(request,"script"),plan,error))return ErrJ(error);
 const auto frequency=vi_qpc::Frequency();if(!frequency||!vi_qpc::Now())return ErrJ("sequence QPC clock unavailable; no input was injected");
 if(JHas(request,"async")&&(request.find("async")->second.t!=3||!JBool(request,"async",false)))return ErrJ("sequence is always asynchronous; async must be true; no input was injected");
 auto job=std::make_shared<InputJob>();job->kind=InputJob::Sequence;job->qpcFrequency=frequency;job->keys=plan.keys;job->sequence=std::move(plan.steps);job->duration=plan.duration;
 if(JHas(request,"max_lateness_ms")){double ms;if(!JobNumber(request,"max_lateness_ms",0,1,5000,ms)||ms!=std::floor(ms))return ErrJ("sequence max_lateness_ms requires integer1..5000; no input was injected");job->latenessLimit=(unsigned)ms;}
 if(JHas(request,"start_hook_frame_index")){
  std::uint64_t target=0,eventFrequency=0;double timeout=5000;const long current=C.present_ours;
  if(JHas(request,"start_qpc_ticks")||!JHas(request,"expected_capture_scope")||request.find("expected_capture_scope")->second.t!=1||request.find("start_hook_frame_index")->second.t!=1||!JHas(request,"start_qpc_frequency")||request.find("start_qpc_frequency")->second.t!=1||!vi_sequence_anchor::Parse(JStr(request,"start_hook_frame_index"),target)||!vi_sequence_anchor::Parse(JStr(request,"start_qpc_frequency"),eventFrequency)||eventFrequency!=frequency||current<=0||target>(std::uint64_t)LONG_MAX||target<=(std::uint64_t)current||target-(std::uint64_t)current>3600)return noInput("sequence Present anchor requires a future hook frame within3600, same QPC frequency and capture scope; no input was injected");
  if(!JobNumber(request,"start_hook_timeout_ms",5000,100,60000,timeout)||timeout!=std::floor(timeout)||timeout+job->duration>118000)return noInput("sequence Present wait plus duration must fit118000ms with timeout100..60000; no input was injected");
  job->presentTarget=target;job->presentTimeout=(unsigned)timeout;job->presentDeadline=HousekeepingNow()+job->presentTimeout;
 }else if(JHas(request,"start_hook_timeout_ms"))return noInput("start_hook_timeout_ms requires start_hook_frame_index; no input was injected");
 else if(JHas(request,"start_qpc_ticks")||JHas(request,"start_qpc_frequency")){
  std::uint64_t anchor=0,eventFrequency=0;
  if(!JHas(request,"expected_capture_scope")||request.find("expected_capture_scope")->second.t!=1||!JHas(request,"start_qpc_ticks")||request.find("start_qpc_ticks")->second.t!=1||!JHas(request,"start_qpc_frequency")||request.find("start_qpc_frequency")->second.t!=1||!vi_sequence_anchor::Parse(JStr(request,"start_qpc_ticks"),anchor)||!vi_sequence_anchor::Parse(JStr(request,"start_qpc_frequency"),eventFrequency)||!vi_sequence_anchor::Current(anchor,eventFrequency,vi_qpc::Now(),frequency))return noInput("sequence event anchor needs same-clock past QPC within6000ms and matching capture scope; no input was injected");
  job->qpcAnchor=anchor;job->qpcNext=anchor+vi_qpc::Milliseconds(job->sequence.empty()?0:job->sequence[0].at,frequency);job->eventAnchor=true;
 }
 if(JHas(request,"guard_ms")){double ms;if(!JobNumber(request,"guard_ms",0,200,10000,ms)||ms!=std::floor(ms)||JStr(request,"owner").empty())return ErrJ("sequence guard_ms requires integer200..10000 and a nonempty owner; no input was injected");job->sequenceGuard.interval=(std::uint64_t)ms;}
 for(int key:job->keys){int resource=JobResource(key);if(std::find(job->resources.begin(),job->resources.end(),resource)==job->resources.end())job->resources.push_back(resource);}
 JObj admitted=request;admitted["async"]=JV{3,{},0,true};return ScheduleInputJob(job,admitted);
}
