// Caller serializes target adoption and all input publication with g_inputMx.
// This is refusal policy only: it grants NO old-window message authority.
bool RetargetHeldJobsReleaseAllLocked();
struct RetargetQuiescence {
 HWND window=nullptr;unsigned long long generation=0;unsigned held=0,pending=0,translations=0;
 bool oldWindowLive=false,required=true;
};
unsigned long long g_retargetHeldRefused=0,g_retargetPendingRefused=0,g_retargetTranslationRefused=0;
std::string g_retargetLastRefusal;
RetargetQuiescence RetargetQuiescenceLocked(){
 RetargetQuiescence state;state.window=g_hwnd;state.generation=g_inputReadyGeneration.load();state.translations=g_postedTranslationsInFlight;
 for(int key=1;key<256;key++)if(g_vk[key])++state.held;
 DWORD owner=0;const DWORD thread=state.window?GetWindowThreadProcessId(state.window,&owner):0;
 state.oldWindowLive=thread&&owner==g_pid;
 if(!state.oldWindowLive)return state;
 const auto binding=WndBindingFind(state.window);const auto top=CurrentWindowProcedure(state.window);
 // Exact existing detach/original comparisons only; never unwrap opaque tokens.
 state.required=!(WndBindingLive(binding)&&(top==h_WndProc||top==binding->original));
 AcquireSRWLockShared(&g_ringLock);
 const auto stamp=(ULONG_PTR)GetPropW(state.window,kPostedWindowProperty);
 for(const auto& entry:g_ring)if(entry.m&&entry.serial&&entry.window==state.window&&entry.generation==state.generation&&
     entry.thread==thread&&entry.windowStamp&&entry.windowStamp==stamp)++state.pending;
 ReleaseSRWLockShared(&g_ringLock);
 return state;
}
bool RetargetQuiescenceRequired(std::string& reason){
 const auto state=RetargetQuiescenceLocked();
 // Not-yet-posted translated chars still carry the old target generation even
 // if that target's top can be detached safely. Wait for publication to finish.
 if(state.translations){reason="target_change_requires_completed_owned_translation";++g_retargetTranslationRefused;g_retargetLastRefusal=reason;return false;}
 if(!state.required&&(!state.held||RetargetHeldJobsReleaseAllLocked()))return true; // exact known top plus actual job cleanup only
 // A dead old HWND cannot consume queued messages, but held global state still
 // requires explicit release. Invalid/dead publication records are not trusted.
 if(state.held){reason="target_change_requires_released_virtual_input";++g_retargetHeldRefused;g_retargetLastRefusal=reason;return false;}
 if(state.pending){reason="target_change_requires_drained_legacy_publications";++g_retargetPendingRefused;g_retargetLastRefusal=reason;return false;}
 return true;
}
std::string RetargetQuiescenceStatusJson(){
 std::lock_guard<std::recursive_mutex> inputLock(g_inputMx);const auto state=RetargetQuiescenceLocked();
 const bool heldBlocked=state.held&&(state.required||!RetargetHeldJobsReleaseAllLocked());
 const bool blocked=state.translations||heldBlocked||(state.required&&state.pending);
 char window[32];sprintf_s(window,"0x%llX",(unsigned long long)(ULONG_PTR)state.window);
 return "{\"version\":1,\"held_virtual_keys_or_buttons\":"+std::to_string(state.held)+",\"pending_current_legacy_publications\":"+std::to_string(state.pending)+
  ",\"required_for_current_chain\":"+(state.required?"true":"false")+",\"change_blocked\":"+(blocked?"true":"false")+",\"owned_translations_in_flight\":"+std::to_string(state.translations)+",\"old_window_live\":"+(state.oldWindowLive?"true":"false")+",\"window_generation\":"+std::to_string(state.generation)+",\"hwnd\":\""+window+"\",\"quiescent\":"+(!state.held&&!state.pending&&!state.translations?"true":"false")+
  ",\"translation_refusals\":"+std::to_string(g_retargetTranslationRefused)+",\"held_refusals\":"+std::to_string(g_retargetHeldRefused)+",\"pending_refusals\":"+std::to_string(g_retargetPendingRefused)+",\"last_refusal\":\""+g_retargetLastRefusal+
  "\",\"scope\":\"all target changes require finished owned translation and released non-job held input; opaque/uncertain old-chain changes also require released job input and dispatch opportunity for valid current-window legacy publications; explicit release then old-target queue readiness then retry; no application-consumption guarantee; no TTL escape for filtered pumps\"}";
}
