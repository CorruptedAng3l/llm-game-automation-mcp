// Included after vi_jobs.inl. Lock order is input -> pad; no native XInput
// call, wait, resource takeover, job cancellation or input release occurs here.
std::string PadReadinessStatusJson(){
 std::lock_guard<std::mutex> lock(g_padStateMx);JOut out;
 out.bol("active",g_padActive);out.str("publication",std::to_string(g_padPublication));
 out.str("virtual_returns",std::to_string(g_padReturn.count));out.str("standard_returns",std::to_string(g_padReturn.standard));out.str("extended_returns",std::to_string(g_padReturn.extended));out.str("native_calls_bypassed",std::to_string(g_padReturn.fast));
 out.str("last_return_publication",std::to_string(g_padReturn.publication));
 if(g_padReturn.count)out.num("last_return_age_ms",(double)(GetTickCount64()-g_padReturn.at));else out.raw("last_return_age_ms","null");
 if(g_padReturn.count)out.bol("last_return_neutral",g_padReturn.neutral);else out.raw("last_return_neutral","null");
 out.bol("capabilities_virtualized",false);out.bol("device_arrival_announced",false);out.raw("application_ready","null");return out.done();
}
std::string PadArmCommand(const JObj& request){
 for(const auto& field:request)if(field.first!="cmd"&&field.first!="wait_ms"&&field.first!="owner"&&field.first!="force_claim")return ErrJ("unknown pad-arm field '"+field.first+"'; virtual pad was not changed");
 double wait=0;if(!JobNumber(request,"wait_ms",500,0,2000,wait)||wait!=std::floor(wait))return ErrJ("pad-arm wait_ms must be an integer0..2000; virtual pad was not changed");
 unsigned long long publication=0,before=0;bool changed=false;
 {
  std::lock_guard<std::recursive_mutex> input(g_inputMx);
  if(g_jobRetargetDepth||g_wndConfigCommitDepth)return ErrJ("target adoption in progress; virtual pad was not changed");
  if(g_admittedJobEpoch!=g_jobClaimEpoch)return ErrJ("claim changed before pad-arm; virtual pad was not changed");
  for(const auto& job:g_jobs)if(!job->done&&job->pad)return ErrJ("pad-arm refused: a queued/running pad job exists; existing virtual state was preserved");
  if(g_resourceOwner[256])return ErrJ("pad-arm refused: pad resource is owned; existing virtual state was preserved");
  std::lock_guard<std::mutex> pad(g_padStateMx);
  if(g_padButtons||g_padSetMask)return ErrJ("pad-arm refused: existing virtual pad state is held; existing virtual state was preserved");
  before=g_padReturn.count;if(!g_padActive){g_padActive=true;g_padPacket++;PadPublicationChangedLocked();changed=true;}
  publication=g_padPublication;
 }
 const ULONGLONG began=GetTickCount64();bool observed=false,neutral=false,superseded=false;
 for(;;){
  {std::lock_guard<std::mutex> pad(g_padStateMx);
   superseded=!g_padActive||g_padPublication!=publication;
   observed=!superseded&&g_padReturn.count>before&&g_padReturn.publication==publication;
   if(observed)neutral=g_padReturn.neutral;
  }
  if(observed||superseded||GetTickCount64()-began>=(ULONGLONG)wait)break;
  const ULONGLONG elapsed=GetTickCount64()-began;if(elapsed<(ULONGLONG)wait)Sleep((DWORD)((std::min)((ULONGLONG)5,(ULONGLONG)wait-elapsed)));
 }
 JOut out;out.bol("ok",true);out.num("pad_readiness_version",1);out.bol("arm_accepted",true);out.bol("pad_armed",!superseded);out.bol("state_changed",changed);
 out.str("arm_publication",std::to_string(publication));out.bol("poll_observed",observed);out.bol("superseded",superseded);
 if(observed)out.bol("neutral_return_observed",neutral);else out.raw("neutral_return_observed","null");
 out.num("waited_ms",(double)(GetTickCount64()-began));out.bol("buttons_or_axes_published",false);out.bol("device_arrival_announced",false);out.raw("application_ready","null");
 out.str("scope","neutral virtual contribution to XInputGetState/Ex user0; foreground/opt-out physical merging unchanged; no glyph/hotplug guarantee");return out.done();
}
