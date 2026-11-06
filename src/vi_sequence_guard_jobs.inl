// Exact-job observation does not acquire resources, replay input, or renew a claim.
// The caller/worker holds g_inputMx while the existing generation-aware finish
// path performs any cleanup. Expired observers can never revive a running job.
std::string SequenceObserve(const JObj& request){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);const auto job=FindJob(JStr(request,"id"));
 if(!job||job->kind!=InputJob::Sequence||!job->sequenceGuard.Enabled())return ErrJ("unknown or unguarded sequence job; no input was changed");
 if(job->owner.empty()||job->owner!=JStr(request,"owner"))return ErrJ("sequence observer owner mismatch; no input was changed");
 double serial;if(!JobNumber(request,"observation_seq",0,1,1000000000,serial)||serial!=std::floor(serial)||!JHas(request,"abort")||request.find("abort")->second.t!=3)return ErrJ("sequence observation needs integer serial and boolean abort; no input was changed");
 const bool abort=JBool(request,"abort",false);const auto reason=JStr(request,"reason");
 if(abort&&reason!="frame_change"&&reason!="region_change"&&reason!="text"&&reason!="image"&&reason!="colour"&&reason!="progress_stalled"&&reason!="observer_error")return ErrJ("unknown sequence abort reason; no input was changed");
 bool renewed=false;
 if(!job->done){const auto now=HousekeepingNow();
  if(job->claimEpoch!=g_jobClaimEpoch||(job->claimed&&now>=g_jobClaimExpiry))JobFinish(*job,"cancelled","claim_expired_or_changed");
  else if(job->captureBound&&!CaptureScopeCurrent(job->captureScope))JobFinish(*job,"cancelled","sequence_capture_scope_changed");
  else {const auto outcome=job->sequenceGuard.Refresh(now,(std::uint64_t)serial);
   if(outcome==vi_sequence_guard::Verdict::Expired)JobFinish(*job,"cancelled","sequence_observer_expired");
   else if(outcome!=vi_sequence_guard::Verdict::Renewed)return ErrJ("sequence observation serial is stale or out of order; guard was not renewed");
   else {job->observerQpc=vi_qpc::Now();renewed=true;if(abort){const auto why="sequence_abort_"+reason;JobFinish(*job,"cancelled",why.c_str());}}
  }
 }
 JOut out;out.bol("ok",true);out.bol("guard_ack",true);out.bol("guard_renewed",renewed&&!job->done);out.num("observation_seq",serial);out.raw("job",JobReplyLocked(*job));return out.done();
}
