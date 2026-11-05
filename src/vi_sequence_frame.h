#pragma once
#include <cstdint>
namespace vi_sequence_frame {
enum Result { Waiting, Fire, Missed, Expired, Invalid };
inline Result Evaluate(std::uint64_t target,std::uint64_t current,std::uint64_t now,std::uint64_t deadline,std::uint64_t qpc,bool checkpoint){
 if(!target||!deadline)return Invalid;
 if(now>=deadline)return Expired;
 if(current>target)return Missed;
 if(current<target||!checkpoint)return Waiting;
 return qpc?Fire:Invalid;
}
}
