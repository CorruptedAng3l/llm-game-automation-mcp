#pragma once
// Adopted-window background demotion only. No activation, restoration or input.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace vi_topmost {
struct Scope {
 HWND window=nullptr;DWORD pid=0,thread=0;std::uintptr_t lifetime=0;std::uint64_t adoption=0;
 bool Valid()const{return window&&pid&&thread&&lifetime;}
};
inline bool Same(const Scope& a,const Scope& b){return a.window==b.window&&a.pid==b.pid&&a.thread==b.thread&&a.lifetime==b.lifetime&&a.adoption==b.adoption;}
struct Node {HWND window=nullptr,owner=nullptr;DWORD pid=0,thread=0;};
inline bool Same(const Node& a,const Node& b){return a.window==b.window&&a.owner==b.owner&&a.pid==b.pid&&a.thread==b.thread;}
struct Relations {bool complete=false;std::string reason="relationship_census_unavailable";std::vector<Node> members;};
constexpr unsigned DesktopLimit=4096,RelatedLimit=64;
// Pure closure over a complete top-level census; every induced owner/owned
// relation must resolve to this PID. Unknown or truncated enumerations refuse.
inline Relations SelectRelations(const Scope& scope,const std::vector<Node>& desktop,bool complete){
 Relations out;if(!complete){out.reason="relationship_census_incomplete";return out;}
 if(!scope.Valid()){out.reason="adopted_scope_unavailable";return out;}
 if(desktop.size()>DesktopLimit){out.reason="relationship_census_capacity";return out;}
 auto find=[&](HWND window)->const Node*{for(const auto& n:desktop)if(n.window==window)return &n;return nullptr;};
 for(std::size_t i=0;i<desktop.size();++i){const auto& n=desktop[i];if(!n.window||!n.pid||!n.thread){out.reason="relationship_member_unconfirmed";return out;}for(std::size_t j=0;j<i;++j)if(desktop[j].window==n.window){out.reason="relationship_census_duplicate";return out;}}
 const auto first=find(scope.window);if(!first||first->pid!=scope.pid||first->thread!=scope.thread){out.reason="adopted_identity_changed";return out;}
 out.members.push_back(*first);
 for(std::size_t i=0;i<out.members.size();++i){
  const auto current=out.members[i];
  auto add=[&](const Node& n){for(const auto& old:out.members)if(old.window==n.window)return true;if(n.pid!=scope.pid){out.reason="foreign_owner_or_owned_window";return false;}if(out.members.size()>=RelatedLimit){out.reason="relationship_component_capacity";return false;}out.members.push_back(n);return true;};
  if(current.owner){const auto owner=find(current.owner);if(!owner){out.reason="relationship_owner_unconfirmed";return out;}if(!add(*owner))return out;}
  for(const auto& n:desktop)if(n.owner==current.window&&!add(n))return out;
 }
 // Ownership cycles are invalid even if every handle resolves to this process.
 for(const auto& start:out.members){HWND next=start.owner;unsigned depth=0;while(next){if(next==start.window||++depth>=RelatedLimit){out.reason="relationship_owner_cycle";return out;}const auto item=find(next);if(!item){out.reason="relationship_owner_unconfirmed";return out;}next=item->owner;}}
 std::sort(out.members.begin(),out.members.end(),[](const Node& a,const Node& b){return (std::uintptr_t)a.window<(std::uintptr_t)b.window;});
 out.complete=true;out.reason="same_process_relationships_confirmed";return out;
}
inline bool Same(const Relations& a,const Relations& b){if(!a.complete||!b.complete||a.members.size()!=b.members.size())return false;for(std::size_t i=0;i<a.members.size();++i)if(!Same(a.members[i],b.members[i]))return false;return true;}
struct Enumeration {std::vector<Node> windows;bool valid=true;};
inline BOOL CALLBACK Enumerate(HWND window,LPARAM context){
 auto& state=*(Enumeration*)context;if(state.windows.size()>=DesktopLimit){state.valid=false;return FALSE;}
 Node node;node.window=window;node.thread=GetWindowThreadProcessId(window,&node.pid);node.owner=GetWindow(window,GW_OWNER);
 DWORD againPid=0;const auto againThread=GetWindowThreadProcessId(window,&againPid);
 if(!node.thread||!node.pid||againThread!=node.thread||againPid!=node.pid||GetWindow(window,GW_OWNER)!=node.owner||!IsWindow(window)){state.valid=false;return FALSE;}
 state.windows.push_back(node);return TRUE;
}
inline Relations NativeRelations(const Scope& scope){Enumeration state;const bool complete=EnumWindows(Enumerate,(LPARAM)&state)!=FALSE&&state.valid;return SelectRelations(scope,state.windows,complete);}
using Position=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);
struct Callbacks {
 void* context=nullptr;
 // Current includes exact retained adoption/lifetime and policy eligibility.
 bool(*current)(void*,const Scope&)=nullptr;
 bool(*background)(void*,const Scope&)=nullptr;
 Relations(*relations)(void*,const Scope&)=nullptr;
 bool(*topmost)(void*,HWND,bool&)=nullptr;
 Position position=nullptr;
};
inline bool NativeTopmost(void*,HWND window,bool& value){SetLastError(0);const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);const auto error=GetLastError();if(!style&&error)return false;value=(style&WS_EX_TOPMOST)!=0;return true;}
inline Relations NativeRelationsCallback(void*,const Scope& scope){return NativeRelations(scope);}
struct State {
 Scope scope;bool keep=false,busy=false,currentKnown=false,currentTopmost=false,strippedEver=false;
 std::uint64_t serial=0,demotionCalls=0,demotions=0,reapplications=0,reassertionRequests=0,preventedRequests=0,refusals=0;
 unsigned relationshipCount=0;DWORD error=0;std::string reason="no_window_adopted";
};
struct Request {HWND after=nullptr;int x=0,y=0,width=0,height=0;UINT flags=0;};
struct Result {bool handled=false,attempted=false,verified=false;BOOL nativeResult=FALSE;};
class Policy {
 std::mutex mutex;State state;
 struct Ticket {Scope scope;std::uint64_t serial=0;bool topBefore=false,request=false;};
 bool Begin(const Scope& scope,bool keep,Ticket& ticket){std::lock_guard<std::mutex> lock(mutex);if(state.busy)return false;if(!Same(state.scope,scope)){state=State{};state.scope=scope;}state.keep=keep;state.currentKnown=false;state.error=0;if(!scope.Valid()){state.reason="adopted_scope_unavailable";return false;}state.busy=true;ticket.scope=scope;ticket.serial=++state.serial;return true;}
 void Finish(const Ticket& ticket,const char* reason,bool known,bool top,DWORD error=0,unsigned relationships=0,bool attempted=false,bool verified=false){
  std::lock_guard<std::mutex> lock(mutex);if(!state.busy||state.serial!=ticket.serial||!Same(state.scope,ticket.scope))return;
  state.busy=false;state.reason=reason;state.currentKnown=known;state.currentTopmost=top;state.error=error;state.relationshipCount=relationships;
  if(attempted)++state.demotionCalls;
  if(verified){if(ticket.topBefore){++state.demotions;state.strippedEver=true;}if(ticket.request)++state.preventedRequests;}
  if(!known||error||(attempted&&!verified))++state.refusals;
 }
 Result Run(const Scope& scope,bool keep,const Callbacks& api,const Request* request){
  Result result;Ticket ticket;if(!Begin(scope,keep,ticket))return result;
  auto finish=[&](const char* why,bool known=false,bool top=false,DWORD error=0,unsigned count=0){Finish(ticket,why,known,top,error,count);return result;};
  if(!api.current||!api.background||!api.relations||!api.topmost||!api.position)return finish("native_callbacks_unavailable");
  if(!api.current(api.context,scope))return finish("adopted_scope_changed");
  if(!api.background(api.context,scope))return finish("foreground_or_policy_ineligible");
  bool top=false;if(!api.topmost(api.context,scope.window,top))return finish("topmost_style_unavailable");
  if(keep)return finish("topmost_keep",true,top);
  ticket.topBefore=top;ticket.request=request!=nullptr;
  {std::lock_guard<std::mutex> lock(mutex);if(top&&state.currentTopmost==false&&state.strippedEver)++state.reapplications;if(request)++state.reassertionRequests;}
  if(!top&&!request)return finish("observed_non_topmost",true,false);
  const auto first=api.relations(api.context,scope);if(!first.complete)return finish(first.reason.c_str());
  if(!api.current(api.context,scope)||!api.background(api.context,scope))return finish("scope_changed_before_relationship_recheck");
  const auto second=api.relations(api.context,scope);if(!second.complete)return finish(second.reason.c_str());
  if(!Same(first,second))return finish("relationship_census_changed");
  if(!api.current(api.context,scope)||!api.background(api.context,scope))return finish("scope_changed_before_demotion");
  // HWND_NOTOPMOST would put the window above ordinary windows. HWND_BOTTOM
  // demotes in the same call and cannot raise this window or its owner group.
  const UINT flags=request?(request->flags&~SWP_NOZORDER)|SWP_NOACTIVATE|SWP_NOOWNERZORDER:SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
  result.handled=request!=nullptr;result.attempted=true;
  SetLastError(0);result.nativeResult=api.position(scope.window,HWND_BOTTOM,request?request->x:0,request?request->y:0,request?request->width:0,request?request->height:0,flags);const DWORD error=result.nativeResult?0:GetLastError();
  if(!api.current(api.context,scope)||!api.background(api.context,scope)){Finish(ticket,"scope_changed_after_demotion",false,false,error,(unsigned)second.members.size(),true,false);SetLastError(error);return result;}
  if(result.nativeResult){const auto afterRelations=api.relations(api.context,scope);if(!Same(second,afterRelations)){Finish(ticket,"relationship_census_changed_after_demotion",false,false,error,(unsigned)second.members.size(),true,false);SetLastError(error);return result;}if(!api.current(api.context,scope)||!api.background(api.context,scope)){Finish(ticket,"scope_changed_after_demotion",false,false,error,(unsigned)second.members.size(),true,false);SetLastError(error);return result;}}
  bool after=false;const bool known=api.topmost(api.context,scope.window,after);result.verified=result.nativeResult&&known&&!after;
  Finish(ticket,result.verified?"demotion_observed":!result.nativeResult?"demotion_failed":!known?"post_demotion_style_unavailable":"still_topmost_after_demotion",known,after,error,(unsigned)second.members.size(),true,result.verified);
  // A native callback may set an error while verifying; preserve the setter's error.
  SetLastError(error);return result;
 }
public:
 State Snapshot(){std::lock_guard<std::mutex> lock(mutex);return state;}
 Result Tick(const Scope& scope,bool keep,const Callbacks& api){return Run(scope,keep,api,nullptr);}
 Result Reassert(const Scope& scope,bool keep,const Callbacks& api,const Request& request){if(request.after!=HWND_TOPMOST||(request.flags&SWP_NOZORDER))return {};return Run(scope,keep,api,&request);}
};
}
