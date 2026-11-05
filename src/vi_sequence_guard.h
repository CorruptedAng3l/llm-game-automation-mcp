#pragma once
#include <cstdint>
#include <limits>
namespace vi_sequence_guard {
enum class Verdict{Renewed,Disabled,Expired,SerialMismatch};
struct State{
 std::uint64_t interval=0,deadline=0,serial=0;
 bool Enabled()const{return interval!=0;}
 bool Expired(std::uint64_t now)const{return Enabled()&&now>=deadline;}
 void Start(std::uint64_t ms,std::uint64_t now){interval=ms;serial=0;deadline=ms>(std::numeric_limits<std::uint64_t>::max)()-now?(std::numeric_limits<std::uint64_t>::max)():now+ms;}
 Verdict Refresh(std::uint64_t now,std::uint64_t next){
  if(!Enabled())return Verdict::Disabled;if(Expired(now))return Verdict::Expired;
  if(serial==(std::numeric_limits<std::uint64_t>::max)()||next!=serial+1)return Verdict::SerialMismatch;
  serial=next;deadline=interval>(std::numeric_limits<std::uint64_t>::max)()-now?(std::numeric_limits<std::uint64_t>::max)():now+interval;return Verdict::Renewed;
 }
};
}
