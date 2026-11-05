// Production pointer admission helper, private finite scope/owner callbacks only.
#include <map>
#include <mutex>
#include <string>
#include <cstdio>
#include <charconv>
#include <cmath>
#include "vi_capture_fence.h"
struct JV{int t=0;std::string s;double n=0;bool b=false;};using JObj=std::map<std::string,JV>;
bool JHas(const JObj& j,const char* k){return j.find(k)!=j.end();}std::string JStr(const JObj& j,const char* k){auto i=j.find(k);return i!=j.end()&&i->second.t==1?i->second.s:"";}
double JNum(const JObj& j,const char* k,double d=0){auto i=j.find(k);return i!=j.end()&&i->second.t==2?i->second.n:d;}bool JBool(const JObj& j,const char* k,bool d=false){auto i=j.find(k);return i!=j.end()&&i->second.t==3?i->second.b:d;}
struct CaptureScope{int value=0;};std::recursive_mutex g_inputMx;bool owner=false,valid=true;std::string token="{\"native\":1}";
bool OwnerCursorActive(){return owner;}bool CaptureScopeTake(CaptureScope& s){s.value=1;return valid;}std::string CaptureScopeJson(const CaptureScope&){return token;}
namespace vi_qpc {std::uint64_t Now(){return 1000;}std::uint64_t Frequency(){return 1000;}}
namespace vi_sequence_anchor {bool Parse(const std::string& text,std::uint64_t& n){auto r=std::from_chars(text.data(),text.data()+text.size(),n);return r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&n;}}
#include "vi_input_deadline.inl"
#include "vi_pointer_scope.inl"
int main(){unsigned passed=0,failed=0;auto check=[&](bool b,const char* n){printf("%s %s\n",b?"PASS":"FAIL",n);b?++passed:++failed;};
 JObj j;j["expected_capture_scope"]={1,token};j["x"]={2,{},10};j["y"]={2,{},20};CaptureScope s;std::string error;
 check(PointerScopeAdmit("click",j,s,error),"one positioned click binds exact native scope");check(PointerScopeAdmit("mouse",j,s,error)&&PointerScopeAdmit("move",j,s,error),"one absolute motion binds exact native scope");
 auto bad=j;bad["expected_capture_scope"]={1,"{\"native\":2}"};check(!PointerScopeAdmit("click",bad,s,error),"stale caller scope refuses admission");
 bad=j;bad["expected_capture_scope"]={2,{},1};check(!PointerScopeAdmit("click",bad,s,error),"non-string scope refuses admission");bad=j;bad["expected_capture_scope"]={1,std::string(2049,'x')};check(!PointerScopeAdmit("click",bad,s,error),"oversized scope refuses admission");
 bad=j;bad["count"]={2,{},10};check(PointerScopeAdmit("click",bad,s,error),"ordinary scoped clicks preserve count1..10");bad["count"]={2,{},11};check(!PointerScopeAdmit("click",bad,s,error),"count beyond ordinary MCP cap refuses before input");check(PointerScopeAdmit("mousedown",j,s,error),"existing explicit positioned down can bind scope");check(PointerScopeAdmit("mouseup",j,s,error),"existing positioned up can bind scope");
 bad=j;bad["steps"]={2,{},200};check(PointerScopeAdmit("mouse",bad,s,error),"ordinary absolute motion preserves steps1..200");bad["steps"]={2,{},201};check(!PointerScopeAdmit("mouse",bad,s,error),"absolute steps beyond ordinary cap refuse input");bad=j;bad["steps"]={1,"2"};check(!PointerScopeAdmit("mouse",bad,s,error),"scoped move rejects untyped step count");bad=j;bad["frame_paced"]={3,{},0,true};check(PointerScopeAdmit("mouse",bad,s,error),"absolute frame pacing is scoped with unlocked waits");
 bad=j;bad.erase("y");check(!PointerScopeAdmit("click",bad,s,error),"both coordinates required before pointer admission");
 JObj raw;raw["expected_capture_scope"]={1,token};raw["raw"]={3,{},0,true};raw["dx"]={2,{},2};check(PointerScopeAdmit("move",raw,s,error),"legacy single raw-relative scope fence remains valid");raw["x"]={2,{},10};check(!PointerScopeAdmit("move",raw,s,error),"mixed absolute and raw coordinates refuse admission");
 raw.erase("x");raw["steps"]={2,{},2};check(!PointerScopeAdmit("move",raw,s,error),"raw-relative scope remains single-step");raw["steps"]={2,{},1};raw["frame_paced"]={3,{},0,true};check(!PointerScopeAdmit("move",raw,s,error),"raw-relative scope remains time-paced");
 owner=true;check(!PointerScopeAdmit("click",j,s,error),"active user ownership refuses scoped pointer input");owner=false;valid=false;check(!PointerScopeAdmit("click",j,s,error),"unknown current window scope refuses admission");valid=true;
 token="{\"native\":2}";check(!PointerScopeAdmit("click",j,s,error),"native scope changed between observations refuses admission");
 token="{\"native\":1}";j["admit_before_qpc_ticks"]={1,"2000"};j["admit_qpc_frequency"]={1,"1000"};check(PointerScopeAdmit("click",j,s,error),"same-clock near-future admission is accepted");j["admit_before_qpc_ticks"]={1,"999"};check(!PointerScopeAdmit("click",j,s,error),"expired admission refuses before hover");j["admit_before_qpc_ticks"]={1,"2000"};j["admit_qpc_frequency"]={1,"999"};check(!PointerScopeAdmit("click",j,s,error),"mismatched admission clock refuses input");j["admit_qpc_frequency"]={1,"1000"};j["admit_before_qpc_ticks"]={1,"121001"};check(!PointerScopeAdmit("click",j,s,error),"admission cannot be delayed more than120seconds");
 check(vi_input_deadline::Admissible(121000,1000,1000,1000)&&!vi_input_deadline::Expired(1000,1000)&&vi_input_deadline::Expired(1000,1001),"exact deadline and120second boundaries retain defined inclusive policy");
 j["admit_before_qpc_ticks"]={1,"2000"};owner=true;std::uint64_t deadline=0;check(!ReadInputDeadline(j,deadline,error)&&deadline==0,"bounded job/pointer admission refuses user ownership before native queue mutation");
 printf("pointer scope model: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
