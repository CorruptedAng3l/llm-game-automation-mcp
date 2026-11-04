// A bounded keyboard-only sequence. Parsing completes before any input changes.
#pragma once
#include "vi_keys.h"
#include <sstream>
#include <cstdint>
#include <utility>

namespace vi_key_sequence {
struct Step { std::vector<int> keys; unsigned ms=0; unsigned line=0; };
struct Plan { std::vector<Step> steps; std::vector<int> keys; unsigned duration=0,transitions=0; };
inline bool Parse(const std::string& script,Plan& result,std::string& error) {
 result={};error.clear();Plan plan;
 auto fail=[&](unsigned line,const char* why){error="sequence line "+std::to_string(line)+": "+why+"; no input was injected";return false;};
 if(script.empty()||script.size()>32768||script.find('\0')!=std::string::npos)return fail(0,"script must be1..32768 bytes without NUL");
 std::istringstream input(script);std::string text;unsigned line=0;
 while(std::getline(input,text)) {
  ++line;const auto comment=text.find('#');if(comment!=std::string::npos)text.resize(comment);
  std::istringstream fields(text);std::string cmd,key,ms,extra;if(!(fields>>cmd))continue;
  Step step;step.line=line;
  if(cmd=="sleep"||cmd=="wait") {if(!(fields>>ms)||(fields>>extra))return fail(line,"use wait MS");}
  else if(cmd=="key"||cmd=="hold") {if(!(fields>>key>>ms)||(fields>>extra))return fail(line,"use key CHORD MS or hold CHORD MS");step.keys=vi_keys::ParseCombo(key);if(step.keys.empty()||step.keys.size()>16)return fail(line,"unknown or oversized key chord");}
  else return fail(line,"only key, hold, wait and sleep are supported");
  unsigned value=0;if(ms.empty())return fail(line,"duration must be an integer0..120000");
  for(unsigned char c:ms){if(c<'0'||c>'9'||value>(120000u-(c-'0'))/10)return fail(line,"duration must be an integer0..120000");value=value*10+c-'0';}
  if(value>120000)return fail(line,"duration must be an integer0..120000");step.ms=value;
  // Modifier ordering costs15ms per extra key on both press and release.
  const unsigned duration=value+(step.keys.empty()?0:30u*static_cast<unsigned>(step.keys.size()-1));
  if(duration>118000||plan.duration>118000-duration)return fail(line,"declared sequence exceeds118000ms");
  plan.duration+=duration;plan.transitions+=static_cast<unsigned>(step.keys.size()*2);
  if(plan.steps.size()>=1024||plan.transitions>4096)return fail(line,"sequence exceeds1024 steps or4096 transitions");
  for(int vk:step.keys)if(std::find(plan.keys.begin(),plan.keys.end(),vk)==plan.keys.end())plan.keys.push_back(vk);
  plan.steps.push_back(std::move(step));
 }
 if(plan.steps.empty()||plan.keys.empty())return fail(0,"sequence needs at least one key step");
 result=std::move(plan);return true;
}
} // namespace vi_key_sequence
