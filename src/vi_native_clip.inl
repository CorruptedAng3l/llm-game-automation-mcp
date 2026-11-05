// Reserve under short state locks. Original calls and window/foreground queries
// run outside product locks. A rectangle receipt is not global ownership.
bool NativeClipWindowProof(HWND,DWORD&,ULONG_PTR&);
struct NativeClipScope {HWND window=nullptr,root=nullptr;DWORD thread=0;ULONG_PTR lifetime=0;unsigned long long adoption=0,geometry=0;RECT client{};};
struct NativeClipState {
 bool pending=false,releaseRequested=false,cleanupExhausted=false,busy=false;
 unsigned cleanupAttempts=0;unsigned long long serial=0,operation=0;ULONGLONG retryAt=0;
 NativeClipScope scope;RECT rectangle{};const char* lastResult="inactive";const char* scopeReason="not_requested";const char* operationKind="none";DWORD lastError=0;
 unsigned long long setForwarded=0,setFailed=0,clearForwarded=0,clearFailed=0,queryFailed=0,superseded=0,scopeRejected=0,reentrantRefused=0,busyRefused=0;
};
NativeClipState g_nativeClip;unsigned long long g_nativeClipSerial=0,g_nativeClipOperation=0;
thread_local unsigned g_nativeClipOriginalDepth=0;
thread_local unsigned g_nativeClipRequestDepth=0;
struct NativeClipRequestScope {NativeClipRequestScope(){++g_nativeClipRequestDepth;}~NativeClipRequestScope(){--g_nativeClipRequestDepth;}};
struct NativeClipOriginalScope {NativeClipOriginalScope(){++g_nativeClipOriginalDepth;}~NativeClipOriginalScope(){--g_nativeClipOriginalDepth;}};
BOOL NativeClipOriginalSet(const RECT* rectangle){NativeClipOriginalScope original;return o_ClipCursor(rectangle);}
BOOL NativeClipOriginalGet(RECT* rectangle){NativeClipOriginalScope original;return o_GetClipCursor(rectangle);}
bool NativeClipDesktop(RECT& result){
 const LONGLONG x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 if(w<=0||h<=0||x+w>(std::numeric_limits<LONG>::max)()||y+h>(std::numeric_limits<LONG>::max)())return false;
 result={(LONG)x,(LONG)y,(LONG)(x+w),(LONG)(y+h)};return true;
}
bool NativeClipInside(const RECT& value,const RECT& bounds){return value.right>value.left&&value.bottom>value.top&&value.left>=bounds.left&&value.top>=bounds.top&&value.right<=bounds.right&&value.bottom<=bounds.bottom;}
bool NativeClipBasicsCurrent(const NativeClipScope& scope){
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 return !g_jobRetargetDepth&&!g_wndConfigCommitDepth&&scope.window==g_hwnd&&scope.adoption==g_inputReadyGeneration&&scope.geometry==g_captureGeometryEpoch;
}
bool NativeClipTakeScope(NativeClipScope& result,const char*& reason){
 NativeClipScope scope;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_jobRetargetDepth||g_wndConfigCommitDepth){reason="adoption_not_ready";return false;}scope.window=g_hwnd;scope.adoption=g_inputReadyGeneration;scope.geometry=g_captureGeometryEpoch;}
 const auto owner=CursorOwnerSnapshot();
 if(owner.agentClaimLive){reason="agent_claim_active";return false;}
 if(!vi_cursor_guard::NativeClipAllowed(owner)){reason=owner.foregroundVerified?"not_owned_foreground":"foreground_unverified";return false;}
 if(!CursorProtectionReady()||!NativeClipWindowProof(scope.window,scope.thread,scope.lifetime)){reason="hooks_or_window_not_ready";return false;}
 scope.root=GetAncestor(scope.window,GA_ROOT);DWORD rootPid=0;
 if(!scope.root||(std::uintptr_t)scope.root!=owner.foregroundRoot||!GetWindowThreadProcessId(scope.root,&rootPid)||rootPid!=g_pid){reason="foreground_root_differs_from_selected_window";return false;}
 if(!IsWindowVisible(scope.window)||IsIconic(scope.window)){reason="window_hidden_or_minimized";return false;}
 RECT client{},outer{},afterOuter{};POINT first{0,0},last{};
 if(!GetClientRect(scope.window,&client)||client.right<=0||client.bottom<=0){reason="client_unavailable";return false;}
 last={client.right,client.bottom};
 if(!GetWindowRect(scope.window,&outer)||!ClientToScreen(scope.window,&first)||!ClientToScreen(scope.window,&last)||!GetWindowRect(scope.window,&afterOuter)){reason="client_mapping_unavailable";return false;}
 scope.client={first.x,first.y,last.x,last.y};
 if(!EqualRect(&outer,&afterOuter)||!NativeClipInside(scope.client,outer)){reason="client_mapping_invalid";return false;}
 DWORD thread=0;ULONG_PTR lifetime=0;RECT verifiedClient{};const auto after=CursorOwnerSnapshot();
 if(!vi_cursor_guard::NativeClipAllowed(after)||(std::uintptr_t)scope.root!=after.foregroundRoot||!NativeClipWindowProof(scope.window,thread,lifetime)||thread!=scope.thread||lifetime!=scope.lifetime||!GetClientRect(scope.window,&verifiedClient)||!EqualRect(&client,&verifiedClient)||!NativeClipBasicsCurrent(scope)){reason="scope_changed_during_geometry_query";return false;}
 result=scope;reason="verified_selected_foreground_client";return true;
}
bool NativeClipSameScope(const NativeClipScope& a,const NativeClipScope& b){return a.window==b.window&&a.root==b.root&&a.thread==b.thread&&a.lifetime==b.lifetime&&a.adoption==b.adoption&&a.geometry==b.geometry&&EqualRect(&a.client,&b.client);}
bool NativeClipScopeCurrent(const NativeClipScope& expected){NativeClipScope current;const char* reason=nullptr;return NativeClipTakeScope(current,reason)&&NativeClipSameScope(current,expected);}
// Flag only: permitted while an adoption/configuration lock is held.
void NativeClipScopeChanged(){std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(g_nativeClip.pending){g_nativeClip.releaseRequested=true;g_nativeClip.retryAt=0;g_nativeClip.lastResult="scope_changed_cleanup_pending";}}
bool NativeClipRefuseBusyLocked(){
 if(!g_nativeClip.busy&&!g_nativeClipOriginalDepth)return false;
 ++g_cursorCounts[vi_cursor_guard::Clip].blocked;
 if(g_nativeClipOriginalDepth)++g_nativeClip.reentrantRefused;else ++g_nativeClip.busyRefused;
 SetLastError(ERROR_BUSY);return true;
}
bool NativeClipCleanup(bool immediate,unsigned long long expectedSerial){
 RECT expected{};unsigned long long serial=0,operation=0;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;
  if(!state.pending||state.serial!=expectedSerial)return true;if(NativeClipRefuseBusyLocked())return false;
  if(g_jobRetargetDepth||g_wndConfigCommitDepth)return false;
  state.releaseRequested=true;if(state.cleanupAttempts>=3){state.cleanupExhausted=true;return false;}
  const auto now=GetTickCount64();if(!immediate&&now<state.retryAt)return false;
  serial=state.serial;expected=state.rectangle;operation=++g_nativeClipOperation;state.operation=operation;state.busy=true;state.operationKind="cleanup";++state.cleanupAttempts;state.retryAt=now+50;
 }
 // All lower query/write callbacks run outside product locks. An ordinary
 // reader may reenter from another thread; a competing Clip gets ERROR_BUSY.
 RECT actual{},after{},desktop{};const bool available=o_GetClipCursor!=nullptr;
 const BOOL queried=available?NativeClipOriginalGet(&actual):FALSE;
 const DWORD queryError=queried?ERROR_SUCCESS:available?GetLastError():ERROR_PROC_NOT_FOUND;
 const bool equal=queried&&EqualRect(&actual,&expected);bool clearCalled=false;BOOL cleared=FALSE,verified=FALSE;
 DWORD clearError=ERROR_SUCCESS,verifyError=ERROR_SUCCESS;bool unclipped=false;
 if(equal){
  if(o_ClipCursor){clearCalled=true;cleared=NativeClipOriginalSet(nullptr);clearError=cleared?ERROR_SUCCESS:GetLastError();}else clearError=ERROR_PROC_NOT_FOUND;
  if(cleared){verified=NativeClipOriginalGet(&after);verifyError=verified?ERROR_SUCCESS:GetLastError();if(verified)unclipped=NativeClipDesktop(desktop)&&EqualRect(&after,&desktop);}
 }
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;
 if(!state.busy||state.operation!=operation||state.serial!=serial){SetLastError(ERROR_RETRY);return false;}
 state.busy=false;state.operationKind="none";
 if(clearCalled){++state.clearForwarded;++g_cursorCounts[vi_cursor_guard::Clip].forwarded;}
 auto failed=[&](const char* result,DWORD error){state.lastResult=result;state.lastError=error;state.cleanupExhausted=state.cleanupAttempts>=3;SetLastError(error?error:ERROR_RETRY);return false;};
 if(!queried){++state.queryFailed;return failed("query_failed",queryError);}
 if(!equal){state.pending=false;state.releaseRequested=false;state.lastResult="superseded";state.lastError=0;++state.superseded;return true;}
 if(!cleared){++state.clearFailed;return failed("clear_failed",clearError);}
 if(!verified){++state.queryFailed;return failed("clear_query_failed",verifyError);}
 if(!unclipped&&EqualRect(&after,&expected))return failed("clear_unconfirmed",ERROR_RETRY);
 state.pending=false;state.releaseRequested=false;state.lastError=0;state.lastResult=unclipped?"cleared":"cleared_or_superseded";return true;
}
void NativeClipTick(){
 NativeClipScope scope;unsigned long long serial=0;bool release=false;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);const auto& state=g_nativeClip;if(!state.pending||state.busy||g_jobRetargetDepth||g_wndConfigCommitDepth)return;scope=state.scope;serial=state.serial;release=state.releaseRequested;}
 if(!release&&!NativeClipScopeCurrent(scope)){
  std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;
  if(!state.pending||state.busy||state.serial!=serial||g_jobRetargetDepth||g_wndConfigCommitDepth)return;
  state.releaseRequested=true;state.retryAt=0;state.lastResult="ownership_or_geometry_lost";release=true;
 }
 if(release)NativeClipCleanup(false,serial);
}
BOOL NativeClipRequest(const RECT* requested){
 if(g_nativeClipRequestDepth){std::lock_guard<std::recursive_mutex> lock(g_inputMx);++g_cursorCounts[vi_cursor_guard::Clip].blocked;++g_nativeClip.reentrantRefused;SetLastError(ERROR_BUSY);return FALSE;}
 NativeClipRequestScope requestScope;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(NativeClipRefuseBusyLocked())return FALSE;}
 if(!requested){
  unsigned long long expectedSerial=0;
  {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(NativeClipRefuseBusyLocked())return FALSE;g_virtualClipEnabled=false;
   if(!g_nativeClip.pending){++g_cursorCounts[vi_cursor_guard::Clip].blocked;++g_cursorCounts[vi_cursor_guard::Clip].virtualized;g_nativeClip.lastResult="unowned_null_virtualized";return TRUE;}g_nativeClip.releaseRequested=true;expectedSerial=g_nativeClip.serial;}
  return NativeClipCleanup(true,expectedSerial)?TRUE:FALSE;
 }
 RECT clip{};const bool readable=CursorReadBytes(requested,&clip,sizeof(clip));
 if(!readable||clip.right<clip.left||clip.bottom<clip.top){++g_cursorCounts[vi_cursor_guard::Clip].blocked;++g_cursorCounts[vi_cursor_guard::Clip].failedVirtual;SetLastError(readable?ERROR_INVALID_PARAMETER:ERROR_NOACCESS);return FALSE;}
 NativeClipTick();NativeClipScope scope;const char* reason=nullptr;RECT desktop{};
 bool permitted=NativeClipTakeScope(scope,reason);
 if(permitted&&(!NativeClipInside(clip,scope.client)||!NativeClipDesktop(desktop)||!NativeClipInside(clip,desktop))){permitted=false;reason="rectangle_outside_selected_client_or_desktop";}
 unsigned long long operation=0;
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;if(NativeClipRefuseBusyLocked())return FALSE;state.scopeReason=reason;
  if(!permitted){++state.scopeRejected;++g_cursorCounts[vi_cursor_guard::Clip].blocked;g_virtualClipRect=clip;g_virtualClipEnabled=true;++g_cursorCounts[vi_cursor_guard::Clip].virtualized;return TRUE;}
  if(state.pending&&(state.releaseRequested||!NativeClipSameScope(state.scope,scope))){SetLastError(ERROR_BUSY);return FALSE;}
  if(g_jobRetargetDepth||g_wndConfigCommitDepth||scope.window!=g_hwnd||scope.adoption!=g_inputReadyGeneration||scope.geometry!=g_captureGeometryEpoch){SetLastError(ERROR_NOT_READY);return FALSE;}
  if(!o_ClipCursor){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
  operation=++g_nativeClipOperation;state.operation=operation;state.busy=true;state.operationKind="set";
 }
 if(!NativeClipScopeCurrent(scope)){std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;if(state.operation==operation){state.busy=false;state.operationKind="none";state.scopeReason="scope_changed_before_native_set";++state.scopeRejected;}SetLastError(ERROR_NOT_READY);return FALSE;}
 ClaimNativeOperation nativeOperation;
 if(!nativeOperation){std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;if(state.operation==operation){state.busy=false;state.operationKind="none";state.scopeReason="claim_native_admission_denied";++state.scopeRejected;++g_cursorCounts[vi_cursor_guard::Clip].blocked;}SetLastError(ERROR_BUSY);return FALSE;}
 const BOOL accepted=NativeClipOriginalSet(&clip);const DWORD error=accepted?ERROR_SUCCESS:GetLastError();
 const bool current=accepted&&NativeClipScopeCurrent(scope);
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);auto& state=g_nativeClip;
  if(!state.busy||state.operation!=operation){SetLastError(ERROR_RETRY);return FALSE;}
  state.busy=false;state.operationKind="none";++state.setForwarded;++g_cursorCounts[vi_cursor_guard::Clip].forwarded;
  if(!accepted){++state.setFailed;state.lastResult="set_failed";state.lastError=error;SetLastError(error);return FALSE;}
  state.pending=true;state.releaseRequested=!current||g_jobRetargetDepth||g_wndConfigCommitDepth||scope.window!=g_hwnd||scope.adoption!=g_inputReadyGeneration||scope.geometry!=g_captureGeometryEpoch;
  state.cleanupExhausted=false;state.cleanupAttempts=0;state.retryAt=0;state.serial=++g_nativeClipSerial;state.scope=scope;state.rectangle=clip;state.lastResult="forwarded";state.lastError=0;g_virtualClipRect=clip;g_virtualClipEnabled=true;
 }
 NativeClipTick();return TRUE;
}
