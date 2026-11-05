#pragma once
#include <cstdint>
namespace vi_input_deadline {
inline bool Admissible(std::uint64_t deadline,std::uint64_t frequency,std::uint64_t now,std::uint64_t nativeFrequency){
 if(!now||!deadline||!frequency||frequency!=nativeFrequency||deadline<now)return false;
 const auto delta=deadline-now;return delta/frequency<120||(delta/frequency==120&&delta%frequency==0);
}
inline bool Expired(std::uint64_t deadline,std::uint64_t now){return deadline&&(!now||now>deadline);}
}
