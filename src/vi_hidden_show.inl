// Explicit visibility request: queue once to the exact adopted UI thread, never
// wait for that queue, and expire without replay if its scope or owner changes.
UINT g_hiddenShowMessage=0;
struct HiddenShowRequest {
 ULONG_PTR ticket=0;CaptureScope scope;std::shared_ptr<WndProcBinding> binding;
 unsigned long long claimEpoch=0,ownerSeq=0;bool claimed=false,pending=false,completed=false,callAttempted=false,visibleAfter=false;
 ULONGLONG expires=0;std::string reason="not_requested";
};
HiddenShowRequest g_hiddenShow;ULONG_PTR g_hiddenShowSerial=0;
const ULONG_PTR kHiddenShowCookie=(ULONG_PTR)&g_hiddenShowSerial ^ 0x56495348;
void HiddenShowExpireLocked(){if(g_hiddenShow.pending&&!g_hiddenShow.callAttempted&&HousekeepingNow()>=g_hiddenShow.expires){g_hiddenShow.pending=false;g_hiddenShow.completed=true;g_hiddenShow.reason="expired_without_dispatch";}}
const char* HiddenShowInvalidLocked(const HiddenShowRequest& request){
 if(HousekeepingNow()>=request.expires)return "expired_without_dispatch";
 if(g_acProduct.load())return "anti_cheat_refusal";
 if(g_jobClaimEpoch!=request.claimEpoch||(request.claimed&&HousekeepingNow()>=g_jobClaimExpiry.load()))return "ownership_changed_or_expired";
 if(g_ownerSeq!=request.ownerSeq||g_ownerHolding||AdoptionHumanSelectionPending()||ComputeRealFg())return "owner_activity";
 if(!CaptureScopeCurrent(request.scope)||!WndBindingLive(request.binding)||WndBindingFind(request.scope.window)!=request.binding)return "window_scope_changed";
 if(!g_adoptionActivationSafe||!o_ShowWindow)return "visibility_protection_unavailable";
 return nullptr;
}
void HiddenShowDispatch(HWND window,WPARAM ticket,LPARAM cookie,const std::shared_ptr<WndProcBinding>& callback){
 if((ULONG_PTR)cookie!=kHiddenShowCookie)return;
 HiddenShowRequest request;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);HiddenShowExpireLocked();if(!g_hiddenShow.pending||g_hiddenShow.ticket!=(ULONG_PTR)ticket)return;
  const char* reason=HiddenShowInvalidLocked(g_hiddenShow);if(!reason&&(window!=g_hiddenShow.scope.window||callback!=g_hiddenShow.binding||GetCurrentThreadId()!=callback->thread))reason="wrong_dispatch_thread_or_binding";
  if(reason){g_hiddenShow.pending=false;g_hiddenShow.completed=true;g_hiddenShow.reason=reason;return;}request=g_hiddenShow;
 }
 ScopedAdoptionActivationBarrier barrier;
 // Only this UI-thread call is fenced strictly, including callbacks triggered by
 // ShowWindow. No mutex is held across an application/system window callback.
 struct NoActivation{NoActivation(){++g_explicitNoActivation;}~NoActivation(){--g_explicitNoActivation;}} noActivation;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);const char* reason=HiddenShowInvalidLocked(request);if(!barrier.hook&&!reason)reason="activation_barrier_unavailable";
  if(!g_hiddenShow.pending||g_hiddenShow.ticket!=request.ticket)return;
  if(reason){g_hiddenShow.pending=false;g_hiddenShow.completed=true;g_hiddenShow.reason=reason;return;}
  g_hiddenShow.callAttempted=true;g_hiddenShow.reason="dispatching";
 }
 o_ShowWindow(window,SW_SHOWNA); // BOOL reports previous visibility, not success.
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_hiddenShow.ticket==request.ticket){g_hiddenShow.pending=false;g_hiddenShow.completed=true;g_hiddenShow.visibleAfter=window==g_hwnd&&request.scope.adoption==g_inputReadyGeneration&&WndBindingLive(request.binding)&&WndBindingFind(window)==request.binding&&IsWindowVisible(window)!=FALSE;g_hiddenShow.reason=g_hiddenShow.visibleAfter?"observed_visible_after_dispatch":"visibility_unconfirmed_after_dispatch";}}
}
