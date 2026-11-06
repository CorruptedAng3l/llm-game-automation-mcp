// Actual offscreen Win32 windows plus pure refusal/race controls. No game or
// native input. Every desktop writer is terminal except exact owned, offscreen,
// NOACTIVATE topmost setup/demotion with a private thread token.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include "vi_cursor_spies.h"
#include "vi_topmost_policy.h"
#pragma comment(lib,"user32.lib")
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);fflush(stdout);if(ok)++passed;else ++failed;}
static bool Has(const std::string& s,const char* text){return s.find(text)!=std::string::npos;}
static HWND primary=nullptr,related=nullptr;static DWORD g_pid=0;static HWND g_hwnd=nullptr;
static std::atomic<unsigned long long> g_inputReadyGeneration{1};static std::atomic<unsigned> g_jobRetargetDepth{0},g_wndConfigCommitDepth{0};static std::atomic<const char*> g_acProduct{nullptr};
static struct {std::atomic<bool> topmostKeep{false};} g_cfg;
static const wchar_t kLifetime[]=L"vinput.topmost.fixture.binding.v1";
struct WndProcBinding {HWND window=nullptr;DWORD thread=0,owner=0;ULONG_PTR lifetime=0;bool lost=false;};
static std::shared_ptr<WndProcBinding> fixtureBinding;
static std::shared_ptr<WndProcBinding> WndBindingFind(HWND h){return fixtureBinding&&fixtureBinding->window==h?fixtureBinding:nullptr;}
static bool WndBindingLive(const std::shared_ptr<WndProcBinding>& binding){if(!binding||binding->lost)return false;DWORD owner=0;if(GetWindowThreadProcessId(binding->window,&owner)!=binding->thread||owner!=binding->owner||(ULONG_PTR)GetPropW(binding->window,kLifetime)!=binding->lifetime)binding->lost=true;return !binding->lost;}
static HWND RealForeground(){return GetForegroundWindow();}
static bool ForegroundBelongsToTarget(HWND h){DWORD owner=0;return h&&GetWindowThreadProcessId(h,&owner)&&owner==g_pid;}
static BOOL(WINAPI* originalPosition)(HWND,HWND,int,int,int,int,UINT)=nullptr;
static BOOL(WINAPI* originalShow)(HWND,int)=nullptr;
static thread_local unsigned writerToken=0;
static std::atomic<unsigned long long> allowedSetup{0},allowedPolicy{0},absorbed{0};
static bool Owned(HWND h){DWORD owner=0;RECT rect{};return h&&(h==primary||h==related)&&GetWindowThreadProcessId(h,&owner)&&owner==GetCurrentProcessId()&&GetWindowRect(h,&rect)&&rect.right<-20000;}
static constexpr UINT SafeFlags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
static BOOL WINAPI GuardPosition(HWND h,HWND after,int x,int y,int width,int height,UINT flags){
 if(writerToken&&Owned(h)&&(flags&SafeFlags)==SafeFlags&&!(flags&(SWP_SHOWWINDOW|SWP_HIDEWINDOW|SWP_ASYNCWINDOWPOS|SWP_NOZORDER))&&((writerToken==1&&(after==HWND_TOPMOST||after==HWND_BOTTOM))||(writerToken==2&&h==primary&&after==HWND_BOTTOM))){if(writerToken==1)++allowedSetup;else ++allowedPolicy;return originalPosition(h,after,x,y,width,height,flags);}
 ++absorbed;SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
static BOOL WINAPI GuardShow(HWND h,int command){if(writerToken==1&&Owned(h)&&command==SW_SHOWNOACTIVATE)return originalShow(h,command);++absorbed;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI GuardLock(UINT){++absorbed;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static void WINAPI GuardKeyboard(BYTE,BYTE,DWORD,ULONG_PTR){++absorbed;}
static void* extraEntries[5]{};static void* extraDetours[5]{};
static bool ExtraGuards(){const char* names[]={"SetWindowPos","ShowWindow","ShowWindowAsync","LockSetForegroundWindow","keybd_event"};void* detours[]={(void*)&GuardPosition,(void*)&GuardShow,(void*)&GuardShow,(void*)&GuardLock,(void*)&GuardKeyboard};for(unsigned i=0;i<5;++i){extraEntries[i]=(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),names[i]);extraDetours[i]=detours[i];if(!extraEntries[i])return false;bool alias=false;for(unsigned j=0;j<i;++j)if(extraEntries[j]==extraEntries[i]){if(extraDetours[j]!=extraDetours[i])return false;alias=true;}if(!alias){void* original=nullptr;if(VIMH_CreateHook(extraEntries[i],detours[i],&original)!=MH_OK||!original||VIMH_EnableHook(extraEntries[i])!=MH_OK)return false;if(i==0)originalPosition=(decltype(originalPosition))original;if(i==1)originalShow=(decltype(originalShow))original;}if(!vi_cursor_spy::Reaches(extraEntries[i],extraDetours[i]))return false;}return originalPosition&&originalShow;}
static bool VerifyExtra(){for(unsigned i=0;i<5;++i)if(!vi_cursor_spy::Reaches(extraEntries[i],extraDetours[i]))return false;return true;}
static BOOL FixturePosition(HWND h,HWND after){writerToken=1;const auto result=SetWindowPos(h,after,0,0,0,0,SafeFlags);writerToken=0;return result;}
static BOOL WINAPI PolicyPosition(HWND h,HWND after,int x,int y,int width,int height,UINT flags){writerToken=2;const auto result=SetWindowPos(h,after,x,y,width,height,flags);writerToken=0;return result;}
static vi_topmost::Position o_SetWindowPos=PolicyPosition;
#include "vi_topmost_native.inl"
static std::string JEsc(const std::string& value){std::string result;for(unsigned char ch:value){if(ch=='"'||ch=='\\')result+='\\';result+=(char)ch;}return result;}
#include "actual_topmost_jout.inl"
#include "vi_topmost_json.inl"
static std::string Status(){JOut output;TopmostFields(output);return output.done();}
static bool IsTopmost(HWND h){bool result=false;return vi_topmost::NativeTopmost(nullptr,h,result)&&result;}
static void Observe(){
#ifndef VI_TOPMOST_OMIT_POLICY
 TopmostObserve();
#endif
}
static unsigned timers=0;static bool timerIsolation=true;
static LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_TIMER&&h==primary&&w==91){const HWND before=RealForeground();const bool set=FixturePosition(primary,HWND_TOPMOST)!=FALSE;const bool positive=IsTopmost(primary);Observe();timerIsolation&=set&&positive&&!IsTopmost(primary)&&RealForeground()==before&&!ForegroundBelongsToTarget(RealForeground());if(++timers>=3)KillTimer(h,91);return 0;}return DefWindowProcW(h,message,w,l);}
struct Model {
 vi_topmost::Scope scope{(HWND)101,77,3,99,2};bool current=true,background=true,top=true,style=true,fail=false,stayTop=false,changeAfter=false,nested=false,postChanged=false;unsigned reads=0,census=0,calls=0,invalidAt=0;HWND after=nullptr;UINT flags=0;int x=0,y=0,width=0,height=0;vi_topmost::Policy* policy=nullptr;
 vi_topmost::Relations first{true,"same_process_relationships_confirmed",{{(HWND)101,nullptr,77,3}}},second=first,post=first;
};
static Model* model=nullptr;
static bool ModelCurrent(void* context,const vi_topmost::Scope& scope){auto& m=*(Model*)context;return m.current&&vi_topmost::Same(scope,m.scope)&&(!m.invalidAt||++m.reads<m.invalidAt);}
static bool ModelBackground(void* context,const vi_topmost::Scope&){return ((Model*)context)->background;}
static bool ModelStyle(void* context,HWND,bool& value){auto& m=*(Model*)context;value=m.top;return m.style;}
static vi_topmost::Relations ModelRelations(void* context,const vi_topmost::Scope&){auto& m=*(Model*)context;const auto ordinal=m.census++;return ordinal==0?m.first:ordinal==1||!m.postChanged?m.second:m.post;}
static vi_topmost::Callbacks ModelCallbacks(Model& m);
static BOOL WINAPI ModelPosition(HWND h,HWND after,int x,int y,int width,int height,UINT flags){auto& m=*model;++m.calls;m.after=after;m.flags=flags;m.x=x;m.y=y;m.width=width;m.height=height;if(h!=m.scope.window)return FALSE;if(m.nested&&m.policy)m.policy->Tick(m.scope,false,ModelCallbacks(m));if(m.changeAfter)m.current=false;if(m.fail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(!m.stayTop)m.top=false;return TRUE;}
static vi_topmost::Callbacks ModelCallbacks(Model& m){model=&m;return {&m,ModelCurrent,ModelBackground,ModelRelations,ModelStyle,ModelPosition};}
static void Models(){
 using namespace vi_topmost;
 const Scope s{(HWND)101,77,3,99,2};const Node root{(HWND)101,nullptr,77,3},child{(HWND)102,(HWND)101,77,4},foreign{(HWND)103,(HWND)102,88,5};
 Check(SelectRelations(s,{root},true).complete,"complete ownerless relationship control qualifies");
 auto group=SelectRelations(s,{root,child},true);Check(group.complete&&group.members.size()==2,"same-PID owner and owned component qualifies");
 auto r=SelectRelations(s,{root,child,foreign},true);Check(!r.complete&&r.reason=="foreign_owner_or_owned_window","foreign descendant refuses the whole induced component");
 auto ownedRoot=root;ownedRoot.owner=(HWND)102;Check(SelectRelations(s,{ownedRoot,{(HWND)102,nullptr,77,4}},true).complete,"same-PID parent owner qualifies");
 Check(!SelectRelations(s,{ownedRoot},true).complete,"owner missing from complete census refuses");
 Check(!SelectRelations(s,{root},false).complete,"incomplete desktop enumeration refuses");
 Check(!SelectRelations(s,{root,root},true).complete,"duplicate HWND census refuses");
 Check(!SelectRelations(s,{ownedRoot,{(HWND)102,(HWND)101,77,4}},true).complete,"ownership cycle refuses");
 std::vector<Node> capacity{root};for(unsigned i=0;i<RelatedLimit;++i)capacity.push_back({(HWND)(std::uintptr_t)(200+i),root.window,77,3});r=SelectRelations(s,capacity,true);Check(!r.complete&&r.reason=="relationship_component_capacity","induced component capacity refuses without truncation success");
 capacity.assign(DesktopLimit+1,root);Check(!SelectRelations(s,capacity,true).complete,"desktop census hard capacity refuses");
 {Policy p;Model m;m.scope=s;const auto out=p.Tick(s,false,ModelCallbacks(m));Check(out.attempted&&out.verified&&m.calls==1&&m.after==HWND_BOTTOM&&(m.flags&SafeFlags)==SafeFlags&&!(m.flags&SWP_NOZORDER),"model demotion uses one nonactivating HWND_BOTTOM call");}
 {Policy p;Model m;m.background=false;auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&p.Snapshot().reason=="foreground_or_policy_ineligible","foreground standdown performs no native mutation");}
 {Policy p;Model m;auto out=p.Tick(s,true,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&m.top&&p.Snapshot().currentKnown&&p.Snapshot().reason=="topmost_keep","keep reports style without stripping or restoring");}
 {Policy p;Model m;m.current=false;auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0,"foreign stale or unretained adopted identity refuses");}
 {Policy p;Model m;m.style=false;auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&p.Snapshot().reason=="topmost_style_unavailable","failed style query refuses");}
 {Policy p;Model m;m.first={false,"foreign_owner_or_owned_window",{}};auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&p.Snapshot().reason=="foreign_owner_or_owned_window","foreign relationship census never reaches native writer");}
 {Policy p;Model m;m.second.members.push_back(child);auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&p.Snapshot().reason=="relationship_census_changed","changed complete second census refuses");}
 {Policy p;Model m;m.invalidAt=3;auto out=p.Tick(s,false,ModelCallbacks(m));Check(!out.attempted&&m.calls==0&&p.Snapshot().reason=="scope_changed_before_demotion","last pre-mutation scope check prevents stale write");}
 {Policy p;Model m;m.fail=true;auto out=p.Tick(s,false,ModelCallbacks(m));const auto state=p.Snapshot();Check(out.attempted&&!out.verified&&m.calls==1&&!state.strippedEver&&state.reason=="demotion_failed"&&state.error==ERROR_ACCESS_DENIED,"native failure stays failed with one call and exact setter error");}
 {Policy p;Model m;m.stayTop=true;auto out=p.Tick(s,false,ModelCallbacks(m));Check(out.attempted&&!out.verified&&m.calls==1&&!p.Snapshot().strippedEver&&p.Snapshot().reason=="still_topmost_after_demotion","successful BOOL alone does not claim observed strip");}
 {Policy p;Model m;m.changeAfter=true;auto out=p.Tick(s,false,ModelCallbacks(m));Check(out.attempted&&!out.verified&&m.calls==1&&!p.Snapshot().currentKnown&&p.Snapshot().reason=="scope_changed_after_demotion","post-native scope change stays unknown with no replay");}
 {Policy p;Model m;m.postChanged=true;m.post.members.push_back(child);auto out=p.Tick(s,false,ModelCallbacks(m));Check(out.attempted&&!out.verified&&m.calls==1&&!p.Snapshot().currentKnown&&p.Snapshot().reason=="relationship_census_changed_after_demotion","native callback ownership change stays unknown without retry or restoration");}
 {Policy p;Model m;m.nested=true;m.policy=&p;auto out=p.Tick(s,false,ModelCallbacks(m));Check(out.verified&&m.calls==1&&!p.Snapshot().busy,"reentrant observation cannot issue a second writer or deadlock");}
 {Policy p;Model m;Request q{HWND_TOPMOST,11,17,320,240,0};auto out=p.Reassert(s,false,ModelCallbacks(m),q);Check(out.handled&&out.verified&&m.calls==1&&m.x==11&&m.y==17&&m.width==320&&m.height==240&&m.flags==(SWP_NOACTIVATE|SWP_NOOWNERZORDER),"intercepted reassert preserves caller geometry while enforcing nonactivation");q.flags=SWP_NOZORDER;out=p.Reassert(s,false,ModelCallbacks(m),q);Check(!out.handled&&m.calls==1,"NOZORDER request is never miscounted or rewritten as reassertion");}
 {Policy p;Model m;p.Tick(s,false,ModelCallbacks(m));m.top=true;m.census=0;p.Tick(s,false,ModelCallbacks(m));const auto before=p.Snapshot();auto fresh=s;++fresh.adoption;m.scope=fresh;m.top=false;p.Tick(fresh,false,ModelCallbacks(m));const auto after=p.Snapshot();Check(before.demotions==2&&before.reapplications==1&&after.demotions==0&&after.reapplications==0&&!after.strippedEver,"counters belong to the current adopted generation and reset on retarget");}
}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);g_pid=GetCurrentProcessId();
 const HWND foreground=GetForegroundWindow();POINT cursorBefore{};GetCursorPos(&cursorBefore);
 const bool safe=vi_cursor_spy::Install()&&vi_cursor_spy::VerifyBeforeProduct()&&ExtraGuards()&&VerifyExtra();Check(safe,"all 19 desktop writer boundaries are terminal before creating owned windows");if(!safe)return 2;
 Check(vi_cursor_spy::Probe(),"terminal physical writer positive controls execute only after guard verification");
 SetForegroundWindow(GetDesktopWindow());BringWindowToTop(GetDesktopWindow());SetActiveWindow(GetDesktopWindow());SetFocus(GetDesktopWindow());SwitchToThisWindow(GetDesktopWindow(),TRUE);AttachThreadInput(GetCurrentThreadId(),GetCurrentThreadId()+1,TRUE);SetCapture(GetDesktopWindow());ReleaseCapture();AllowSetForegroundWindow(ASFW_ANY);SetWindowPos(GetDesktopWindow(),HWND_TOPMOST,0,0,0,0,0);ShowWindow(GetDesktopWindow(),SW_SHOW);ShowWindowAsync(GetDesktopWindow(),SW_SHOW);LockSetForegroundWindow(LSFW_LOCK);keybd_event(VK_F8,0,0,0);
 Check(vi_cursor_spy::Total()==6&&vi_cursor_spy::foregroundEffects==5&&vi_cursor_spy::queueEffects==1&&vi_cursor_spy::captureEffects==2&&vi_cursor_spy::permissionEffects==1&&absorbed==5,"each foreground capture permission queue layout and input writer is absorbed by its lower boundary");
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Window;wc.lpszClassName=L"VInputTopmostOwnedFixture11";if(!RegisterClassW(&wc))return 3;
 primary=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned offscreen topmost fixture",WS_POPUP,-28000,600,320,240,nullptr,nullptr,wc.hInstance,nullptr);Check(primary&&Owned(primary),"actual target is exact owned far-offscreen NOACTIVATE window");if(!primary)return 4;
 writerToken=1;ShowWindow(primary,SW_SHOWNOACTIVATE);writerToken=0;
 fixtureBinding=std::make_shared<WndProcBinding>();fixtureBinding->window=primary;fixtureBinding->thread=GetCurrentThreadId();fixtureBinding->owner=g_pid;fixtureBinding->lifetime=41;SetPropW(primary,kLifetime,(HANDLE)41);g_hwnd=primary;
 Check(FixturePosition(primary,HWND_TOPMOST)&&IsTopmost(primary),"red control observes genuine native WS_EX_TOPMOST before policy");
 const auto setupCount=allowedSetup.load();RECT before{},after{};GetWindowRect(primary,&before);Observe();GetWindowRect(primary,&after);auto status=Status();printf("TOPMOST status %s\n",status.c_str());
 Check(!IsTopmost(primary)&&EqualRect(&before,&after)&&allowedPolicy==1,"actual periodic policy demotes once while preserving window rectangle");
 Check(Has(status,"\"topmost_stripped\":true")&&Has(status,"\"demotions_observed\":\"1\"")&&Has(status,"\"current_topmost\":false"),"actual nested status confirms observed strip within current binding");
 Observe();Check(allowedPolicy==1&&allowedSetup==setupCount,"already non-topmost target performs no z-order call");
 SetTimer(primary,91,10,nullptr);const auto until=GetTickCount64()+1500;while(timers<3&&GetTickCount64()<until){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}KillTimer(primary,91);
 const auto timerState=g_topmostPolicy.Snapshot();Check(timers==3&&timerIsolation&&!IsTopmost(primary)&&timerState.demotions==4&&timerState.reapplications==3,"three finite timer reapplications are counted and each ends non-topmost without activation");
 FixturePosition(primary,HWND_TOPMOST);g_cfg.topmostKeep=true;const auto callsBeforeKeep=allowedPolicy.load();Observe();status=Status();Check(IsTopmost(primary)&&allowedPolicy==callsBeforeKeep&&Has(status,"\"policy\":\"keep\"")&&Has(status,"\"topmost_stripped\":false"),"keep opt-out leaves actual topmost state and reports it honestly");
 g_cfg.topmostKeep=false;Observe();Check(!IsTopmost(primary),"re-enabling strip demotes without restoring any prior z-order");
 BOOL native=FALSE;const auto callsBeforeRedirect=allowedPolicy.load();const auto handled=TopmostSetWindowPos(primary,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE,native);Check(handled&&native&&!IsTopmost(primary)&&allowedPolicy==callsBeforeRedirect+1&&g_topmostPolicy.Snapshot().preventedRequests==1,"actual reassert interception routes exact adopted window to one safe bottom call");
 const auto notHandled=TopmostSetWindowPos(primary,HWND_TOPMOST,0,0,0,0,SWP_NOZORDER,native);Check(!notHandled&&allowedPolicy==callsBeforeRedirect+1,"actual NOZORDER path remains untouched");
 related=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Owned offscreen related dialog",WS_POPUP,-28000,600,240,180,primary,nullptr,wc.hInstance,nullptr);Check(related&&GetWindow(related,GW_OWNER)==primary&&Owned(related),"actual same-PID owned-dialog relationship is established offscreen");
 FixturePosition(primary,HWND_TOPMOST);const bool relatedWasNormal=!IsTopmost(related);Observe();Check(relatedWasNormal&&!IsTopmost(primary)&&!IsTopmost(related),"demoting adopted target never raises a normal same-PID related window");
 FixturePosition(related,HWND_TOPMOST);FixturePosition(primary,HWND_TOPMOST);const bool bothTop=IsTopmost(primary)&&IsTopmost(related);vi_topmost::Scope actualScope;TopmostContext actualContext;TopmostContextTake(actualContext,actualScope);const auto actualRelations=vi_topmost::NativeRelations(actualScope);bool containsPrimary=false,containsRelated=false,allSamePid=true;for(const auto& node:actualRelations.members){containsPrimary|=node.window==primary;containsRelated|=node.window==related;allSamePid&=node.pid==g_pid;}Observe();const auto relatedState=g_topmostPolicy.Snapshot();printf("TOPMOST relationships {\"complete\":%s,\"members\":%u,\"policy_members\":%u,\"both_topmost_before\":%s,\"primary_topmost_after\":%s,\"related_topmost_after\":%s}\n",actualRelations.complete?"true":"false",(unsigned)actualRelations.members.size(),relatedState.relationshipCount,bothTop?"true":"false",IsTopmost(primary)?"true":"false",IsTopmost(related)?"true":"false");Check(actualRelations.complete&&containsPrimary&&containsRelated&&allSamePid&&bothTop&&!IsTopmost(primary)&&IsTopmost(related)&&relatedState.relationshipCount==actualRelations.members.size(),"complete actual same-PID relationships permit adopted-HWND demotion without targeting independent topmost dialog");
 const auto callsBeforeOther=allowedPolicy.load();Check(!TopmostSetWindowPos(related,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE,native)&&allowedPolicy==callsBeforeOther,"unadopted same-PID dialog is never itself rewritten");
 ++g_inputReadyGeneration;status=Status();Check(Has(status,"\"demotions_observed\":\"0\"")&&Has(status,"awaiting_current_scope_observation")&&!Has(status,"\"topmost_stripped\":true"),"status cannot attach old per-window counters to a new adopted generation");Observe();Check(g_topmostPolicy.Snapshot().demotions==0&&g_topmostPolicy.Snapshot().reapplications==0,"new actual adoption generation resets policy counters");
 const auto callsBeforeStale=allowedPolicy.load();SetPropW(primary,kLifetime,(HANDLE)42);Observe();Check(allowedPolicy==callsBeforeStale&&Has(Status(),"\"current_topmost\":null"),"changed actual binding lifetime refuses before any native mutation");SetPropW(primary,kLifetime,(HANDLE)41);Observe();Check(allowedPolicy==callsBeforeStale,"restamped numeric lifetime cannot revive a quarantined binding");
 Models();POINT cursorAfter{};GetCursorPos(&cursorAfter);Check(vi_cursor_spy::Total()==6,"only explicit absorbed probes reached cursor or input writers; native cursor positions are observational");Check(GetForegroundWindow()==foreground&&!ForegroundBelongsToTarget(GetForegroundWindow()),"original desktop foreground remains unchanged and fixture never owns it");Check(vi_cursor_spy::VerifyBeforeProduct()&&VerifyExtra(),"all terminal boundaries remain installed after callbacks and negative controls");
 printf("TOPMOST evidence {\"terminal_writer_guards\":19,\"foreground_before\":\"%llu\",\"foreground_after\":\"%llu\",\"setup_writes\":\"%llu\",\"policy_writes\":\"%llu\",\"timer_reassertions\":%u,\"real_game\":false,\"os_input\":false}\n",(unsigned long long)(ULONG_PTR)foreground,(unsigned long long)(ULONG_PTR)GetForegroundWindow(),allowedSetup.load(),allowedPolicy.load(),timers);
 if(related)DestroyWindow(related);if(primary)DestroyWindow(primary);printf("native topmost policy: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
