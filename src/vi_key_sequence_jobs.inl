// Included after the existing job scheduler. The shared worker never sleeps under
// the input mutex; cancellation uses the same generations as all timed input.
void JobRunSequence(InputJob& job,ULONGLONG now) {
 if(now<job.next)return;
 job.maxLate=(std::max)(job.maxLate,now-job.next);
 if(job.sequenceStep>=job.sequence.size()){JobFinish(job,"completed","");return;}
 const auto& step=job.sequence[job.sequenceStep];
 if(job.phase==0){job.index=0;job.phase=step.keys.empty()?3:1;if(step.keys.empty()){job.next=now+step.ms;return;}}
 if(job.phase==1){KeyEvent(step.keys[job.index],true);++job.transitions;if(++job.index<(int)step.keys.size())job.next=now+15;else{job.phase=2;job.index=(int)step.keys.size()-1;job.next=now+step.ms;}return;}
 if(job.phase==2){KeyEvent(step.keys[job.index],false);++job.transitions;if(--job.index>=0){job.next=now+15;return;}}
 ++job.sequenceStep;job.phase=0;job.next=now;
 if(job.sequenceStep>=job.sequence.size())JobFinish(job,"completed","");
}
std::string ScheduleKeySequence(const JObj& request) {
 vi_key_sequence::Plan plan;std::string error;
 auto script=request.find("script");if(script==request.end()||script->second.t!=1)return ErrJ("sequence requires a string script; no input was injected");
 if(!vi_key_sequence::Parse(JStr(request,"script"),plan,error))return ErrJ(error);
 if(JHas(request,"async")&&(request.find("async")->second.t!=3||!JBool(request,"async",false)))return ErrJ("sequence is always asynchronous; async must be true; no input was injected");
 auto job=std::make_shared<InputJob>();job->kind=InputJob::Sequence;job->keys=plan.keys;job->sequence=std::move(plan.steps);job->duration=plan.duration;
 for(int key:job->keys){int resource=JobResource(key);if(std::find(job->resources.begin(),job->resources.end(),resource)==job->resources.end())job->resources.push_back(resource);}
 JObj admitted=request;admitted["async"]=JV{3,{},0,true};return ScheduleInputJob(job,admitted);
}
