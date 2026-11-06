#include "vi_job_time.h"
#include "vi_sequence_guard.h"
#include "vi_sequence_anchor.h"
#include "vi_sequence_frame.h"
#include "vi_input_read_observation.h"
#include "vi_radar.h"
#include "vi_key_drive.h"
#include "vi_input_deadline.inl"
// One target-side worker. All state transitions and ownership checks share g_inputMx.
// Native frame checkpoints only apply bounded pad publications; they never wait.
struct InputJob {
 enum Kind{Keys,Pulse,Pad,Ramp,Timeline,Placement,Sequence,Radar,KeyDrive} kind=Keys;
 struct PadPoint{ULONGLONG at=0;WORD buttons=0;int mask=0,axis[6]={};std::string name;
  bool published=false,prepared=false,pressEdge=false;ULONGLONG publishedAt=0,preparedAt=0,axesAt=0;long publishedFrame=0,preparedFrame=0,axesFrame=0;int separated=-1;};
 unsigned long long id=0,claimEpoch=0;std::string owner,state="queued",reason;
 HWND target=nullptr;bool claimed=false,async=false,done=false,started=false,pad=false,frames=false,strictFrames=false;
 ULONGLONG accepted=0,start=0,next=0,finished=0,duration=0,ms=0,gap=0,period=0,onMs=0,endHold=0;
 int count=1,repeat=0,phase=0,index=0,mask=0,from[6]={},to[6]={};WORD buttons=0;bool clamped=false,pulseDown=false;
 std::vector<int> keys,resources;std::vector<PadPoint> points;size_t point=0;
 bool staggerEnabled=false,guardPending=false,padSnapshot=false,movementKnown=false,movementNative=false,cleanupPublished=false;
 ULONGLONG staggerMs=0,timelineShift=0,guardAt=0,movementAt=0;size_t timelineNext=0,guardPoint=0;long movementFrame=0;
 WORD publishedButtons=0;int publishedMask=0,publishedAxes[6]={};
 std::vector<vi_key_sequence::Step> sequence;size_t sequenceStep=0;
 std::uint64_t qpcFrequency=0,qpcAnchor=0,qpcNext=0,qpcLast=0,qpcLastDeadline=0,qpcMaxLate=0,qpcFinished=0;
 std::uint64_t qpcNativeStart=0,qpcFirst=0;unsigned latenessLimit=0;bool eventAnchor=false;
 std::uint64_t presentTarget=0,presentAnchor=0;ULONGLONG presentDeadline=0;unsigned presentTimeout=0;
 vi_sequence_guard::State sequenceGuard;std::uint64_t observerQpc=0;
 unsigned long long generation[257]={};unsigned transitions=0,updates=0,skipped=0;ULONGLONG maxLate=0;long firstFrame=0,lastFrame=0;ULONGLONG lastApply=0;
 CaptureScope captureScope;bool captureBound=false;std::uint64_t admissionDeadlineQpc=0;
 CaptureScope finishedCaptureScope;bool finishedCaptureScopeValid=false;
 vi_radar::Config radarConfig;vi_radar::State radarState;std::vector<int> radarForward;int radarLeft=0,radarRight=0;
 ULONGLONG radarLeaseAt=0,radarLeaseMs=1000;unsigned radarObservationCount=0;std::uint64_t radarLastQpc=0;
 std::vector<vi_radar::Trace> radarTrace;unsigned radarTraceDropped=0;
 std::string radarRequestId;
 vi_key_drive::Plan keyDrive;vi_key_pwm::Cursor keyDriveCursor;unsigned keyDrivePhase=0;std::uint64_t keyDriveSampleQpc=0;
 vi_input_reads::State readObservation;
 vi_sequence_edges::Trace sequenceEdges;
 PointerPath pointerPath;POINT pointerStart{},pointerEnd{},expected{};int pointerSteps=0,consumed=0,buttonIndex=0;
 unsigned long long motionToken=0,pressToken=0,releaseToken=0;ULONGLONG phaseAt=0,pressedAt=0,releasedAt=0,finalAt=0,pollCap=1000,dwell=200,stepMs=0;
 bool downSeen=false,upSeen=false,finalSeen=false,finalBeforeRelease=false,edgeContact=false,positionSeen=false;int pointerReader=0;
 HANDLE completion=nullptr;~InputJob(){if(completion)CloseHandle(completion);}
};
void JobRunSequence(InputJob&,ULONGLONG);
void JobRunRadar(InputJob&,ULONGLONG);
void RadarJobFields(JOut&,const InputJob&);
void JobRunKeyDrive(InputJob&,ULONGLONG);
void JobKeyDriveFields(JOut&,const InputJob&);
bool JobPublishKeyDriveState(InputJob&,const std::vector<int>&,std::uint64_t);
void JobSequenceRecordEdge(InputJob& job,int vk,bool down,std::uint64_t qpc){vi_sequence_edges::Record(job.sequenceEdges,(unsigned)job.sequenceStep,job.sequence[job.sequenceStep].line,(unsigned)vk,down,qpc,CompletedPresentAt(job.target,qpc));}
void JobRunStaggeredTimeline(InputJob&,ULONGLONG);
void JobTimelinePublish(InputJob&,WORD,int,const int*,ULONGLONG);
void JobTimelineRecord(InputJob&,size_t,ULONGLONG,bool);
void JobTimelineFields(JOut&,const InputJob&,bool);
std::deque<std::shared_ptr<InputJob>> g_jobs;
unsigned long long g_nextJob=0,g_resourceOwner[257]={},g_resourceGeneration[257]={};
std::atomic<unsigned long long> g_jobClaimEpoch{1};std::atomic<ULONGLONG> g_jobClaimExpiry{0};
thread_local unsigned long long g_admittedJobEpoch=0;thread_local bool g_admittedJobClaimed=false;
thread_local std::shared_ptr<InputJob> g_waitJob;
HANDLE g_jobWake=nullptr;bool g_jobWorker=false;
int JobResource(int vk){return vk==VK_SHIFT||vk==VK_LSHIFT||vk==VK_RSHIFT?VK_SHIFT:vk==VK_CONTROL||vk==VK_LCONTROL||vk==VK_RCONTROL?VK_CONTROL:vk==VK_MENU||vk==VK_LMENU||vk==VK_RMENU?VK_MENU:vk;}
struct JobMutation {bool prior;JobMutation():prior(g_jobApplying){g_jobApplying=true;}~JobMutation(){g_jobApplying=prior;}};
void JobPadPublish(WORD buttons,int mask,const int* values,bool active=true,unsigned long long readOwner=0){std::lock_guard<std::mutex> lock(g_padStateMx);g_padLT=values[0];g_padRT=values[1];g_padLX=values[2];g_padLY=values[3];g_padRX=values[4];g_padRY=values[5];g_padButtons=buttons;g_padSetMask=mask;g_padActive=active;g_padPacket++;PadPublicationChangedLocked(readOwner);}
#include "vi_job_reads.inl"
bool JobOwns(const InputJob& job,int resource){return g_resourceOwner[resource]==job.id&&g_resourceGeneration[resource]==job.generation[resource];}
// Pure preview of JobFinish's exact keyboard/mouse release effects. Caller
// holds g_inputMx. A reserved future Sequence key or pad resource proves nothing.
bool RetargetHeldJobsReleaseAllLocked(){
 bool remaining[256]{};for(int key=1;key<256;key++)remaining[key]=g_vk[key]!=0;
 auto release=[&](int key){
  remaining[key]=false;
  auto mirror=[&](int generic,int left,int right){remaining[generic]=remaining[left]||remaining[right];};
  switch(key){
  case VK_SHIFT:remaining[VK_LSHIFT]=false;break;
  case VK_CONTROL:remaining[VK_LCONTROL]=false;break;
  case VK_MENU:remaining[VK_LMENU]=false;break;
  case VK_LSHIFT:case VK_RSHIFT:mirror(VK_SHIFT,VK_LSHIFT,VK_RSHIFT);break;
  case VK_LCONTROL:case VK_RCONTROL:mirror(VK_CONTROL,VK_LCONTROL,VK_RCONTROL);break;
  case VK_LMENU:case VK_RMENU:mirror(VK_MENU,VK_LMENU,VK_RMENU);break;
  }
 };
 for(const auto& job:g_jobs)if(!job->done&&job->started&&!job->pad&&job->target==g_hwnd){
  const auto& releasing=job->kind==InputJob::Sequence&&job->sequenceStep<job->sequence.size()?job->sequence[job->sequenceStep].keys:job->keys;
  for(size_t i=releasing.size();i-- >0;){const int key=releasing[i];if(key>0&&key<256&&JobOwns(*job,JobResource(key))&&remaining[key])release(key);}
 }
 for(int key=1;key<256;key++)if(remaining[key])return false;
 return true;
}
void JobFinish(InputJob& job,const char* state,const char* reason){
 if(job.done)return;JobReadsSample(job,true);JobMutation mutation;
 if(job.started){if(job.pad){if(JobOwns(job,256)){int zero[6]={};JobPadPublish(0,0,zero);if(job.kind==InputJob::Timeline){job.cleanupPublished=true;job.padSnapshot=true;job.publishedButtons=0;job.publishedMask=0;for(int i=0;i<6;i++)job.publishedAxes[i]=0;}}}
  else {const auto& releasing=job.kind==InputJob::Sequence&&job.sequenceStep<job.sequence.size()?job.sequence[job.sequenceStep].keys:job.keys;for(size_t i=releasing.size();i-- >0;)if(JobOwns(job,JobResource(releasing[i]))&&g_vk[releasing[i]]){KeyEvent(releasing[i],false);++job.transitions;}}
  for(int resource:job.resources)if(JobOwns(job,resource)){g_resourceOwner[resource]=0;++g_resourceGeneration[resource];}}
 job.state=state;job.reason=reason;job.done=true;job.finished=HousekeepingNow();if(job.kind==InputJob::Sequence||job.kind==InputJob::Radar||job.kind==InputJob::KeyDrive){job.qpcFinished=vi_qpc::Now();job.finishedCaptureScopeValid=CaptureScopeTake(job.finishedCaptureScope)&&job.finishedCaptureScope.window==job.target;}SetEvent(job.completion);
 if(job.kind==InputJob::Placement&&job.pressedAt&&!job.releasedAt&&!g_vk[job.keys[0]]){job.releasedAt=job.finished;job.releaseToken=g_diMouseObservation[job.buttonIndex].release;}
}
void JobsCancelAll(const char* reason){std::lock_guard<std::recursive_mutex> lock(g_inputMx);for(auto& job:g_jobs)if(!job->done)JobFinish(*job,"cancelled",reason);if(g_jobWake)SetEvent(g_jobWake);}
void JobsOverrideKey(int vk){const int resource=JobResource(vk);for(auto& job:g_jobs)if(!job->done&&std::find(job->resources.begin(),job->resources.end(),resource)!=job->resources.end())JobFinish(*job,"cancelled","manual_key_override");++g_resourceGeneration[resource];g_resourceOwner[resource]=0;}
void JobsOverridePad(){for(auto& job:g_jobs)if(!job->done&&job->pad)JobFinish(*job,"cancelled","manual_pad_override");++g_resourceGeneration[256];g_resourceOwner[256]=0;}
void JobsOverrideCursor(const char* reason){for(auto& job:g_jobs)if(!job->done&&job->kind==InputJob::Placement)JobFinish(*job,"cancelled",reason);++g_resourceGeneration[0];g_resourceOwner[0]=0;}
void JobsEpochChange(){++g_jobClaimEpoch;JobsCancelAll("claim_changed");}
void JobPrune(ULONGLONG now){size_t terminal=0;for(auto& job:g_jobs)if(job->done)++terminal;for(auto i=g_jobs.begin();i!=g_jobs.end();){if((*i)->done&&(terminal>128||vi_job_time::HistoryExpired(now,(*i)->finished))){i=g_jobs.erase(i);--terminal;}else ++i;}}
bool JobConflicts(const InputJob& a,const InputJob& b){for(int x:a.resources)if(std::find(b.resources.begin(),b.resources.end(),x)!=b.resources.end())return true;return false;}
void PlacementRun(InputJob& job,ULONGLONG now){
 if(job.captureBound&&!CaptureScopeCurrent(job.captureScope)){JobFinish(job,"cancelled","capture_scope_changed");return;}
 if(!job.pointerPath.Valid()){JobFinish(job,"cancelled","pointer_target_resized");return;}
 auto& observation=g_diMouseObservation[job.buttonIndex];const int key=job.keys[0];
 if(job.phase&&((g_pointerIssued!=job.motionToken)||(g_vcurX!=job.expected.x)||(g_vcurY!=job.expected.y))){JobFinish(job,"cancelled","cursor_generation_changed");return;}
 if(job.phase>=2&&(observation.press!=job.pressToken||(job.phase<6&&!g_vk[key]))){JobFinish(job,"cancelled","button_generation_changed");return;}
 const bool positionSeen=job.motionToken&&g_pointerSeen==job.motionToken;
 if(positionSeen){job.positionSeen=true;job.pointerReader=g_pointerReader;}
 auto waitExpired=[&](const char* why){if(now-job.phaseAt<job.pollCap)return false;JobFinish(job,"failed",why);return true;};
 auto expose=[&](LONG x,LONG y,PointerPath* path){MouseMoveTo(x,y,false,0,0,false,path);job.expected={g_vcurX,g_vcurY};job.motionToken=g_pointerIssued;job.positionSeen=false;job.phaseAt=now;job.edgeContact=job.edgeContact||job.pointerPath.Contact(job.expected.x,job.expected.y)!=0;};
 if(job.phase==0){g_cfg.cursor=1;PointerPath hover;if(!hover.Init(job.pointerStart.x,job.pointerStart.y)){JobFinish(job,"failed","pointer_target_unavailable");return;}expose(job.pointerStart.x,job.pointerStart.y,&hover);job.phase=1;return;}
 if(job.phase==1){if(!positionSeen){waitExpired("start_position_not_observed");return;}
  if(!MouseButtonChecked(key,true,&job.expected,nullptr,&job.pointerPath.scope)){JobFinish(job,"failed","start_position_changed");return;}++job.transitions;job.pressToken=observation.press;job.pressedAt=now;job.motionToken=++g_pointerIssued;job.positionSeen=false;job.phaseAt=now;job.phase=2;return;}
 if(job.phase==2){job.downSeen=observation.state_down==job.pressToken||observation.buffer_down==job.pressToken;
  if(!job.downSeen||!positionSeen){waitExpired(!job.downSeen?"button_down_not_observed":"press_position_not_observed");return;}job.phase=3;job.next=now;return;}
 if(job.phase==3){if(now<job.next)return;const int step=job.consumed+1;
  LONG x=(LONG)(job.pointerStart.x+((long long)job.pointerEnd.x-job.pointerStart.x)*step/job.pointerSteps),y=(LONG)(job.pointerStart.y+((long long)job.pointerEnd.y-job.pointerStart.y)*step/job.pointerSteps);
  expose(x,y,&job.pointerPath);job.phase=4;return;}
 if(job.phase==4){if(!positionSeen){waitExpired("step_position_not_observed");return;}++job.consumed;
  if(job.consumed==job.pointerSteps){job.finalSeen=true;job.finalAt=now;job.phase=5;}else{job.next=now+job.stepMs;job.phase=3;}return;}
 if(job.phase==5){if(now-job.finalAt<job.dwell||now-job.pressedAt<job.ms)return;
  MouseButton(key,false);++job.transitions;job.releaseToken=observation.release;job.releasedAt=now;job.finalBeforeRelease=job.finalSeen&&job.finalAt<=now;job.phaseAt=now;job.phase=6;return;}
 if(job.phase==6){if(observation.release!=job.releaseToken||g_vk[key]){JobFinish(job,"cancelled","release_generation_changed");return;}
  job.upSeen=observation.state_up==job.releaseToken||observation.buffer_up==job.releaseToken;if(!job.upSeen){waitExpired("button_release_not_observed");return;}job.phase=7;JobFinish(job,"completed","");}
}
void JobRun(InputJob& job,ULONGLONG now,bool frame){
 if(job.done)return;
 DWORD targetPid=0;if(job.target)GetWindowThreadProcessId(job.target,&targetPid);if(job.target!=g_hwnd||!IsWindow(job.target)||targetPid!=g_pid){JobFinish(job,"cancelled","target_changed");return;}
 if(!job.pad&&!WndBindingInputAllowed(job.target)){JobFinish(job,"cancelled","window_binding_quarantined");return;}
 if(job.claimEpoch!=g_jobClaimEpoch||(job.claimed&&now>=g_jobClaimExpiry)){JobFinish(job,"cancelled","claim_expired_or_changed");return;}
 if(job.kind==InputJob::Sequence&&job.sequenceGuard.Expired(now)){JobFinish(job,"cancelled","sequence_observer_expired");return;}
 if(job.captureBound&&!CaptureScopeCurrent(job.captureScope)){JobFinish(job,"cancelled",job.kind==InputJob::Placement?"capture_scope_changed":"input_capture_scope_changed");return;}
 if(now-job.accepted>=120000){JobFinish(job,"cancelled","job_lifetime_cap");return;}
 if(!job.started){
  if(job.admissionDeadlineQpc){if(OwnerCursorActive()){JobFinish(job,"cancelled","input_admission_user_active");return;}if(vi_input_deadline::Expired(job.admissionDeadlineQpc,vi_qpc::Now())){JobFinish(job,"cancelled","input_admission_expired");return;}}
  // Earlier conflicting queued jobs keep FIFO; unrelated resources may proceed.
  for(auto& earlier:g_jobs){if(earlier.get()==&job)break;if(!earlier->done&&JobConflicts(job,*earlier))return;}
  for(int resource:job.resources)if(g_resourceOwner[resource])return;
  if(job.frames&&!frame){if(now-job.accepted>=1500)JobFinish(job,"failed","native_present_start_timeout");return;}
  for(int resource:job.resources){g_resourceOwner[resource]=job.id;job.generation[resource]=++g_resourceGeneration[resource];}
  job.started=true;job.state="running";job.start=now;job.next=now;job.firstFrame=job.lastFrame=C.present_ours;job.lastApply=now;
  if(!job.presentTarget)JobReadsStart(job,now);
 }
 const ULONGLONG elapsed=now-job.start;
 if(job.guardPending&&now-job.guardAt>=job.staggerMs+1500){JobFinish(job,"failed","stagger_present_stalled");return;}
 if(job.frames&&!frame){if(elapsed>=job.duration+job.endHold+job.timelineShift+1500)JobFinish(job,"failed","native_present_stalled");return;}
 JobMutation mutation;
 if(job.kind==InputJob::Sequence){
  if(job.presentTarget&&!job.qpcAnchor){
   const long index=frame?t_frameIndex:(long)C.present_ours;
   const auto check=vi_sequence_frame::Evaluate(job.presentTarget,index>0?(std::uint64_t)index:0,now,job.presentDeadline,t_frameQpc,frame);
   if(check==vi_sequence_frame::Waiting)return;
   if(check!=vi_sequence_frame::Fire){JobFinish(job,"cancelled",check==vi_sequence_frame::Expired?"sequence_present_timeout":check==vi_sequence_frame::Missed?"sequence_present_missed":"sequence_present_invalid");return;}
   job.qpcAnchor=t_frameQpc;job.presentAnchor=job.presentTarget;job.qpcNext=job.qpcAnchor+vi_qpc::Milliseconds(job.sequence.empty()?0:job.sequence[0].at,job.qpcFrequency);JobReadsStart(job,now);
  }
  JobRunSequence(job,now);return;
 }
 if(job.kind==InputJob::Placement){PlacementRun(job,now);return;}
 if(job.kind==InputJob::Radar){JobRunRadar(job,now);return;}
 if(job.kind==InputJob::KeyDrive){JobRunKeyDrive(job,now);return;}
 if(job.kind==InputJob::Keys){if(now<job.next)return;job.maxLate=(std::max)(job.maxLate,now-job.next);
  if(job.phase==0){KeyEvent(job.keys[job.index],true);++job.transitions;if(++job.index<(int)job.keys.size())job.next=now+15;else{job.phase=1;job.index=(int)job.keys.size()-1;job.next=now+job.ms;}return;}
  KeyEvent(job.keys[job.index],false);++job.transitions;if(--job.index>=0){job.next=now+15;return;}
  if(++job.repeat>=job.count){JobFinish(job,"completed","");return;}job.phase=0;job.index=0;job.next=now+job.gap;return;
 }
 if(job.kind==InputJob::Pulse){if(elapsed>=job.duration){JobFinish(job,"completed","");return;}
  const bool down=job.onMs&&elapsed%job.period<job.onMs;if(job.onMs&&job.onMs<job.period){const size_t boundary=(size_t)(elapsed/job.period*2+(elapsed%job.period>=job.onMs));if(job.phase&&boundary>job.point+1)job.skipped+=(unsigned)(boundary-job.point-1);job.point=boundary;}if(job.phase==0||down!=job.pulseDown){for(int key:job.keys)KeyEvent(key,down);job.transitions+=(unsigned)job.keys.size();job.pulseDown=down;job.phase=1;}return;
 }
 if(job.kind==InputJob::Timeline&&job.staggerEnabled){JobRunStaggeredTimeline(job,now);return;}
 if(job.frames&&job.lastFrame==C.present_ours&&job.updates)return;
 int values[6]={};WORD buttons=job.buttons;int mask=job.mask;
 if(job.kind==InputJob::Pad){if(job.updates&&elapsed>=job.duration){JobFinish(job,"completed","");return;}for(int i=0;i<6;i++)values[i]=job.to[i];}
 else if(job.kind==InputJob::Ramp){if(job.updates&&elapsed>=job.duration+job.endHold){if(job.point==0){for(int i=0;i<6;i++)values[i]=job.to[i];JobPadPublish(job.buttons,job.mask,values,true,job.id);++job.updates;job.point=1;job.lastApply=now;job.lastFrame=C.present_ours;return;}JobFinish(job,"completed","");return;}
  const double fraction=job.duration?(std::min)(1.0,(double)elapsed/job.duration):1;for(int i=0;i<6;i++)values[i]=(int)std::llround(job.from[i]+(job.to[i]-job.from[i])*fraction);if(fraction>=1)job.point=1;
 }else{
  size_t next=job.point;while(next+1<job.points.size()&&job.points[next+1].at<=elapsed)++next;
  if(next>job.point+1){job.skipped+=(unsigned)(next-job.point-1);if(job.strictFrames){JobFinish(job,"failed","timeline_states_missed");return;}}
  if(job.updates&&next==job.point){if(elapsed>=job.duration+job.endHold)JobFinish(job,"completed","");return;}
  job.point=next;auto& p=job.points[next];job.maxLate=(std::max)(job.maxLate,elapsed-p.at);buttons=p.buttons;mask=p.mask;for(int i=0;i<6;i++)values[i]=p.axis[i];
 }
 if(job.kind==InputJob::Timeline){
  bool movement=false;for(int i=2;i<6;i++)if(job.publishedAxes[i]!=values[i])movement=true;
  const bool press=(buttons&~job.publishedButtons)||(values[0]>0&&job.publishedAxes[0]==0)||(values[1]>0&&job.publishedAxes[1]==0);
  if(movement){job.movementKnown=true;job.movementAt=now;job.movementFrame=C.present_ours;job.movementNative=job.movementFrame>0;}
  JobTimelinePublish(job,buttons,mask,values,now);JobTimelineRecord(job,job.point,now,press);return;
 }
 JobPadPublish(buttons,mask,values,true,job.id);++job.updates;job.lastApply=now;job.lastFrame=C.present_ours;
}
void JobsTick(bool frame){std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_acProduct.load()){JobsCancelAll("anticheat_detected");return;}const ULONGLONG now=HousekeepingNow();for(auto& job:g_jobs)JobRun(*job,now,frame);JobPrune(now);}
void JobsFrame(){try{std::unique_lock<std::recursive_mutex> lock(g_inputMx,std::try_to_lock);if(!lock.owns_lock())return;if(g_acProduct.load()){JobsCancelAll("anticheat_detected");return;}const ULONGLONG now=HousekeepingNow();for(auto& job:g_jobs)if(job->frames||(job->presentTarget&&!job->qpcAnchor))JobRun(*job,now,true);}catch(...){try{JobsCancelAll("frame_checkpoint_exception");}catch(...){}}}
DWORD SequenceWorkerWait(){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);const auto now=vi_qpc::Now();std::uint64_t nearest=0,frequency=0;
 for(const auto& job:g_jobs)if((job->kind==InputJob::Sequence||job->kind==InputJob::KeyDrive)&&job->started&&!job->done&&job->qpcNext&&(!nearest||job->qpcNext<nearest)){nearest=job->qpcNext;frequency=job->qpcFrequency;}
 if(!nearest||!now||!frequency)return 5;if(nearest<=now)return 0;
 const double left=vi_qpc::DeltaMs(nearest,now,frequency);return left<=1?0:(DWORD)(std::min)(5.0,std::floor(left-1));
}
DWORD WINAPI InputJobWorker(void*){for(;;){try{JobsTick(false);}catch(...){try{JobsCancelAll("worker_exception");}catch(...){}}
 // The final <=1 ms uses a cancellable yield loop outside the input mutex.
 // Interrupt-clock bound prevents a stalled/modified QPC from spinning forever.
 const DWORD wait=SequenceWorkerWait();if(wait)WaitForSingleObject(g_jobWake,wait);else{const auto start=HousekeepingNow();while(SequenceWorkerWait()==0&&HousekeepingNow()-start<1){if(WaitForSingleObject(g_jobWake,0)==WAIT_OBJECT_0)break;YieldProcessor();}}
 }return 0;}
bool EnsureJobWorker(){if(g_jobWorker)return true;if(!g_jobWake)g_jobWake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!g_jobWake)return false;HANDLE thread=CreateThread(nullptr,0,InputJobWorker,nullptr,0,nullptr);if(!thread)return false;CloseHandle(thread);g_jobWorker=true;return true;}
std::string JobId(const InputJob& job){return std::to_string(g_pid)+":"+std::to_string(g_jobIdentity)+":"+std::to_string(job.id);}
std::string JobReplyLocked(const InputJob& job,bool accepted=false,bool details=true){JOut out;out.bol("ok",accepted||job.state=="completed");out.bol("accepted",accepted);out.str("job_id",JobId(job));out.str("job_state",job.state);out.str("job_reason",job.reason);out.bol("job_complete",job.done);out.bol("hold_ready",job.started&&!job.done&&(job.pad?job.updates>0:(job.kind==InputJob::Keys||job.kind==InputJob::KeyDrive)&&job.phase==1||job.kind==InputJob::Radar&&job.radarObservationCount>0));out.str("owner",job.owner);
 out.bol("capture_scope_checked",job.captureBound);JobReadFields(out,job);
 if(job.admissionDeadlineQpc){out.num("input_admission_version",1);out.str("admission_deadline_qpc_ticks",std::to_string(job.admissionDeadlineQpc));}
 if(job.kind!=InputJob::Sequence){const char* kinds[]={"keys","pulse","pad","ramp","timeline","placement","sequence","radar","key_drive"};out.str("job_kind",kinds[(unsigned)job.kind]);}
 if(accepted)out.raw("input_injected","null");else out.bol("input_injected",job.transitions>0||job.updates>0);out.num("transitions",job.transitions);out.num("pad_updates",job.updates);out.num("skipped_states",job.skipped);const bool latenessMeasured=job.kind==InputJob::Keys||job.kind==InputJob::Timeline||((job.kind==InputJob::Sequence||job.kind==InputJob::KeyDrive)&&job.qpcLast!=0);out.bol("max_lateness_measured",latenessMeasured);if(latenessMeasured)out.num("max_lateness_ms",(double)job.maxLate);else out.raw("max_lateness_ms","null");out.str("housekeeping_clock","QueryInterruptTime");out.num("requested_ms",(double)job.duration);out.num("elapsed_ms",(double)((job.done?job.finished:HousekeepingNow())-job.accepted));out.num("active_ms",job.started?(double)((job.done?job.finished:HousekeepingNow())-job.start):0);out.bol("frame_paced",job.frames);out.num("first_frame",job.firstFrame);out.num("last_frame",job.lastFrame);out.num("last_apply_ms",job.started?(double)(job.lastApply-job.start):0);
 if(job.kind==InputJob::Sequence){out.str("job_kind","sequence");out.num("sequence_steps",(double)job.sequence.size());out.num("sequence_step",(double)job.sequenceStep);if(job.sequenceStep<job.sequence.size())out.num("sequence_line",job.sequence[job.sequenceStep].line);
  out.num("sequence_timing_version",1);out.str("timing_clock","QueryPerformanceCounter");out.str("qpc_frequency",std::to_string(job.qpcFrequency));out.str("anchor_qpc_ticks",std::to_string(job.qpcAnchor));out.str("last_apply_qpc_ticks",std::to_string(job.qpcLast));out.str("last_deadline_qpc_ticks",std::to_string(job.qpcLastDeadline));
  out.str("anchor_source",job.presentTarget?(job.qpcAnchor?"native_present_hook":"pending_present_hook"):job.eventAnchor?"observed_capture":"native_start");out.str("native_start_qpc_ticks",std::to_string(job.qpcNativeStart));out.str("first_apply_qpc_ticks",std::to_string(job.qpcFirst));out.num("max_lateness_limit_ms",job.latenessLimit);
  out.num("sequence_frame_anchor_version",1);out.bol("sequence_waiting_for_present",job.presentTarget&&!job.qpcAnchor&&!job.done);if(job.presentTarget){out.str("present_anchor_target_index",std::to_string(job.presentTarget));out.str("anchor_present_hook_index",std::to_string(job.presentAnchor));out.num("present_anchor_timeout_ms",job.presentTimeout);out.str("present_anchor_scope","exact native OnFrame hook checkpoint, not successful Present return, display scanout or game-event onset; missed frame cancels without rebasing");}
  if(job.eventAnchor&&job.qpcNativeStart)out.num("observed_to_native_start_ms",vi_qpc::DeltaMs(job.qpcNativeStart,job.qpcAnchor,job.qpcFrequency));else out.raw("observed_to_native_start_ms","null");
  if(job.eventAnchor&&job.qpcFirst)out.num("observed_to_first_apply_ms",vi_qpc::DeltaMs(job.qpcFirst,job.qpcAnchor,job.qpcFrequency));else out.raw("observed_to_first_apply_ms","null");
  out.bol("deadline_anchored",true);out.bol("sequence_lateness_measured",job.qpcLast!=0);if(job.qpcLast)out.num("sequence_max_lateness_ms",vi_qpc::DeltaMs(job.qpcMaxLate,0,job.qpcFrequency));else out.raw("sequence_max_lateness_ms","null");
  out.num("sequence_guard_version",1);out.bol("observer_guarded",job.sequenceGuard.Enabled());out.num("observer_guard_ms",(double)job.sequenceGuard.interval);out.num("guard_observation_seq",(double)job.sequenceGuard.serial);out.str("observer_qpc_ticks",std::to_string(job.observerQpc));
  out.num("sequence_observer_reconcile_version",1);out.str("observer_guard_clock","QueryInterruptTime");const auto guardNow=HousekeepingNow();out.str("observer_guard_sample_ms",std::to_string(guardNow));out.num("observer_guard_remaining_ms",!job.done&&job.captureBound&&job.target==g_hwnd&&CaptureScopeCurrent(job.captureScope)&&WndBindingInputAllowed(job.target)&&!OwnerCursorActive()&&job.claimEpoch==g_jobClaimEpoch&&(!job.claimed||guardNow<g_jobClaimExpiry)&&job.sequenceGuard.Enabled()&&!job.sequenceGuard.Expired(guardNow)?(double)(job.sequenceGuard.deadline-guardNow):0);if(job.captureBound)out.raw("capture_scope",CaptureScopeJson(job.captureScope));if(job.done&&job.finishedCaptureScopeValid)out.raw("finished_capture_scope",CaptureScopeJson(job.finishedCaptureScope));
  out.num("sequence_edge_trace_version",1);out.num("sequence_edge_count",(double)job.sequenceEdges.edges.size());out.num("sequence_edges_dropped",(double)job.sequenceEdges.dropped);out.bol("sequence_edge_trace_complete",job.sequenceEdges.dropped==0);out.str("finished_qpc_ticks",std::to_string(job.qpcFinished));
  out.str("sequence_edge_scope","QPC sampled after each scheduled KeyEvent return; completed_present_index is the latest confirmed successful DXGI/D3D9 return for that live window at or before that QPC, null if unavailable; neither application consumption nor display scanout is measured; cleanup releases are outside this scheduled-edge trace");
  if(details){std::string edges="[";for(const auto& e:job.sequenceEdges.edges){if(edges.size()>1)edges+=",";JOut edge;edge.num("step_index",e.step);edge.num("script_line",e.line);edge.num("vk",e.vk);edge.bol("down",e.down);edge.str("qpc_ticks",std::to_string(e.qpc));if(e.present)edge.str("completed_present_index",std::to_string(e.present));else edge.raw("completed_present_index","null");edges+=edge.done();}out.raw("sequence_edges",edges+"]");}
  if(job.qpcAnchor&&job.done&&job.state=="completed")out.num("cumulative_drift_ms",vi_qpc::DeltaMs(job.qpcFinished,job.qpcAnchor+vi_qpc::Milliseconds(job.duration,job.qpcFrequency),job.qpcFrequency));else if(job.qpcLast)out.num("cumulative_drift_ms",vi_qpc::DeltaMs(job.qpcLast,job.qpcLastDeadline,job.qpcFrequency));else out.raw("cumulative_drift_ms","null");
 }
 if(job.kind==InputJob::Placement){static const char* phases[]={"prepare","hover_poll","press_poll","move","step_poll","dwell","release_poll","complete"};out.str("placement_phase",phases[(std::min)(7,job.phase)]);out.num("steps",job.pointerSteps);out.num("steps_consumed",job.consumed);out.num("position_generation",(double)job.motionToken);out.bol("position_poll_confirmed",job.positionSeen);out.str("position_poll_channel",job.pointerReader==2?"GetPhysicalCursorPos":job.pointerReader==1?"GetCursorPos":"none");out.bol("di_poll_confirmed",job.downSeen);out.bol("di_release_confirmed",job.upSeen);out.bol("di_press_release_confirmed",job.downSeen&&job.upSeen);out.bol("final_position_seen",job.finalSeen);out.bol("final_position_before_release",job.finalBeforeRelease);out.num("dwell_ms",(double)job.dwell);out.num("final_seen_ms",job.finalAt?(double)(job.finalAt-job.start):0);out.num("release_ms",job.releasedAt?(double)(job.releasedAt-job.start):0);out.num("button_hold_ms",job.pressedAt?(double)((job.releasedAt?job.releasedAt:job.done?job.finished:HousekeepingNow())-job.pressedAt):0);out.bol("button_released",!g_vk[job.keys[0]]);out.num("x",job.expected.x);out.num("y",job.expected.y);EdgeFields(out,&job.pointerPath);out.bol("path_edge_contact",job.edgeContact);}
 if(job.kind==InputJob::Timeline)JobTimelineFields(out,job,details&&!accepted);
 if(job.kind==InputJob::Radar)RadarJobFields(out,job);
 if(job.kind==InputJob::KeyDrive)JobKeyDriveFields(out,job);
 if(job.pad){out.num("buttons",job.buttons);out.num("axes_set_mask",job.mask);out.bol("clamped",job.clamped);out.bol("hold",false);if(job.kind==InputJob::Pad){JOut axes;const char* names[]={"lt","rt","lx","ly","rx","ry"};for(unsigned i=0;i<6;++i)axes.num(names[i],job.to[i]);out.raw("requested_axes",axes.done());out.num("pad_motion_job_version",1);}}if(job.done&&job.state!="completed")out.str("error",job.reason);out.str("delivery","job transitions describe virtual input; application response is not implied; no uncertain input is retried");return out.done();}
std::string JobReply(const std::shared_ptr<InputJob>& job,bool accepted=false){std::lock_guard<std::recursive_mutex> lock(g_inputMx);JobReadsSample(*job);return JobReplyLocked(*job,accepted);}
std::string ScheduleInputJob(const std::shared_ptr<InputJob>& job,const JObj& request){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 {std::string error;if(!ReadInputDeadline(request,job->admissionDeadlineQpc,error)){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error",error);return out.done();}}
 if(JHas(request,"expected_capture_scope")){const auto scope=JStr(request,"expected_capture_scope");
  if(request.find("expected_capture_scope")->second.t!=1||scope.size()<2||scope.size()>2048||scope.find('\0')!=std::string::npos||!CaptureScopeTake(job->captureScope)||scope!=CaptureScopeJson(job->captureScope)){
   JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error","input observation scope changed or is unrecognized; no input was injected");return out.done();}
  job->captureBound=true;
 }
 if(job->kind==InputJob::Sequence&&job->eventAnchor&&!vi_sequence_anchor::Current(job->qpcAnchor,job->qpcFrequency,vi_qpc::Now(),vi_qpc::Frequency())){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error","sequence event QPC anchor is stale, future or mismatched; no input was injected");return out.done();}
 if(!RealInterruptClock())return ErrJ("unscaled interrupt clock unavailable; no input was injected");const auto now=HousekeepingNow();JobPrune(now);if(g_jobRetargetDepth)return ErrJ("target adoption in progress; no input was injected");
 for(const char* flag:{"async","replace"})if(JHas(request,flag)&&request.find(flag)->second.t!=3)return ErrJ(std::string(flag)+" must be a JSON boolean");
 job->async=JBool(request,"async",false);const bool replace=JBool(request,"replace",false);size_t live=0;
 if(job->async&&!replace)for(int resource:job->resources)if(!g_resourceOwner[resource]&&(resource==256?(g_padActive&&(g_padSetMask||g_padButtons)):g_vk[resource]!=0))return ErrJ("manual virtual input already holds this resource; no input was injected (use replace:true for explicit takeover)");
 std::vector<std::shared_ptr<InputJob>> replaced;
 for(auto& old:g_jobs)if(!old->done){++live;if(JobConflicts(*job,*old)){if(replace)replaced.push_back(old);else if(job->async)return ErrJ("input resource busy; no input was injected (use replace:true to cancel the earlier job)");}}
 if(live-replaced.size()>=32)return ErrJ("input job capacity32 reached; no input was injected");
 if(g_admittedJobEpoch!=g_jobClaimEpoch)return ErrJ("claim changed before scheduling; no input was injected");
 if(job->captureBound&&!CaptureScopeCurrent(job->captureScope)){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error","input capture scope changed before scheduling; no input was injected");return out.done();}
 if(!EnsureJobWorker())return ErrJ("input worker unavailable; no input was injected");
 job->state.reserve(32);job->reason.reserve(64);
 job->completion=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!job->completion)return ErrJ("job completion unavailable; no input was injected");
 for(auto& old:replaced)JobFinish(*old,"cancelled","explicit_replacement");
 if(!g_jobIdentity)g_jobIdentity=now;
 job->target=g_hwnd;
 if(job->kind==InputJob::Sequence&&job->sequenceGuard.interval)job->sequenceGuard.Start(job->sequenceGuard.interval,now);
 job->id=++g_nextJob;job->accepted=now;job->owner=JStr(request,"owner");job->claimEpoch=g_admittedJobEpoch;job->claimed=g_admittedJobClaimed;if(job->kind==InputJob::Placement)CursorAdmitCommand();g_jobs.push_back(job);SetEvent(g_jobWake);
 if(!job->async)g_waitJob=job;return JobReplyLocked(*job,true);
}
bool JobNumber(const JObj& j,const char* key,double fallback,double low,double high,double& value){auto i=j.find(key);if(i==j.end())value=fallback;else{if(i->second.t!=2)return false;value=i->second.n;}return std::isfinite(value)&&value>=low&&value<=high;}
std::string ScheduleKeyInput(const JObj& j,bool pulse){
 auto job=std::make_shared<InputJob>();job->kind=pulse?InputJob::Pulse:InputJob::Keys;job->keys=ParseCombo(JStr(j,"key"));if(job->keys.empty()||job->keys.size()>16)return ErrJ("unknown or oversized key chord");
 for(int key:job->keys){int resource=JobResource(key);if(std::find(job->resources.begin(),job->resources.end(),resource)==job->resources.end())job->resources.push_back(resource);}
 double ms,count,gap;if(!JobNumber(j,"ms",pulse?1000:JStr(j,"cmd")=="hold"?1000:80,0,120000,ms)||!JobNumber(j,"count",1,1,1000,count)||std::floor(count)!=count||!JobNumber(j,"gap",80,0,60000,gap))return ErrJ("key duration/count/gap invalid; no input was injected");
 job->ms=(ULONGLONG)std::ceil(ms);job->gap=(ULONGLONG)std::ceil(gap);job->count=(int)count;job->duration=job->ms*job->count+job->gap*(job->count-1)+30*(job->keys.size()-1)*job->count;
 if(pulse){double period,duty;if(!JobNumber(j,"period_ms",100,20,10000,period)||!JobNumber(j,"duty",50,0,100,duty))return ErrJ("pulse period must be20..10000ms and duty0..100");job->period=(ULONGLONG)std::ceil(period);job->onMs=(ULONGLONG)std::llround(job->period*duty/100);job->duration=job->ms;if((job->onMs&&job->onMs<10)||(job->onMs<job->period&&job->period-job->onMs<10)||job->duration/job->period>2048)return ErrJ("pulse intervals must be at least10ms and at most2048 periods");}
 if(job->duration>120000||job->count*job->keys.size()*2>4096)return ErrJ("job exceeds120000ms or4096 transitions");return ScheduleInputJob(job,j);
}
std::shared_ptr<InputJob> FindJob(const std::string& id){for(auto& job:g_jobs)if(JobId(*job)==id)return job;return {};}
std::string JobCommand(const std::string& cmd,const JObj& j){std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(cmd=="jobs"){JOut out;out.bol("ok",true);std::string list="[";for(auto& job:g_jobs){if(list.size()>1)list+=',';list+=JobReplyLocked(*job,false,false);}out.raw("jobs",list+"]");return out.done();}
 auto job=FindJob(JStr(j,"id"));if(!job)return ErrJ("unknown job_id (jobs are retained for at most60seconds/128 terminal records)");if(cmd=="job-cancel"){if(job->owner!=JStr(j,"owner")&&!JBool(j,"force_claim",false))return ErrJ("job owner mismatch; no input was changed");JobFinish(*job,"cancelled","explicit_cancel");JOut out;out.bol("ok",true);out.raw("job",JobReplyLocked(*job));return out.done();}JOut out;out.bol("ok",true);out.raw("job",JobReplyLocked(*job));return out.done();}
std::string SchedulePadInput(const JObj& j,WORD buttons,int mask,const int* values,double ms,bool clamped){auto job=std::make_shared<InputJob>();job->kind=InputJob::Pad;job->pad=true;job->resources={256};job->buttons=buttons;job->mask=mask;job->duration=(ULONGLONG)std::ceil(ms);job->clamped=clamped;for(int i=0;i<6;i++)job->to[i]=values[i];return ScheduleInputJob(job,j);}
const char* kJobAxes[6]={"lt","rt","lx","ly","rx","ry"};
std::string SchedulePlacement(const JObj& request){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 if(!g_hwnd)return ErrJ("no window");
 if(JStr(request,"space")=="capture"&&!JBool(request,"norm",false)&&!CaptureMapCurrent())return CaptureMapRefusal();
 for(const char* name:{"x","y","x2","y2"}){double value;if(!JHas(request,name)||!JobNumber(request,name,0,-1000000,1000000,value))return ErrJ("placement requires finite numeric x,y,x2,y2 within +/-1000000; no input was injected");}
 double steps,ms,dwell,cap,hold;
 if(!JobNumber(request,"steps",15,1,128,steps)||steps!=std::floor(steps)||!JobNumber(request,"ms",300,0,60000,ms)||!JobNumber(request,"dwell_ms",200,0,60000,dwell)||!JobNumber(request,"poll_timeout_ms",1000,1,5000,cap)||!JobNumber(request,"hold_ms",60,0,60000,hold))return ErrJ("placement steps/ms/dwell/poll/hold timing invalid; no input was injected");
 const double stepMs=std::ceil(ms/steps);if((steps+4)*std::ceil(cap)+stepMs*steps+std::ceil(dwell)+std::ceil(hold)>118500)return ErrJ("placement worst-case phase budget exceeds118500ms; no input was injected");
 const std::string button=JStr(request,"button","left");int index=button=="left"?0:button=="right"?1:button=="middle"?2:button=="x1"?3:button=="x2"?4:-1;
 if(index<0)return ErrJ("unknown placement mouse button; no input was injected");
 const int key=kDiMouseVk[index];if(g_vk[key]&&!g_resourceOwner[JobResource(key)])return ErrJ("placement button is manually held; release it before starting an owned placement; no input was injected");
 auto job=std::make_shared<InputJob>();job->captureBound=JStr(request,"space")=="capture"&&!JBool(request,"norm",false);if(job->captureBound)job->captureScope=g_savedCaptureMap.scope;job->kind=InputJob::Placement;job->keys={key};job->resources={0,JobResource(key)};job->buttonIndex=index;
 auto resolve=[&](const char* xName,const char* yName,POINT& point){double x=JNum(request,xName,0),y=JNum(request,yName,0);RECT r{};if(!GetClientRect(g_hwnd,&r))return false;
  if(JBool(request,"norm",false)){x*=r.right;y*=r.bottom;}else if(JStr(request,"space")=="capture"){x=g_savedCaptureMap.ox+x*g_savedCaptureMap.cw/g_savedCaptureMap.w;y=g_savedCaptureMap.oy+y*g_savedCaptureMap.ch/g_savedCaptureMap.h;}
  if(!std::isfinite(x)||!std::isfinite(y)||x<-2147483648.0||x>2147483647.0||y<-2147483648.0||y>2147483647.0)return false;point={(LONG)std::llround(x),(LONG)std::llround(y)};return true;};
 if(!resolve("x","y",job->pointerStart)||!resolve("x2","y2",job->pointerEnd))return ErrJ("resolved placement coordinate exceeds signed32-bit range; no input was injected");
 ClampToClient(job->pointerStart.x,job->pointerStart.y);ClampToClient(job->pointerEnd.x,job->pointerEnd.y);
 if(!job->pointerPath.Init(job->pointerEnd.x,job->pointerEnd.y))return ErrJ("owned placement target unavailable; no input was injected");
 if(job->captureBound&&!CaptureScopeCurrent(job->captureScope))return CaptureMapRefusal();
 job->pointerSteps=(int)steps;job->dwell=(ULONGLONG)std::ceil(dwell);job->pollCap=(ULONGLONG)std::ceil(cap);job->ms=(ULONGLONG)std::ceil(hold);job->stepMs=(ULONGLONG)stepMs;job->duration=(ULONGLONG)std::ceil(ms)+job->dwell+job->ms;
 return ScheduleInputJob(job,request);
}
bool TimelineNumber(const std::string& text,size_t& at,double& value){size_t start=at;if(at<text.size()&&text[at]=='-')++at;if(at>=text.size())return false;if(text[at]=='0')++at;else{if(text[at]<'1'||text[at]>'9')return false;while(at<text.size()&&isdigit((unsigned char)text[at]))++at;}if(at<text.size()&&text[at]=='.'){++at;size_t digits=at;while(at<text.size()&&isdigit((unsigned char)text[at]))++at;if(at==digits)return false;}if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))++at;size_t digits=at;while(at<text.size()&&isdigit((unsigned char)text[at]))++at;if(at==digits)return false;}value=strtod(text.substr(start,at-start).c_str(),nullptr);return std::isfinite(value);}
bool TimelineString(const std::string& text,size_t& at,std::string& value){size_t end=at;if(end>=text.size()||text[end++]!='"')return false;bool closed=false;while(end<text.size()){unsigned char ch=text[end++];if(ch=='"'){closed=true;break;}if(ch<0x20)return false;if(ch=='\\'){if(end>=text.size())return false;char escape=text[end++];if(escape=='u'){if(end+4>text.size())return false;for(int n=0;n<4;n++)if(!isxdigit((unsigned char)text[end++]))return false;}else if(escape!='"'&&escape!='\\'&&escape!='/'&&escape!='b'&&escape!='f'&&escape!='n'&&escape!='r'&&escape!='t')return false;}}return closed&&ParseStr(text,at,value);}
bool ParsePadTimeline(const std::string& text,std::vector<InputJob::PadPoint>& points,std::string& error){
 auto fail=[&](const char* why){error=why;return false;};if(text.size()>65536)return fail("timeline exceeds64KiB");size_t at=0;SkipWs(text,at);if(at>=text.size()||text[at++]!='[')return fail("timeline must be a JSON array");
 for(;;){SkipWs(text,at);if(at>=text.size()||text[at++]!='{')return fail("timeline needs1..256 state objects");InputJob::PadPoint point;unsigned triggerAliases=0;std::vector<std::string> fields;bool hasAt=false;
  for(;;){SkipWs(text,at);std::string key;if(!TimelineString(text,at,key))return fail("timeline field name invalid");if(std::find(fields.begin(),fields.end(),key)!=fields.end())return fail("duplicate timeline field");fields.push_back(key);SkipWs(text,at);if(at>=text.size()||text[at++]!=':')return fail("timeline colon missing");SkipWs(text,at);
   if(key=="phase"){if(!TimelineString(text,at,point.name)||point.name.empty()||point.name.size()>32)return fail("timeline phase must be1..32 ASCII letters/digits/underscore/dot/hyphen");for(unsigned char ch:point.name)if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='.'||ch=='-'))return fail("timeline phase contains invalid characters");}
   else if(key=="buttons"){std::string names,bad;if(!TimelineString(text,at,names))return fail("timeline buttons must be a string");point.buttons=PadButtonsFromNames(names,&bad,&triggerAliases);if(!bad.empty())return fail(bad.c_str());}
   else{double value;if(!TimelineNumber(text,at,value)||std::floor(value)!=value)return fail("timeline values must be finite integers");if(key=="at_ms"){if(value<0||value>120000)return fail("timeline at_ms out of range");point.at=(ULONGLONG)value;hasAt=true;}
    else{int axis=-1;for(int i=0;i<6;i++)if(key==kJobAxes[i])axis=i;if(axis<0)return fail("unknown timeline field");if(value<(axis<2?0:-32768)||value>(axis<2?255:32767))return fail("timeline axis out of range");point.axis[axis]=(int)value;point.mask|=1<<axis;}}
   SkipWs(text,at);if(at>=text.size())return fail("unfinished timeline state");if(text[at]=='}'){++at;break;}if(text[at++]!=',')return fail("timeline state delimiter invalid");
  }
  if(!vi_pad_buttons::ApplyTriggerAliases(triggerAliases,point.mask,point.axis,error))return false;
  if(!hasAt||(points.empty()?point.at!=0:point.at<=points.back().at))return fail("timeline begins at0 and at_ms increases strictly");points.push_back(point);if(points.size()>256)return fail("timeline exceeds256 states");SkipWs(text,at);if(at>=text.size())return fail("unfinished timeline array");if(text[at]==']'){++at;SkipWs(text,at);return at==text.size()?true:fail("trailing timeline data");}if(text[at++]!=',')return fail("timeline delimiter invalid");
 }
}
std::string SchedulePadSteering(const JObj& j,bool timeline){auto job=std::make_shared<InputJob>();job->pad=true;job->resources={256};job->kind=timeline?InputJob::Timeline:InputJob::Ramp;
 const std::string pacing=JStr(j,"pacing","frames");if(pacing!="frames"&&pacing!="time")return ErrJ("pacing must be frames or time");job->frames=pacing=="frames";if(job->frames&&C.present_ours==0)return ErrJ("no native Present observed; frame-paced input was not started (explicit pacing:time is available)");
 if(JHas(j,"strict_frames")&&j.find("strict_frames")->second.t!=3)return ErrJ("strict_frames must be a JSON boolean");job->strictFrames=JBool(j,"strict_frames",false);if(job->strictFrames&&!job->frames)return ErrJ("strict_frames requires frame pacing");
 double ms,endHold;if(!JobNumber(j,"end_hold_ms",0,0,10000,endHold))return ErrJ("end_hold_ms must be0..10000");job->endHold=(ULONGLONG)std::ceil(endHold);
 if(timeline){std::string error;if(!ParsePadTimeline(JStr(j,"states_json"),job->points,error))return ErrJ(error+"; no input was injected");if(!JobNumber(j,"ms",(double)job->points.back().at,(double)job->points.back().at,120000,ms))return ErrJ("timeline ms must cover the last state and be at most120000");}
 else{const std::string name=JStr(j,"axis");int axis=-1;for(int i=0;i<6;i++)if(name==kJobAxes[i])axis=i;if(axis<0)return ErrJ("pad-ramp axis must be lt rt lx ly rx or ry");double from,to;if(!JobNumber(j,"from",0,axis<2?0:-32768,axis<2?255:32767,from)||!JobNumber(j,"to",0,axis<2?0:-32768,axis<2?255:32767,to)||!JHas(j,"from")||!JHas(j,"to")||!JobNumber(j,"ms",1000,1,120000,ms))return ErrJ("ramp from/to/ms invalid; no input was injected");
  for(int i=0;i<6;i++)if(JHas(j,kJobAxes[i])){double value;if(!JobNumber(j,kJobAxes[i],0,i<2?0:-32768,i<2?255:32767,value))return ErrJ("constant ramp axis invalid");job->mask|=1<<i;job->from[i]=job->to[i]=(int)std::llround(value);}job->mask|=1<<axis;job->from[axis]=(int)std::llround(from);job->to[axis]=(int)std::llround(to);if(JHas(j,"buttons")&&j.find("buttons")->second.t!=1)return ErrJ("ramp buttons must be a string; no input was injected");std::string bad;unsigned aliases=0;job->buttons=PadButtonsFromNames(JStr(j,"buttons"),&bad,&aliases);if(!bad.empty())return ErrJ(bad+"; no input was injected");if(!vi_pad_buttons::ApplyTriggerAliases(aliases,job->mask,job->from,bad))return ErrJ(bad+"; no input was injected");for(int i=0;i<2;i++)if(aliases&(1u<<i))job->to[i]=255;}
 if(JHas(j,"stagger_ms")){double stagger;if(!timeline||!JobNumber(j,"stagger_ms",0,0,1000,stagger)||stagger!=std::floor(stagger))return ErrJ("stagger_ms requires pad-timeline and an integer0..1000; no input was injected");job->staggerEnabled=true;job->staggerMs=(ULONGLONG)stagger;}
 job->duration=(ULONGLONG)std::ceil(ms);if(job->duration+job->endHold+job->staggerMs*job->points.size()>118500)return ErrJ("steering duration plus end hold exceeds118500ms (1500ms cleanup reserve)");return ScheduleInputJob(job,j);
}

#include "vi_pad_stagger.inl"
#include "vi_key_sequence_jobs.inl"
#include "vi_sequence_guard_jobs.inl"
