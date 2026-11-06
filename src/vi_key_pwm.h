// Finite PWM policy shared by sequence steps, keyboard driving and radar steering.
// At() returns only the state that belongs NOW. Past edges are never replayed.
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
namespace vi_key_pwm {
struct Spec {unsigned period=100,on=0;double requested=0;};
struct Sample {bool down=false,finished=false;unsigned next=0,boundary=0;std::uint64_t ordinal=0;};
inline bool Number(const std::string& text,double low,double high,double& value){
 if(text.empty())return false;double parsed=0;const auto read=std::from_chars(text.data(),text.data()+text.size(),parsed);
 if(read.ec!=std::errc()||read.ptr!=text.data()+text.size()||!std::isfinite(parsed)||parsed<low||parsed>high)return false;value=parsed;return true;
}
inline bool Make(double duty,unsigned period,Spec& result,std::string& error){
 if(!std::isfinite(duty)||duty<0||duty>1||period<20||period>1000){error="PWM duty must be0..1 and period_ms20..1000";return false;}
 const auto on=(unsigned)std::llround(duty*period);
 if((duty>0&&!on)||(duty<1&&on==period)||(on&&on<10)||(on<period&&period-on<10)){error="quantized PWM on/off intervals must each be at least10ms (or duty exactly0/1)";return false;}
 result={period,on,duty};return true;
}
inline unsigned TransitionBound(const Spec& spec,unsigned duration,unsigned keys){
 return !spec.on?0:spec.on==spec.period?2*keys:2*keys*((duration+spec.period-1)/spec.period);
}
inline Sample At(const Spec& spec,unsigned elapsed,unsigned duration){
 Sample out;out.finished=elapsed>=duration;
 if(!spec.on||spec.on==spec.period){out.down=spec.on&&!out.finished;out.next=out.boundary=duration;out.ordinal=out.finished?1:0;if(!out.finished)out.boundary=0;return out;}
 const unsigned at=(std::min)(elapsed,duration),cycle=at/spec.period,within=at%spec.period;
 out.down=!out.finished&&within<spec.on;
 out.ordinal=(std::uint64_t)cycle*2+(within>=spec.on?1:0);
 out.boundary=out.finished?duration:cycle*spec.period+(within>=spec.on?spec.on:0);
 out.next=out.finished?duration:(std::min)(duration,cycle*spec.period+(within<spec.on?spec.on:spec.period));
 if(out.finished&&within)++out.ordinal;
 return out;
}
struct Cursor {
 bool initialized=false;unsigned elapsed=0;std::uint64_t ordinal=0,skipped=0;
 bool Observe(unsigned now,std::uint64_t boundary){
  if(initialized&&(now<elapsed||boundary<ordinal))return false;
  const auto difference=boundary-ordinal;
  skipped+=initialized?(difference>1?difference-1:0):difference;
  initialized=true;elapsed=now;ordinal=boundary;return true;
 }
};
}
