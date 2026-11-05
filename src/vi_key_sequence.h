// A bounded keyboard-only sequence. Parsing completes before any input changes.
#pragma once
#include "vi_keys.h"
#include <sstream>
#include <cstdint>
#include <utility>

namespace vi_key_sequence {
struct Step { std::vector<int> keys; unsigned ms=0; unsigned line=0; unsigned at=0;int retapKey=0;unsigned every=0,gap=0,retaps=0; };
struct Plan { std::vector<Step> steps; std::vector<int> keys; unsigned duration=0,transitions=0; bool absolute=false,retap=false; };
inline bool Milliseconds(const std::string& text,unsigned maximum,unsigned& value){value=0;if(text.empty())return false;for(unsigned char c:text){if(c<'0'||c>'9'||value>(maximum-(c-'0'))/10)return false;value=value*10+c-'0';}return value<=maximum;}
inline bool Parse(const std::string& script,Plan& result,std::string& error) {
 result={};error.clear();Plan plan;
 auto fail=[&](unsigned line,const char* why){error="sequence line "+std::to_string(line)+": "+why+"; no input was injected";return false;};
 if(script.empty()||script.size()>32768||script.find('\0')!=std::string::npos)return fail(0,"script must be1..32768 bytes without NUL");
 std::istringstream input(script);std::string text;unsigned line=0;
 while(std::getline(input,text)) {
  ++line;const auto comment=text.find('#');if(comment!=std::string::npos)text.resize(comment);
  std::istringstream fields(text);std::string cmd,key,ms,extra;if(!(fields>>cmd))continue;
  Step step;step.line=line;
  bool explicitAt=false;unsigned at=0;
  if(cmd=="at"){std::string offset;if(!(fields>>offset>>cmd)||!Milliseconds(offset,118000,at))return fail(line,"use at OFFSET_MS key CHORD MS");explicitAt=true;plan.absolute=true;}
  if(cmd=="sleep"||cmd=="wait") {if(!(fields>>ms)||(fields>>extra))return fail(line,"use wait MS");}
  else if(cmd=="key"||cmd=="hold"||cmd=="retap") {
   if(cmd=="retap"){std::string target,every,gap;if(!(fields>>key>>target>>ms>>every>>gap)||(fields>>extra))return fail(line,"use retap CHORD KEY MS EVERY_MS GAP_MS");
    const auto tapped=vi_keys::ParseCombo(target);if(tapped.size()!=1)return fail(line,"retap target must be one key from the held chord");step.retapKey=tapped[0];
    if(!Milliseconds(every,120000,step.every)||!Milliseconds(gap,1000,step.gap)||!step.gap||step.every<=step.gap)return fail(line,"retap needs gap1..1000 strictly below every_ms");plan.retap=true;
   }else if(!(fields>>key>>ms)||(fields>>extra))return fail(line,"use key CHORD MS or hold CHORD MS");
   step.keys=vi_keys::ParseCombo(key);if(step.keys.empty()||step.keys.size()>16)return fail(line,"unknown or oversized key chord");
   if(step.retapKey&&std::find(step.keys.begin(),step.keys.end(),step.retapKey)==step.keys.end())return fail(line,"retap target must be one exact key from the held chord");
   if(step.retapKey){auto resource=[](int vk){return vk==0x10||vk==0xa0||vk==0xa1?0x10:vk==0x11||vk==0xa2||vk==0xa3?0x11:vk==0x12||vk==0xa4||vk==0xa5?0x12:vk;};
    for(size_t i=0;i<step.keys.size();++i)for(size_t j=0;j<i;++j)if(resource(step.keys[i])==resource(step.keys[j]))return fail(line,"retap chord cannot contain aliases of one modifier resource");}
  }else return fail(line,"only key, hold, retap, wait and sleep are supported");
  unsigned value=0;if(!Milliseconds(ms,120000,value))return fail(line,"duration must be an integer0..120000");step.ms=value;
  // Modifier ordering costs15ms per extra key on both press and release.
  const unsigned duration=value+(step.keys.empty()?0:30u*static_cast<unsigned>(step.keys.size()-1));
  if(explicitAt&&at<plan.duration)return fail(line,"absolute step overlaps a prior step (including modifier ordering)");
  step.at=explicitAt?at:plan.duration;
  if(duration>118000||step.at>118000-duration)return fail(line,"declared sequence exceeds118000ms");
  if(step.retapKey){step.retaps=value>step.gap?(value-step.gap-1)/step.every:0;if(!step.retaps)return fail(line,"retap needs at least one complete gap before the chord ends");}
  plan.duration=step.at+duration;plan.transitions+=static_cast<unsigned>(step.keys.size()*2)+2*step.retaps;
  if(plan.steps.size()>=1024||plan.transitions>4096)return fail(line,"sequence exceeds1024 steps or4096 transitions");
  for(int vk:step.keys)if(std::find(plan.keys.begin(),plan.keys.end(),vk)==plan.keys.end())plan.keys.push_back(vk);
  plan.steps.push_back(std::move(step));
 }
 if(plan.steps.empty())return fail(0,"sequence needs at least one step");
 result=std::move(plan);return true;
}
} // namespace vi_key_sequence
