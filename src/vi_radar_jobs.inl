// Native worker publication only. Capturing pixels never runs under g_inputMx.
void JobRunRadar(InputJob& job,ULONGLONG now){
 if(now-job.start>=job.duration){JobFinish(job,"completed","navigation_timeout");return;}
 if(OwnerCursorActive()){JobFinish(job,"cancelled","user_controls_target");return;}
 if(now<job.radarLeaseAt||now-job.radarLeaseAt>=job.radarLeaseMs){JobFinish(job,"cancelled","radar_observation_expired");return;}
 if(job.transitions>=4084){JobFinish(job,"cancelled","radar_transition_cap");return;}
 const auto& decision=job.radarState.latest;
 bool wanted[256]{};
 if(!decision.done){if(decision.forward)for(int key:job.radarForward)wanted[key]=true;
  if(vi_radar::PulseDown(decision.steer,job.radarConfig.periodMs,now-job.start))wanted[decision.steer<0?job.radarLeft:job.radarRight]=true;}
 std::vector<int> desired;for(int key:job.keys)if(wanted[key])desired.push_back(key);
 JobPublishKeyDriveState(job,desired,vi_qpc::Now());
}
void RadarJobFields(JOut& out,const InputJob& job){
 const auto& d=job.radarState.latest;JOut radar;
 radar.num("version",1);radar.str("request_id",job.radarRequestId);radar.str("mode",job.radarConfig.turnOnly?"turn_reference":"steer_to_marker");radar.num("observations",job.radarObservationCount);
 radar.bol("valid_marker_observed",d.observed);if(d.observed){radar.num("bearing_deg",d.bearing);radar.num("bearing_error_deg",d.error);radar.num("distance_client_px",d.distance);if(job.radarConfig.turnOnly)radar.num("radar_angle_delta_deg",d.radarAngleDelta);else radar.raw("radar_angle_delta_deg","null");}else{radar.raw("bearing_deg","null");radar.raw("bearing_error_deg","null");radar.raw("distance_client_px","null");radar.raw("radar_angle_delta_deg","null");}
 radar.num("stable_observations",job.radarState.stable);radar.num("requested_stable_observations",job.radarConfig.samples);
 radar.bol("goal_reached",job.done&&(job.reason=="radar_bearing_aligned"||job.reason=="radar_marker_near_centre"));radar.str("stop_reason",job.reason);
 radar.num("observation_lease_ms",(double)job.radarLeaseMs);radar.str("last_capture_qpc_ticks",std::to_string(job.radarLastQpc));radar.str("last_valid_marker_qpc_ticks",std::to_string(job.radarState.lastQpc));radar.str("qpc_frequency",std::to_string(job.qpcFrequency));
 radar.str("finished_qpc_ticks",std::to_string(job.qpcFinished));radar.bol("trace_complete",job.radarTraceDropped==0);radar.num("trace_dropped",job.radarTraceDropped);
 std::string trace="[";for(const auto& observation:job.radarTrace){if(trace.size()>1)trace+=',';JOut sample;sample.str("capture_qpc_ticks",std::to_string(observation.qpc));sample.bol("valid_marker_observed",observation.decision.observed);if(observation.decision.observed){sample.num("bearing_deg",observation.decision.bearing);sample.num("error_deg",observation.decision.error);sample.num("distance_px",observation.decision.distance);}else{sample.raw("bearing_deg","null");sample.raw("error_deg","null");sample.raw("distance_px","null");}sample.num("steer",observation.decision.steer);sample.bol("forward",observation.decision.forward);if(!observation.decision.reason.empty())sample.str("reason",observation.decision.reason);trace+=sample.done();}radar.raw("observations_tail",trace+"]");
 radar.str("measurement","configured_marker_bearing_and_distance_in_heading_up_radar");radar.str("key_observation_membership","reserved/configured steering and forward bindings; a binding may never be pressed during this job");radar.str("heading_assumption","caller declares radar heading-up and steering keys/sign; neither world yaw nor vehicle/character response is measured");radar.raw("application_response","null");radar.raw("rendered_frame_age_ms","null");
 out.raw("radar_navigation",radar.done());if(job.captureBound)out.raw("capture_scope",CaptureScopeJson(job.captureScope));if(job.done&&job.finishedCaptureScopeValid)out.raw("finished_capture_scope",CaptureScopeJson(job.finishedCaptureScope));
}
