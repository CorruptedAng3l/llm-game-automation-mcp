// Extracted actual motion branch with private publication/sleep/scope callbacks.
// Own worker threads probe only the private mutex; no GUI/input API is called.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "vi_capture_fence.h"
using DWORD=std::uint32_t;using LONG=long;struct POINT{LONG x,y;};
struct JV{int t=0;std::string s;double n=0;bool b=false;};using JObj=std::map<std::string,JV>;
bool JHas(const JObj& j,const char* k){return j.find(k)!=j.end();}std::string JStr(const JObj& j,const char* k,const std::string& d=""){auto i=j.find(k);return i!=j.end()&&i->second.t==1?i->second.s:d;}
double JNum(const JObj& j,const char* k,double d=0){auto i=j.find(k);return i!=j.end()&&i->second.t==2?i->second.n:d;}bool JBool(const JObj& j,const char* k,bool d=false){auto i=j.find(k);return i!=j.end()&&i->second.t==3?i->second.b:d;}
struct JOut{std::string text="{";void field(const char* k,const std::string& v){if(text.size()>1)text+=',';text+='"'+std::string(k)+"\":"+v;}void bol(const char* k,bool b){field(k,b?"true":"false");}void num(const char* k,double n){field(k,std::to_string(n));}void str(const char* k,const std::string& v){field(k,'"'+v+'"');}std::string done(){return text+'}';}};
std::string ErrJ(const std::string& e){JOut j;j.bol("ok",false);j.str("error",e);return j.done();}
struct CaptureScope{std::string token;};std::recursive_mutex g_inputMx;std::string currentToken="{\"native\":1}";bool owner=false,valid=true;DWORD clockMs=1000;int sleepMutation=0;bool waitUnlocked=true,publicationLocked=true;unsigned waits=0;std::vector<POINT> writes;
bool OwnerCursorActive(){return owner;}bool CaptureScopeTake(CaptureScope& s){s.token=currentToken;return valid;}std::string CaptureScopeJson(const CaptureScope& s){return s.token;}
namespace vi_qpc{std::uint64_t Now(){return clockMs;}std::uint64_t Frequency(){return 1000;}}
namespace vi_sequence_anchor{bool Parse(const std::string& s,std::uint64_t& v){auto p=std::from_chars(s.data(),s.data()+s.size(),v);return p.ec==std::errc{}&&p.ptr==s.data()+s.size()&&v;}}
#include "vi_input_deadline.inl"
#include "actual_pointer_admit.inl"
struct{long present_ours=0;}C;struct{int cursor=0,edgeMargin=0;}g_cfg;void* g_hwnd=reinterpret_cast<void*>(1);long g_relativeScopeRefused=0;LONG g_vcurX=0,g_vcurY=0;
bool OtherThreadCanLock(){bool available=false;std::thread probe([&](){available=g_inputMx.try_lock();if(available)g_inputMx.unlock();});probe.join();return available;}
DWORD GetTickCount(){return clockMs;}void Sleep(DWORD ms){waitUnlocked=waitUnlocked&&OtherThreadCanLock();++waits;clockMs+=ms;C.present_ours+=ms;if(sleepMutation==1)currentToken="{\"native\":2}";if(sleepMutation==2)owner=true;}
void SleepMs(double ms){if(ms>0)Sleep((DWORD)std::ceil(ms));}
struct PointerPath{CaptureScope scope;bool Init(LONG,LONG,bool=false){return CaptureScopeTake(scope);}bool Valid(){return valid&&scope.token==currentToken;}};
struct MotionAccounting{void Add(const MotionAccounting&) {}};struct AppliedMouse{long long dx=0,dy=0;MotionAccounting accounting;};
AppliedMouse MouseMoveTo(LONG x,LONG y,bool relative,LONG dx,LONG dy,bool=false,PointerPath* =nullptr,const POINT* =nullptr){publicationLocked=publicationLocked&&!OtherThreadCanLock();const LONG oldX=g_vcurX,oldY=g_vcurY;if(relative){g_vcurX+=dx;g_vcurY+=dy;}else{g_vcurX=x;g_vcurY=y;}writes.push_back({g_vcurX,g_vcurY});return {(long long)g_vcurX-oldX,(long long)g_vcurY-oldY,{}};}
bool ResolveXY(const JObj& j,LONG& x,LONG& y){x=(LONG)JNum(j,"x");y=(LONG)JNum(j,"y");return true;}bool CaptureMapCurrent(){return true;}bool UseVirtualCursor(){return true;}
void EdgeFields(JOut&,PointerPath* =nullptr){}void AccountingFields(JOut& j,const MotionAccounting&,long long,long long,unsigned n){j.num("publications",n);}
std::string Motion(const std::string& cmd,const JObj& j){
#include "actual_motion_branch.inl"
 return ErrJ("unsupported model command");
}
void Reset(){clockMs=1000;owner=false;valid=true;currentToken="{\"native\":1}";sleepMutation=0;waitUnlocked=publicationLocked=true;waits=0;writes.clear();C.present_ours=0;g_vcurX=g_vcurY=0;}
JObj Absolute(){JObj j;j["expected_capture_scope"]={1,"{\"native\":1}"};j["x"]={2,{},30};j["y"]={2,{},40};j["steps"]={2,{},3};j["step_ms"]={2,{},10};return j;}
int main(){unsigned passed=0,failed=0;auto check=[&](bool b,const char* n){printf("%s %s\n",b?"PASS":"FAIL",n);b?++passed:++failed;};
 Reset();auto j=Absolute();auto r=Motion("mouse",j);check(r.find("\"ok\":true")!=std::string::npos&&writes.size()==3&&g_vcurX==30&&g_vcurY==40,"actual absolute branch publishes three intended steps");check(waits>0&&waitUnlocked&&publicationLocked,"input mutex is held only for publication and released across time waits");
 Reset();j=Absolute();j["frame_paced"]={3,{},0,true};C.present_ours=1;r=Motion("mouse",j);check(r.find("\"ok\":true")!=std::string::npos&&writes.size()==3&&waitUnlocked&&publicationLocked,"actual scoped frame-paced motion releases mutex during frame/time waits");
 Reset();j=Absolute();sleepMutation=1;r=Motion("mouse",j);check(writes.size()==1&&r.find("\"ok\":false")!=std::string::npos&&r.find("\"input_injected\":true")!=std::string::npos&&r.find("no input was injected")==std::string::npos,"scope change during wait preserves partial-input fields and honest error text");
 Reset();j=Absolute();sleepMutation=2;r=Motion("mouse",j);check(writes.size()==1&&r.find("\"ok\":false")!=std::string::npos,"user ownership during wait stops subsequent publications");
 Reset();j=Absolute();j["admit_before_qpc_ticks"]={1,"1012"};j["admit_qpc_frequency"]={1,"1000"};r=Motion("mouse",j);check(writes.size()==2&&r.find("\"ok\":false")!=std::string::npos&&r.find("\"input_injected\":true")!=std::string::npos,"deadline expiring after partial move cannot be reported as no input");
 Reset();j=Absolute();j["expected_capture_scope"]={1,"{}"};r=Motion("mouse",j);check(writes.empty()&&r.find("\"input_injected\":false")!=std::string::npos,"wrong initial scope refuses before the first publication");
 Reset();j={};j["expected_capture_scope"]={1,"{\"native\":1}"};j["dx"]={2,{},5};j["dy"]={2,{},-4};j["raw"]={3,{},0,true};r=Motion("move",j);check(writes.size()==1&&g_vcurX==5&&g_vcurY==-4&&r.find("\"ok\":true")!=std::string::npos,"legacy single raw-relative input keeps one checked publication");
 printf("pointer motion model: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
