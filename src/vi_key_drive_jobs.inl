// Included after the common job scheduler. No sleep or renderer access occurs
// under g_inputMx. Radar jobs may call JobPublishKeyDriveState with their own
// reserved key union; it never creates a nested job or takes another resource.
bool JobPublishKeyDriveState(InputJob& job,const std::vector<int>& desired,std::uint64_t targetQpc){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(job.done)return false;
 if(!job.qpcFrequency||!targetQpc){JobFinish(job,"failed","key_drive_publication_clock_unavailable");return false;}
 if(g_acProduct.load()){JobFinish(job,"cancelled","anticheat_detected");return false;}
 if(OwnerCursorActive()||job.claimEpoch!=g_jobClaimEpoch||(job.claimed&&HousekeepingNow()>=g_jobClaimExpiry)){JobFinish(job,"cancelled","key_drive_owner_changed");return false;}
 if(!job.captureBound||!CaptureScopeCurrent(job.captureScope)||job.target!=g_hwnd||!WndBindingInputAllowed(job.target)){JobFinish(job,"cancelled","key_drive_capture_scope_changed");return false;}
 for(int key:job.keys)if(!JobOwns(job,JobResource(key))){JobFinish(job,"cancelled","key_drive_resource_changed");return false;}
 for(int key:desired)if(key<8||key>255||std::find(job.keys.begin(),job.keys.end(),key)==job.keys.end()){JobFinish(job,"failed","key_drive_unreserved_key");return false;}
 JobMutation mutation;
 auto publish=[&](int key,bool down){
  const bool before=g_vk[key]!=0,accepted=KeyEvent(key,down),changed=before!=(g_vk[key]!=0);
  // KeyEvent can publish virtual/DI state before a later message-post failure.
  // Retain that partial application, then stop and clean only this job's keys.
  if(changed){++job.transitions;const auto at=vi_qpc::Now();if(!at||(job.qpcLast&&at<job.qpcLast)){JobFinish(job,"failed","key_drive_qpc_unavailable_or_regressed");return false;}if(!job.qpcFirst)job.qpcFirst=at;job.qpcLast=at;job.qpcLastDeadline=targetQpc;const auto late=at>targetQpc?at-targetQpc:0;job.qpcMaxLate=(std::max)(job.qpcMaxLate,late);job.maxLate=(ULONGLONG)std::ceil(vi_qpc::DeltaMs(job.qpcMaxLate,0,job.qpcFrequency));}
  if(!accepted||(g_vk[key]!=0)!=down){JobFinish(job,"failed","key_drive_publication_failed");return false;}return true;
 };
 // Releasing first prevents a left/right direction change publishing both keys.
 for(size_t i=job.keys.size();i-- >0;)if(g_vk[job.keys[i]]&&std::find(desired.begin(),desired.end(),job.keys[i])==desired.end())if(!publish(job.keys[i],false))return false;
 for(int key:job.keys)if(!g_vk[key]&&std::find(desired.begin(),desired.end(),key)!=desired.end())if(!publish(key,true))return false;
 return true;
}
void JobRunKeyDrive(InputJob& job,ULONGLONG now){
 (void)now;const auto sampled=vi_qpc::Now(),frequency=job.qpcFrequency;
 if(!sampled||!frequency||frequency>(std::numeric_limits<std::uint64_t>::max)()/60001||(job.keyDriveSampleQpc&&sampled<job.keyDriveSampleQpc)){JobFinish(job,"failed","key_drive_qpc_unavailable_or_regressed");return;}
 if(!job.qpcAnchor){job.qpcAnchor=sampled;job.qpcNativeStart=sampled;job.qpcNext=sampled;}
 if(sampled<job.qpcAnchor){JobFinish(job,"failed","key_drive_qpc_regressed");return;}job.keyDriveSampleQpc=sampled;
 if(sampled<job.qpcNext)return;
 if(job.latenessLimit&&sampled-job.qpcNext>vi_qpc::Milliseconds(job.latenessLimit,frequency)){JobFinish(job,"cancelled","key_drive_max_lateness_exceeded");return;}
 const auto ticks=sampled-job.qpcAnchor;const auto horizon=vi_qpc::Milliseconds(job.keyDrive.duration,frequency);
 // Clamp before converting: an arbitrarily late sample must finish, never wrap
 // the millisecond multiplication back into an earlier phase.
 const auto ms=ticks>=horizon?(std::uint64_t)job.keyDrive.duration:ticks/frequency*1000+ticks%frequency*1000/frequency;
 const auto elapsed=(unsigned)(std::min)(ms,(std::uint64_t)job.keyDrive.duration);const auto state=vi_key_drive::At(job.keyDrive,elapsed);
 if(!job.keyDriveCursor.Observe(elapsed,state.ordinal)){JobFinish(job,"failed","key_drive_clock_regressed");return;}
 job.keyDrivePhase=state.phase;job.skipped=(unsigned)(std::min)(job.keyDriveCursor.skipped,(std::uint64_t)(std::numeric_limits<unsigned>::max)());
 if(state.finished){JobFinish(job,"completed","");return;}
 if(!JobPublishKeyDriveState(job,state.desired,job.qpcAnchor+vi_qpc::Milliseconds(state.boundary,frequency)))return;
 job.phase=1;
 job.qpcNext=job.qpcAnchor+vi_qpc::Milliseconds(state.next,frequency);
}
void JobKeyDriveFields(JOut& out,const InputJob& job){
 out.num("key_drive_version",1);out.num("key_drive_phases",(double)job.keyDrive.phases.size());out.num("key_drive_phase",job.keyDrivePhase);out.num("key_drive_period_ms",job.keyDrive.period);out.num("key_drive_transition_bound",job.keyDrive.transitionBound);out.str("timing_clock","QueryPerformanceCounter");out.str("qpc_frequency",std::to_string(job.qpcFrequency));out.str("anchor_qpc_ticks",std::to_string(job.qpcAnchor));out.str("first_apply_qpc_ticks",std::to_string(job.qpcFirst));out.str("last_apply_qpc_ticks",std::to_string(job.qpcLast));out.str("finished_qpc_ticks",std::to_string(job.qpcFinished));out.num("skipped_scheduling_boundaries",job.skipped);out.bol("vehicle_motion_measured",false);out.raw("application_response","null");
 std::string keys="[";for(int key:job.keys){if(keys.size()>1)keys+=',';keys+=std::to_string(key);}out.raw("reserved_keys",keys+"]");
 out.str("key_drive_scope","virtual digital key-state transitions at anchored native PWM/timeline deadlines; posting failure may follow partial state publication and stops this job with owned cleanup; expired boundaries are skipped, not replayed; duty is requested key-on fraction, never analog steering angle, vehicle speed, route progress or game acceptance");
}
std::string ScheduleKeyDrive(const JObj& request){
 auto noInput=[](const std::string& message){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error",message);return out.done();};
 for(const auto& field:request){const auto& key=field.first;if(key!="cmd"&&key!="script"&&key!="throttle_key"&&key!="left_key"&&key!="right_key"&&key!="brake_key"&&key!="period_ms"&&key!="async"&&key!="replace"&&key!="owner"&&key!="force_claim"&&key!="expected_capture_scope"&&key!="admit_before_qpc_ticks"&&key!="admit_qpc_frequency"&&key!="max_lateness_ms")return noInput("unknown key-drive field '"+key+"'; no input was injected");}
 if(!JHas(request,"script")||request.at("script").t!=1)return noInput("key-drive requires a bounded string script; no input was injected");
 for(const char* key:{"throttle_key","left_key","right_key","brake_key"})if(JHas(request,key)&&request.at(key).t!=1)return noInput("key-drive bindings must be strings; no input was injected");
 for(const char* key:{"async","replace"})if(JHas(request,key)&&request.at(key).t!=3)return noInput("key-drive async/replace must be booleans; no input was injected");
 double period=100;if(!JobNumber(request,"period_ms",100,20,1000,period)||period!=std::floor(period))return noInput("key-drive period_ms must be integer20..1000; no input was injected");
 vi_key_drive::Bindings bindings;vi_key_drive::Plan plan;std::string error;
 if(!vi_key_drive::ParseBindings({JStr(request,"throttle_key","w"),JStr(request,"left_key","a"),JStr(request,"right_key","d"),JStr(request,"brake_key","s")},bindings,error)||!vi_key_drive::Parse(JStr(request,"script"),bindings,(unsigned)period,plan,error))return noInput(error);
 const auto frequency=vi_qpc::Frequency();if(!frequency||frequency>(std::numeric_limits<std::uint64_t>::max)()/60001||!vi_qpc::Now())return noInput("key-drive native QPC clock is unavailable; no input was injected");
 auto job=std::make_shared<InputJob>();job->kind=InputJob::KeyDrive;job->keyDrive=std::move(plan);job->keys=job->keyDrive.keys;job->duration=job->keyDrive.duration;job->qpcFrequency=frequency;
 if(JHas(request,"max_lateness_ms")){double cap;if(!JobNumber(request,"max_lateness_ms",0,1,5000,cap)||cap!=std::floor(cap))return noInput("max_lateness_ms must be integer1..5000; no input was injected");job->latenessLimit=(unsigned)cap;}
 for(int key:job->keys)job->resources.push_back(JobResource(key));
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(OwnerCursorActive())return noInput("the user controls this target; no key-drive input was injected");
 JObj admitted=request;
 if(!JHas(request,"expected_capture_scope")){
  if(JHas(request,"admit_before_qpc_ticks")||JHas(request,"admit_qpc_frequency"))return noInput("caller key-drive deadlines require an explicit capture scope; no input was injected");
  CaptureScope scope;if(!CaptureScopeTake(scope))return noInput("key-drive target scope is unavailable; no input was injected");admitted["expected_capture_scope"]=JV{1,CaptureScopeJson(scope),0,false};
 }
 // Atomic busy/manual-input refusal applies even when the caller wants to wait.
 // Waiting remains on the existing per-client completion path outside g_inputMx.
 const bool wait=!JBool(request,"async",false);admitted["async"]=JV{3,{},0,true};auto result=ScheduleInputJob(job,admitted);
 if(!job->id){JObj failure;if(ParseJson(result,failure))return noInput(JStr(failure,"error","key-drive was not admitted; no input was injected"));return noInput("key-drive was not admitted; no input was injected");}
 if(wait)g_waitJob=job;return result;
}
