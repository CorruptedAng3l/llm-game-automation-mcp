// Called only under g_inputMx. Publications remain atomic in g_padStateMx.
// The frame counter witnesses target Present checkpoints, not game acceptance.
void JobTimelinePublish(InputJob& job,WORD buttons,int mask,const int* axes,ULONGLONG now){
 JobPadPublish(buttons,mask,axes,true,job.id);job.publishedButtons=buttons;job.publishedMask=mask;
 for(int i=0;i<6;i++)job.publishedAxes[i]=axes[i];job.padSnapshot=true;
 ++job.updates;job.lastApply=now;job.lastFrame=C.present_ours;
}
void JobTimelineRecord(InputJob& job,size_t index,ULONGLONG now,bool press){
 auto& point=job.points[index];point.published=true;point.publishedAt=now-job.start;point.publishedFrame=C.present_ours;
 point.pressEdge=press;if(press&&job.movementKnown){point.axesAt=job.movementAt-job.start;point.axesFrame=job.movementFrame;point.separated=job.movementNative&&C.present_ours>job.movementFrame?1:-1;}
 job.point=index;job.timelineNext=index+1;
}
void JobRunStaggeredTimeline(InputJob& job,ULONGLONG now){
 if(job.frames&&job.updates&&job.lastFrame==C.present_ours)return;
 if(!job.padSnapshot){std::lock_guard<std::mutex> lock(g_padStateMx);job.publishedButtons=g_padButtons;job.publishedMask=g_padSetMask;int values[6]={g_padLT,g_padRT,g_padLX,g_padLY,g_padRX,g_padRY};for(int i=0;i<6;i++)job.publishedAxes[i]=values[i];job.padSnapshot=true;}
 const ULONGLONG elapsed=now-job.start;
 if(job.guardPending){
  // A renderer that appears after time-only preparation needs two checkpoints:
  // one witnessing the already-published axes and a later one for the press.
  const long frame=C.present_ours;
  if(frame>0&&!job.movementNative){job.movementNative=true;job.movementFrame=frame;}
  if(now-job.movementAt<job.staggerMs||(job.movementNative&&frame<=job.movementFrame))return;
  auto& point=job.points[job.guardPoint];JobTimelinePublish(job,point.buttons,point.mask,point.axis,now);
  JobTimelineRecord(job,job.guardPoint,now,true);job.timelineShift+=now-job.guardAt;job.guardPending=false;return;
 }
 const ULONGLONG logical=elapsed>=job.timelineShift?elapsed-job.timelineShift:0;
 if(job.timelineNext>=job.points.size()){if(logical>=job.duration+job.endHold)JobFinish(job,"completed","");return;}
 size_t index=job.timelineNext;if(job.points[index].at>logical)return;
 while(index+1<job.points.size()&&job.points[index+1].at<=logical)++index;
 if(index>job.timelineNext){job.skipped+=(unsigned)(index-job.timelineNext);if(job.strictFrames){JobFinish(job,"failed","timeline_states_missed");return;}}
 auto& point=job.points[index];job.maxLate=(std::max)(job.maxLate,logical-point.at);
 bool movement=false;for(int i=2;i<6;i++)if(job.publishedAxes[i]!=point.axis[i])movement=true;
 const bool press=(point.buttons&~job.publishedButtons)||(point.axis[0]>0&&job.publishedAxes[0]==0)||(point.axis[1]>0&&job.publishedAxes[1]==0);
 if(movement){job.movementKnown=true;job.movementAt=now;job.movementFrame=C.present_ours;job.movementNative=job.movementFrame>0;}
 if(press&&job.movementKnown&&!job.movementNative&&C.present_ours>0){job.movementNative=true;job.movementFrame=C.present_ours;}
 const bool delay=press&&job.movementKnown&&(movement||now-job.movementAt<job.staggerMs||(job.movementNative&&C.present_ours<=job.movementFrame));
 if(delay){
  int prepared[6];for(int i=0;i<6;i++)prepared[i]=point.axis[i];
  for(int i=0;i<2;i++)if(point.axis[i]>0&&job.publishedAxes[i]==0)prepared[i]=0;
  // Requested releases happen now; only newly rising edges are withheld.
  JobTimelinePublish(job,(WORD)(point.buttons&job.publishedButtons),point.mask,prepared,now);
  point.prepared=true;point.preparedAt=now-job.start;point.preparedFrame=C.present_ours;
  job.guardPending=true;job.guardPoint=index;job.guardAt=now;return;
 }
 JobTimelinePublish(job,point.buttons,point.mask,point.axis,now);JobTimelineRecord(job,index,now,press);
}
void JobTimelineFields(JOut& out,const InputJob& job,bool details){
 out.num("pad_timeline_version",2);out.bol("stagger_enabled",job.staggerEnabled);out.num("stagger_ms",(double)job.staggerMs);out.bol("stagger_waiting",job.guardPending&&!job.done);out.num("timeline_shift_ms",(double)job.timelineShift);out.bol("cleanup_neutral_published",job.cleanupPublished);
 out.str("phase_evidence","published virtual pad states and observed target Present checkpoints; application consumption or menu selection is not implied");
 if(job.padSnapshot){JOut state;state.num("buttons",job.publishedButtons);state.num("axes_set_mask",job.publishedMask);static const char* names[]={"lt","rt","lx","ly","rx","ry"};for(int i=0;i<6;i++)state.num(names[i],job.publishedAxes[i]);out.raw("last_published_pad",state.done());}else out.raw("last_published_pad","null");
 if(details){std::string phases="[";for(size_t i=0;i<job.points.size();i++){const auto& point=job.points[i];JOut phase;phase.num("index",(double)i);phase.str("phase",point.name);phase.num("scheduled_at_ms",(double)point.at);phase.bol("published",point.published);phase.bol("axes_prepared",point.prepared);phase.bol("press_edge",point.pressEdge);
   if(point.published){phase.num("published_at_ms",(double)point.publishedAt);phase.num("published_present",point.publishedFrame);}else{phase.raw("published_at_ms","null");phase.raw("published_present","null");}
   if(point.prepared){phase.num("axes_prepared_at_ms",(double)point.preparedAt);phase.num("axes_prepared_present",point.preparedFrame);}else{phase.raw("axes_prepared_at_ms","null");phase.raw("axes_prepared_present","null");}
   if(point.separated==1){phase.bol("press_after_axis_present",true);phase.num("axis_change_present",point.axesFrame);}else{phase.raw("press_after_axis_present","null");phase.raw("axis_change_present","null");}
   if(i)phases+=',';phases+=phase.done();}out.raw("phase_publications",phases+"]");}
}
