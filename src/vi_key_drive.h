// Bounded digital keyboard intent, never vehicle speed/heading or analog steering.
#pragma once
#include "vi_keys.h"
#include "vi_key_pwm.h"
#include <array>
#include <sstream>
#include <vector>
namespace vi_key_drive {
inline int Resource(int key){return key==0x10||key==0xa0||key==0xa1?0x10:key==0x11||key==0xa2||key==0xa3?0x11:key==0x12||key==0xa4||key==0xa5?0x12:key;}
struct Bindings {std::vector<int> throttle,left,right,brake;};
inline bool ValidBindings(const Bindings& bindings,std::string& error){
 const std::vector<int>* roles[]={&bindings.throttle,&bindings.left,&bindings.right,&bindings.brake};std::vector<int> resources;
 for(unsigned i=0;i<4;++i){if(roles[i]->empty()||roles[i]->size()>(i==0?4u:1u)){error="throttle binding needs1..4 keys; left/right/brake bindings need one key each";return false;}
  for(int key:*roles[i]){const int resource=Resource(key);if(key<8||key>255){error="key_drive bindings must be keyboard keys, not mouse buttons";return false;}if(std::find(resources.begin(),resources.end(),resource)!=resources.end()){error="key_drive roles must use distinct canonical keyboard resources";return false;}resources.push_back(resource);}
 }return true;
}
struct Phase {unsigned at=0,ms=0;bool throttle=false,brake=false;int direction=0;vi_key_pwm::Spec pwm;std::uint64_t ordinal=0;};
struct Plan {Bindings bindings;std::vector<Phase> phases;std::vector<int> keys;unsigned duration=0,period=100,transitionBound=0;std::uint64_t boundaries=0;};
struct Sample {std::vector<int> desired;unsigned phase=0,next=0,boundary=0;std::uint64_t ordinal=0;bool finished=false;};
inline bool Milliseconds(const std::string& text,unsigned maximum,unsigned& value){value=0;if(text.empty())return false;for(unsigned char c:text){if(c<'0'||c>'9'||value>(maximum-(c-'0'))/10)return false;value=value*10+c-'0';}return value<=maximum;}
inline bool ParseBindings(const std::array<std::string,4>& names,Bindings& out,std::string& error){
 Bindings parsed;std::vector<int>* roles[]={&parsed.throttle,&parsed.left,&parsed.right,&parsed.brake};
 for(unsigned i=0;i<4;++i)*roles[i]=vi_keys::ParseCombo(names[i]);if(!ValidBindings(parsed,error))return false;out=std::move(parsed);return true;
}
inline bool Parse(const std::string& script,const Bindings& bindings,unsigned period,Plan& result,std::string& error){
 result={};error.clear();Plan plan;plan.bindings=bindings;plan.period=period;
 if(script.empty()||script.size()>32768||script.find('\0')!=std::string::npos){error="key_drive script needs1..32768 bytes without NUL";return false;}
 if(!ValidBindings(bindings,error))return false;
 std::istringstream lines(script);std::string line;unsigned number=0;
 auto add=[&](const std::vector<int>& keys){for(int key:keys)if(std::find(plan.keys.begin(),plan.keys.end(),key)==plan.keys.end())plan.keys.push_back(key);};
 while(std::getline(lines,line)){++number;const auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);std::istringstream fields(line);std::string cmd,ms,throttle,steer,brake,extra;if(!(fields>>cmd))continue;
  auto fail=[&](const std::string& why){error="key_drive line "+std::to_string(number)+": "+why+"; no input was injected";return false;};
  if(cmd!="drive"||!(fields>>ms>>throttle>>steer>>brake)||(fields>>extra))return fail("use drive MS THROTTLE(0|1) STEER(-1..1) BRAKE(0|1)");
  Phase phase;double duty=0;if(!Milliseconds(ms,60000,phase.ms)||phase.ms<10||!vi_key_pwm::Number(steer,-1,1,duty)||(throttle!="0"&&throttle!="1")||(brake!="0"&&brake!="1"))return fail("invalid finite duration, steer or digital throttle/brake value");
  if(!vi_key_pwm::Make(std::abs(duty),period,phase.pwm,error))return fail(error);
  phase.at=plan.duration;phase.throttle=throttle=="1";phase.brake=brake=="1";phase.direction=phase.pwm.on?(duty<0?-1:1):0;phase.ordinal=plan.boundaries;
  if(plan.duration>60000-phase.ms||plan.phases.size()>=128)return fail("plan exceeds60000ms or128 phases");
  const unsigned held=static_cast<unsigned>((phase.throttle?bindings.throttle.size():0)+(phase.brake?bindings.brake.size():0));
  const unsigned transitions=held*2+vi_key_pwm::TransitionBound(phase.pwm,phase.ms,phase.direction?1:0);
  if(transitions>4096||plan.transitionBound>4096-transitions)return fail("plan exceeds4096 potential transitions");
  if(phase.throttle)add(bindings.throttle);if(phase.brake)add(bindings.brake);if(phase.direction)add(phase.direction<0?bindings.left:bindings.right);
  plan.transitionBound+=transitions;plan.duration+=phase.ms;plan.boundaries+=vi_key_pwm::At(phase.pwm,phase.ms,phase.ms).ordinal;plan.phases.push_back(phase);
 }
 if(plan.phases.empty()){error="key_drive needs at least one finite phase";return false;}
 result=std::move(plan);return true;
}
inline Sample At(const Plan& plan,unsigned elapsed){
 Sample out;out.finished=elapsed>=plan.duration;out.next=out.boundary=plan.duration;out.ordinal=plan.boundaries;
 if(out.finished){out.phase=(unsigned)plan.phases.size();return out;}
 for(unsigned i=0;i<plan.phases.size();++i){const auto& phase=plan.phases[i];if(elapsed>=phase.at+phase.ms)continue;
  const auto pulse=vi_key_pwm::At(phase.pwm,elapsed-phase.at,phase.ms);out.phase=i;out.next=phase.at+pulse.next;out.boundary=phase.at+pulse.boundary;out.ordinal=phase.ordinal+pulse.ordinal;
  if(phase.throttle)out.desired.insert(out.desired.end(),plan.bindings.throttle.begin(),plan.bindings.throttle.end());if(phase.brake)out.desired.insert(out.desired.end(),plan.bindings.brake.begin(),plan.bindings.brake.end());
  if(pulse.down&&phase.direction){const auto& keys=phase.direction<0?plan.bindings.left:plan.bindings.right;out.desired.insert(out.desired.end(),keys.begin(),keys.end());}return out;
 }return out;
}
}
