// Called inside presentation_window, never adds a new top-level status key.
void TopmostFields(JOut& evidence){
 const auto state=TopmostCurrentStatus();JOut policy;policy.num("version",1);policy.str("policy",state.keep?"keep":"strip_background");policy.str("reason",state.reason);
 policy.raw("current_topmost",state.currentKnown?(state.currentTopmost?"true":"false"):"null");policy.bol("topmost_stripped",state.currentKnown&&!state.currentTopmost&&state.strippedEver);
 policy.str("demotion_calls",std::to_string(state.demotionCalls));policy.str("demotions_observed",std::to_string(state.demotions));policy.str("reapplications_observed",std::to_string(state.reapplications));policy.str("reassertion_requests",std::to_string(state.reassertionRequests));policy.str("reassertions_prevented",std::to_string(state.preventedRequests));policy.str("refusals",std::to_string(state.refusals));policy.num("last_error",state.error);policy.num("same_process_relationship_count",state.relationshipCount);
 policy.str("scope","current adopted binding lifetime only; HWND_BOTTOM demotion without activation; ownership census is double-checked but not atomic with native window-manager changes; keep never restores topmost");
 if(state.scope.Valid()){char hwnd[32];sprintf_s(hwnd,"0x%llX",(unsigned long long)(ULONG_PTR)state.scope.window);policy.str("hwnd",hwnd);policy.num("thread",state.scope.thread);policy.str("adoption_generation",std::to_string(state.scope.adoption));policy.str("binding_lifetime",std::to_string(state.scope.lifetime));}
 evidence.raw("topmost",policy.done());
}
