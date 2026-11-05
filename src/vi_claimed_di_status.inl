std::string ClaimedDiStatusJson(){
 JOut out;out.num("version",1);out.bol("future_calls_only",true);out.bol("escape_ready",false);out.bol("previous_acquisitions_released",false);
 out.str("previous_acquisitions","unknown: no retained device lifetime registry or automatic Unacquire");
 out.num("probe_attempted_mask",g_claimDiProbeTried.load());out.num("probe_covered_mask",g_claimDiProbeCovered.load());
 out.str("probe_bits","1=dinput8 A mouse,2=dinput8 W mouse,4=legacy3 A mouse,8=legacy3 W mouse; install observations, not ongoing entry integrity or all driver implementations");
 out.num("acquire_entries",g_claimDiAcquireCovered.load());out.num("cooperative_entries",g_claimDiCoopCovered.load());
 out.num("hook_failed",(double)g_claimDiHookFailed.load());out.num("hook_busy",(double)g_claimDiHookBusy.load());out.num("probe_failed",(double)g_claimDiProbeFailed.load());
 out.num("mouse_acquire_refused",(double)g_claimDiAcquireBlocked.load());out.num("unknown_refused",(double)g_claimDiUnknownRefused.load());out.num("transition_refused",(double)g_claimDiTransitionRefused.load());
 out.num("cooperative_normalized",(double)g_claimDiCoopNormalized.load());out.num("cooperative_failed",(double)g_claimDiCoopFailed.load());out.num("invalid_flags",(double)g_claimDiInvalidFlags.load());out.num("inspection_reentry_refused",(double)g_claimDiInspectionReentry.load());
 out.str("policy","claimed mouse/unknown Acquire refuses; legal claimed mouse cooperative flags are nonexclusive; native failures remain failures; unclaimed admitted and proven nonmouse behavior retained");return out.done();
}
