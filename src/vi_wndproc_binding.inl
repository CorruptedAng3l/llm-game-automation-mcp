// Each retained subclass calls the predecessor captured for THAT window object.
// A private property is an independent lifetime witness, not a human-intent proof.
// No application/window callback runs with g_wndBindingMx held.
LRESULT CALLBACK h_WndProc(HWND,UINT,WPARAM,LPARAM);
struct WndProcBinding {
 HWND window=nullptr;DWORD thread=0,owner=0;ULONG_PTR lifetime=0;
 bool unicode=true;WNDPROC original=nullptr;
};
const wchar_t kWndProcLifetimeProperty[]=L"vinput.wndproc_binding.lifetime.v1";
std::mutex g_wndBindingMx,g_wndAdoptionMx;
std::map<HWND,std::shared_ptr<WndProcBinding>> g_wndBindings;
std::atomic<ULONG_PTR> g_wndBindingSerial{0};
std::atomic<unsigned long long> g_wndBindingRefused{0},g_wndBindingMissing{0};
bool WndBindingLive(const std::shared_ptr<WndProcBinding>& binding) {
 if(!binding||!binding->window||!binding->original||binding->original==h_WndProc)return false;
 DWORD owner=0;return GetWindowThreadProcessId(binding->window,&owner)==binding->thread&&owner==binding->owner&&
  // The current outer subclass can legitimately use the other A/W API. Its
  // encoding is not this window object's lifetime or our saved predecessor's
  // encoding; keep that predecessor encoding solely for CallWindowProc.
  owner==g_pid&&
  (ULONG_PTR)GetPropW(binding->window,kWndProcLifetimeProperty)==binding->lifetime;
}
std::shared_ptr<WndProcBinding> WndBindingFind(HWND window) {
 std::lock_guard<std::mutex> lock(g_wndBindingMx);auto it=g_wndBindings.find(window);
 return it==g_wndBindings.end()?std::shared_ptr<WndProcBinding>{}:it->second;
}
bool WndBindingReady(HWND window) {return WndBindingLive(WndBindingFind(window));}
LRESULT WndBindingDefault(HWND h,UINT m,WPARAM w,LPARAM l) {
 return IsWindowUnicode(h)?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
}
LRESULT WndBindingOriginal(HWND h,UINT m,WPARAM w,LPARAM l) {
 auto binding=WndBindingFind(h);
 if(!WndBindingLive(binding)){++g_wndBindingMissing;return WndBindingDefault(h,m,w,l);}
 return binding->unicode?CallWindowProcW(binding->original,h,m,w,l):CallWindowProcA(binding->original,h,m,w,l);
}
// Capture the destruction witness on entry, retire only this exact record after
// the original callback returns. Reentrant callbacks retain their shared record.
struct WndBindingCallback {
 std::shared_ptr<WndProcBinding> binding;bool destroy=false;
 WndBindingCallback(HWND h,UINT m):binding(WndBindingFind(h)),destroy(m==WM_NCDESTROY&&WndBindingLive(binding)){}
 ~WndBindingCallback(){
  if(!destroy)return;
  std::lock_guard<std::mutex> lock(g_wndBindingMx);auto it=g_wndBindings.find(binding->window);
  if(it==g_wndBindings.end()||it->second!=binding)return;
  if((ULONG_PTR)GetPropW(binding->window,kWndProcLifetimeProperty)==binding->lifetime)RemovePropW(binding->window,kWndProcLifetimeProperty);
  g_wndBindings.erase(it);
 }
};
bool WndBindingCanAdopt(HWND window,std::string& reason) {
 DWORD owner=0;if(!window||!GetWindowThreadProcessId(window,&owner)||owner!=g_pid){reason="window_not_owned_or_destroyed";return false;}
 auto binding=WndBindingFind(window);WNDPROC top=CurrentWindowProcedure(window);
 if(!top){reason="window_procedure_unavailable";return false;}
 if(binding){
  if(!WndBindingLive(binding)){reason="window_lifetime_witness_missing_or_changed";return false;}
  // A current target's outer hook is already attached; this is a no-op.
  if(window==g_hwnd)return true;
  if(top!=h_WndProc&&top!=binding->original){reason="retained_opaque_outer_chain";return false;}
 }else if(top==h_WndProc){reason="hook_without_lifetime_binding";return false;}
 return true;
}
void WndBindingPruneDead() {
 std::lock_guard<std::mutex> lock(g_wndBindingMx);
 for(auto it=g_wndBindings.begin();it!=g_wndBindings.end();){DWORD owner=0;DWORD thread=GetWindowThreadProcessId(it->first,&owner);
  if(!thread||thread!=it->second->thread||owner!=it->second->owner)it=g_wndBindings.erase(it);else ++it;
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
