// Each retained subclass calls the predecessor captured for THAT window object.
// A private property is an independent lifetime witness, not a human-intent proof.
// No application/window callback runs with g_wndBindingMx held.
LRESULT CALLBACK h_WndProc(HWND,UINT,WPARAM,LPARAM);
struct WndProcBinding {
 HWND window=nullptr;DWORD thread=0,owner=0;ULONG_PTR lifetime=0;
 bool unicode=true;WNDPROC original=nullptr;
 // Immutable predecessor/lifetime; trust loss never clears on a restamp.
 std::atomic<unsigned> trustLost{0},lastLoss{0};std::atomic<bool> destroying{false},retired{false};
};
const wchar_t kWndProcLifetimeProperty[]=L"vinput.wndproc_binding.lifetime.v1";
std::mutex g_wndBindingMx,g_wndAdoptionMx;
std::map<HWND,std::shared_ptr<WndProcBinding>> g_wndBindings;
std::atomic<ULONG_PTR> g_wndBindingSerial{0};
std::atomic<unsigned long long> g_wndBindingRefused{0},g_wndBindingMissing{0};
std::atomic<unsigned long long> g_wndPropertyMissing{0},g_wndPropertyMismatch{0},g_wndOwnerChanged{0};
std::atomic<unsigned long long> g_wndGuiUnconfirmed{0},g_wndGuiDefaulted{0},g_wndInputRefused{0},g_wndDestroyRetired{0};
void WndBindingLose(const std::shared_ptr<WndProcBinding>& binding,unsigned reason){
 binding->lastLoss=reason;const unsigned before=binding->trustLost.fetch_or(reason);if(before&reason)return;
 if(reason==1)++g_wndPropertyMissing;else if(reason==2)++g_wndPropertyMismatch;else ++g_wndOwnerChanged;
}
bool WndBindingGuiUsable(const std::shared_ptr<WndProcBinding>& binding){
 if(!binding||binding->retired||!binding->window||!binding->thread||!binding->lifetime||!binding->original||binding->original==h_WndProc)return false;
 DWORD owner=0;return GetWindowThreadProcessId(binding->window,&owner)==binding->thread&&owner==binding->owner&&owner==g_pid;
}
bool WndBindingLive(const std::shared_ptr<WndProcBinding>& binding) {
 if(!binding||binding->retired||binding->destroying||!binding->window||!binding->original||binding->original==h_WndProc||!binding->lifetime)return false;
 if(!WndBindingGuiUsable(binding)){WndBindingLose(binding,4);return false;}
 // The current outer can use the other A/W API. Saved predecessor encoding
 // is independent of that top-level flag, and is never rewritten on loss.
 const auto property=(ULONG_PTR)GetPropW(binding->window,kWndProcLifetimeProperty);
 if(property!=binding->lifetime){WndBindingLose(binding,property?2u:1u);return false;}
 return binding->trustLost.load()==0;
}
std::shared_ptr<WndProcBinding> WndBindingFind(HWND window) {
 std::lock_guard<std::mutex> lock(g_wndBindingMx);auto it=g_wndBindings.find(window);
 return it==g_wndBindings.end()?std::shared_ptr<WndProcBinding>{}:it->second;
}
bool WndBindingReady(HWND window) {return WndBindingLive(WndBindingFind(window));}
// Preserve existing unrelated/unbound raw windows. A known retained binding
// or the selected target may not regain admission through independent stamps.
bool WndBindingInputAllowed(HWND window){
 const auto binding=WndBindingFind(window);
 if(!binding)return window!=g_hwnd;
 return WndBindingLive(binding)&&WndBindingFind(window)==binding;
}
bool WndBindingRawAllowed(HWND window){
 DWORD owner=0;if(!window||!GetWindowThreadProcessId(window,&owner)||owner!=g_pid)return false;
 if(!WndBindingInputAllowed(window))return false;
 const HWND selected=g_hwnd;
 return !selected||window==selected||WndBindingInputAllowed(selected);
}
void WndBindingRecordInputRefusal(){++g_wndInputRefused;}
LRESULT WndBindingDefault(HWND h,UINT m,WPARAM w,LPARAM l) {
 return IsWindowUnicode(h)?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
}
LRESULT WndBindingOriginal(const std::shared_ptr<WndProcBinding>& binding,HWND h,UINT m,WPARAM w,LPARAM l) {
 if(!binding||binding->window!=h||binding->thread!=GetCurrentThreadId()||!WndBindingGuiUsable(binding)||WndBindingFind(h)!=binding){
  ++g_wndBindingMissing;++g_wndGuiDefaulted;return WndBindingDefault(h,m,w,l);
 }
 const bool trusted=WndBindingLive(binding);
 // This final check also catches loss during an earlier native/filter call.
 // No original input callback is granted by the GUI-continuity fallback.
 if((PostedInputMessage(m)||m==WM_INPUT)&&!trusted){WndBindingRecordInputRefusal();return m==WM_INPUT?WndBindingDefault(h,m,w,l):0;}
 if(binding->trustLost.load())++g_wndGuiUnconfirmed;
 return binding->unicode?CallWindowProcW(binding->original,h,m,w,l):CallWindowProcA(binding->original,h,m,w,l);
}
// Retain the entry record through reentrant original callbacks. NCDESTROY
// cleanup does not depend on a property the application may already remove.
struct WndBindingCallback {
 std::shared_ptr<WndProcBinding> binding;bool gui=false,destroy=false;
 WndBindingCallback(HWND h,UINT m):binding(WndBindingFind(h)){
  gui=WndBindingGuiUsable(binding)&&binding->thread==GetCurrentThreadId()&&WndBindingFind(h)==binding;
  if(binding)WndBindingLive(binding); // observe/latch loss even for non-input messages
  destroy=gui&&m==WM_NCDESTROY;if(destroy)binding->destroying=true;
 }
 ~WndBindingCallback(){
  if(!destroy)return;
  {std::lock_guard<std::mutex> lock(g_wndBindingMx);auto it=g_wndBindings.find(binding->window);
   if(it==g_wndBindings.end()||it->second!=binding)return;
   binding->retired=true;g_wndBindings.erase(it);++g_wndDestroyRetired;}
  // No window/native query or application callback under the map lock.
  if((ULONG_PTR)GetPropW(binding->window,kWndProcLifetimeProperty)==binding->lifetime)RemovePropW(binding->window,kWndProcLifetimeProperty);
 }
};
bool WndBindingCanAdopt(HWND window,std::string& reason) {
 DWORD owner=0;if(!window||!GetWindowThreadProcessId(window,&owner)||owner!=g_pid){reason="window_not_owned_or_destroyed";return false;}
 auto binding=WndBindingFind(window);WNDPROC top=CurrentWindowProcedure(window);
 if(!top){reason="window_procedure_unavailable";return false;}
 if(binding){
  if(!WndBindingLive(binding)){reason=binding->trustLost.load()?"window_binding_quarantined_restart_or_select_fresh_window":"window_lifetime_witness_missing_or_changed";return false;}
  // A current target's outer hook is already attached; this is a no-op.
  if(window==g_hwnd)return true;
  if(top!=h_WndProc&&top!=binding->original){reason="retained_opaque_outer_chain";return false;}
 }else if(top==h_WndProc){reason="hook_without_lifetime_binding";return false;}
 return true;
}
void WndBindingPruneDead() {
 std::lock_guard<std::mutex> lock(g_wndBindingMx);
 for(auto it=g_wndBindings.begin();it!=g_wndBindings.end();){DWORD owner=0;DWORD thread=GetWindowThreadProcessId(it->first,&owner);
  if(!thread||thread!=it->second->thread||owner!=it->second->owner){it->second->retired=true;it=g_wndBindings.erase(it);}else ++it;
 }
}
bool WndBindingAttach(HWND window,std::shared_ptr<WndProcBinding>& result,std::string& reason) {
 if(!WndBindingCanAdopt(window,reason))return false;
 result=WndBindingFind(window);
 if(window==g_hwnd&&result)return true;
 WNDPROC top=CurrentWindowProcedure(window);
 if(result&&top==h_WndProc)return true; // reuse its predecessor, never capture ourselves
 bool fresh=!result;
 if(fresh){
  WndBindingPruneDead();
  {std::lock_guard<std::mutex> lock(g_wndBindingMx);if(g_wndBindings.size()>=128){reason="window_binding_capacity";return false;}}
  if(GetPropW(window,kWndProcLifetimeProperty)){reason="unrecognized_window_lifetime_property";return false;}
  result=std::make_shared<WndProcBinding>();result->window=window;result->thread=GetWindowThreadProcessId(window,&result->owner);
  result->unicode=IsWindowUnicode(window)!=FALSE;result->original=top;
  ULONG_PTR serial=g_wndBindingSerial.load();
  for(;;){if(serial==std::numeric_limits<ULONG_PTR>::max()){reason="window_binding_serial_exhausted";return false;}
   if(g_wndBindingSerial.compare_exchange_weak(serial,serial+1)){result->lifetime=serial+1;break;}}
  if(!SetPropW(window,kWndProcLifetimeProperty,(HANDLE)result->lifetime)){reason="window_lifetime_property_failed";return false;}
  {std::lock_guard<std::mutex> lock(g_wndBindingMx);g_wndBindings[window]=result;}
 }
 if(!WndBindingLive(result)||CurrentWindowProcedure(window)!=result->original){reason="window_chain_changed_before_attach";return false;}
 SetLastError(0);WNDPROC previous=result->unicode?(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)h_WndProc):(WNDPROC)SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)h_WndProc);
 if(!previous){reason="window_subclass_install_failed";return false;}
 if(previous!=result->original){
  // The getter/setter representation differs, or an external writer raced it.
  // Restore ONLY if we still
  // own the top. Never treat an opaque outer procedure as a new saved original.
  bool retiredFresh=false;
  if(previous!=h_WndProc&&CurrentWindowProcedure(window)==h_WndProc){
   WNDPROC replaced=result->unicode?(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)previous):(WNDPROC)SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)previous);
   // Fresh system-class getter and setter values need not use the same opaque
   // CallProc representation. Do not change a published predecessor. Retry as
   // fresh only after the native rollback proves our topmost hook was removed.
   if(fresh&&replaced==h_WndProc){
    std::lock_guard<std::mutex> lock(g_wndBindingMx);auto it=g_wndBindings.find(window);
    WNDPROC restored=result->unicode?(WNDPROC)GetWindowLongPtrW(window,GWLP_WNDPROC):(WNDPROC)GetWindowLongPtrA(window,GWLP_WNDPROC);
    if(it!=g_wndBindings.end()&&it->second==result&&WndBindingLive(result)&&
       (IsWindowUnicode(window)!=FALSE)==result->unicode&&restored==previous&&
       (ULONG_PTR)RemovePropW(window,kWndProcLifetimeProperty)==result->lifetime){
     g_wndBindings.erase(it);retiredFresh=true;
    }
   }
  }
  if(retiredFresh)Log("fresh subclass record retired after verified native rollback hwnd=%p",window);
  reason="window_chain_changed_during_attach";return false;
 }
 if(!WndBindingLive(result)){reason="window_lifetime_changed_during_attach";return false;}return true;
}
void WndBindingDetachIfTop(HWND window) {
 auto binding=WndBindingFind(window);if(!WndBindingLive(binding)||CurrentWindowProcedure(window)!=h_WndProc)return;
 // An opaque outer chain retains our inner hook. Leave that chain and binding
 // intact; only the known topmost hook may be restored in reverse order.
 if(binding->unicode)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)binding->original);
 else SetWindowLongPtrA(window,GWLP_WNDPROC,(LONG_PTR)binding->original);
}
