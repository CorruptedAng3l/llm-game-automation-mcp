// Include after vi_wndproc_binding.inl. All native writers use the existing
// original SetWindowPos; no input/adoption/binding mutex spans that callback.
vi_topmost::Policy g_topmostPolicy;
struct TopmostContext {std::shared_ptr<WndProcBinding> binding;bool keep=false;};
bool TopmostContextTake(TopmostContext& context,vi_topmost::Scope& scope){
 HWND window=g_hwnd;const auto generation=g_inputReadyGeneration.load();const auto binding=WndBindingFind(window);
 if(!window||!WndBindingLive(binding)||WndBindingFind(window)!=binding)return false;
 scope.window=window;scope.pid=g_pid;scope.thread=binding->thread;scope.lifetime=binding->lifetime;scope.adoption=generation;
 context.binding=binding;context.keep=g_cfg.topmostKeep.load();
 return window==g_hwnd&&generation==g_inputReadyGeneration&&WndBindingLive(binding)&&WndBindingFind(window)==binding;
}
bool TopmostScopeCurrent(void* value,const vi_topmost::Scope& scope){
 const auto& context=*(TopmostContext*)value;const auto& binding=context.binding;DWORD owner=0;
 return !g_acProduct.load()&&!g_jobRetargetDepth.load()&&!g_wndConfigCommitDepth.load()&&context.keep==g_cfg.topmostKeep.load()&&
  scope.window==g_hwnd&&scope.pid==g_pid&&scope.adoption==g_inputReadyGeneration&&binding&&binding->window==scope.window&&
  binding->thread==scope.thread&&binding->lifetime==scope.lifetime&&WndBindingLive(binding)&&WndBindingFind(scope.window)==binding&&
  GetWindowThreadProcessId(scope.window,&owner)==scope.thread&&owner==scope.pid;
}
bool TopmostBackground(void* value,const vi_topmost::Scope& scope){return TopmostScopeCurrent(value,scope)&&!ForegroundBelongsToTarget(RealForeground());}
vi_topmost::Callbacks TopmostCallbacks(TopmostContext& context){
 vi_topmost::Callbacks callbacks;callbacks.context=&context;callbacks.current=TopmostScopeCurrent;callbacks.background=TopmostBackground;callbacks.relations=vi_topmost::NativeRelationsCallback;callbacks.topmost=vi_topmost::NativeTopmost;callbacks.position=o_SetWindowPos;return callbacks;
}
void TopmostObserve(){
 TopmostContext context;vi_topmost::Scope scope;TopmostContextTake(context,scope);const auto callbacks=TopmostCallbacks(context);g_topmostPolicy.Tick(scope,g_cfg.topmostKeep.load(),callbacks);
}
bool TopmostSetWindowPos(HWND window,HWND after,int x,int y,int width,int height,UINT flags,BOOL& nativeResult){
 if(after!=HWND_TOPMOST||(flags&SWP_NOZORDER)||window!=g_hwnd)return false;
 TopmostContext context;vi_topmost::Scope scope;if(!TopmostContextTake(context,scope)||scope.window!=window)return false;
 const auto result=g_topmostPolicy.Reassert(scope,context.keep,TopmostCallbacks(context),{after,x,y,width,height,flags});
 if(!result.handled)return false;nativeResult=result.nativeResult;return true;
}
vi_topmost::State TopmostCurrentStatus(){
 auto state=g_topmostPolicy.Snapshot();TopmostContext context;vi_topmost::Scope scope;state.currentKnown=false;
 if(!TopmostContextTake(context,scope)||!TopmostScopeCurrent(&context,scope)){state=vi_topmost::State{};state.keep=g_cfg.topmostKeep.load();state.reason="adopted_scope_unconfirmed";return state;}
 if(!vi_topmost::Same(scope,state.scope)){state=vi_topmost::State{};state.scope=scope;state.reason="awaiting_current_scope_observation";}
 else if(state.keep!=context.keep)state.reason="awaiting_policy_observation";
 state.keep=context.keep;
 bool value=false;if(vi_topmost::NativeTopmost(nullptr,scope.window,value)&&TopmostScopeCurrent(&context,scope)){state.currentKnown=true;state.currentTopmost=value;}
 return state;
}
