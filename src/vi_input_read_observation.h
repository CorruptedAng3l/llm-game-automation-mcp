#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <vector>
namespace vi_input_reads {
constexpr unsigned channels=7;
struct Sample {bool valid=true;std::array<long long,channels> reads{},down{};long long messages=0;};
struct Delta {bool valid=true;std::uint64_t reads=0,down=0,messages=0;std::array<std::uint64_t,channels> channelReads{},channelDown{};};
inline bool Add(std::uint64_t& sum,std::uint64_t value){if(value>(std::numeric_limits<std::uint64_t>::max)()-sum)return false;sum+=value;return true;}
inline Delta Difference(const Sample& before,const Sample& after){
 Delta out;
 if(!before.valid||!after.valid){out.valid=false;return out;}
 for(unsigned n=0;n<channels;++n){if(before.reads[n]<0||before.down[n]<0||before.down[n]>before.reads[n]||after.reads[n]<before.reads[n]||after.down[n]<before.down[n]||after.down[n]>after.reads[n]||after.down[n]-before.down[n]>after.reads[n]-before.reads[n]){out.valid=false;return out;}
  out.channelReads[n]=(std::uint64_t)(after.reads[n]-before.reads[n]);out.channelDown[n]=(std::uint64_t)(after.down[n]-before.down[n]);
  if(!Add(out.reads,out.channelReads[n])||!Add(out.down,out.channelDown[n])){out.valid=false;return out;}}
 if(before.messages<0||after.messages<before.messages)out.valid=false;else out.messages=(std::uint64_t)(after.messages-before.messages);return out;
}
struct Alias {int vk=0;Sample before;Delta delta;};
struct Evidence {bool measured=false,aliasAmbiguous=false;};
// A related modifier high can come from another pre-held side. Never promote it
// to an exact-VK return, or turn it into a claim that the modifier was unread.
inline Evidence KeyEvidence(const Delta& exact,const std::array<Delta,2>& aliases){
 bool aliasesValid=true,aliasHigh=false;
 for(const auto& alias:aliases){aliasesValid=aliasesValid&&alias.valid;aliasHigh=aliasHigh||(alias.valid&&alias.down>0);}
 return {exact.valid&&(exact.down>0||(aliasesValid&&!aliasHigh)),exact.valid&&exact.down==0&&aliasHigh};
}
struct Key {int vk=0;Sample before;Delta delta;std::array<Alias,2> aliases{};};
struct State {bool started=false,frozen=false,pad=false,valid=true;std::uint64_t began=0,ended=0,padReturns=0;std::vector<Key> keys;};
}
