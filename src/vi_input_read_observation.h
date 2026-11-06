#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <vector>
namespace vi_input_reads {
constexpr unsigned channels=7;
struct Sample {bool valid=true;std::array<long long,channels> reads{},down{};long long messages=0;std::array<bool,channels> channelValid{{true,true,true,true,true,true,true}};};
struct Delta {bool valid=true,pollValid=true,messageValid=true;std::uint64_t reads=0,down=0,messages=0;std::array<std::uint64_t,channels> channelReads{},channelDown{};std::array<bool,channels> channelValid{{true,true,true,true,true,true,true}};};
inline bool Add(std::uint64_t& sum,std::uint64_t value){if(value>(std::numeric_limits<std::uint64_t>::max)()-sum)return false;sum+=value;return true;}
inline Delta Difference(const Sample& before,const Sample& after){
 Delta out;
 out.messageValid=before.messages>=0&&after.messages>=before.messages;
 if(out.messageValid)out.messages=(std::uint64_t)(after.messages-before.messages);
 for(unsigned n=0;n<channels;++n){if(!before.valid||!after.valid||!before.channelValid[n]||!after.channelValid[n]||before.reads[n]<0||before.down[n]<0||before.down[n]>before.reads[n]||after.reads[n]<before.reads[n]||after.down[n]<before.down[n]||after.down[n]>after.reads[n]||after.down[n]-before.down[n]>after.reads[n]-before.reads[n]){out.channelValid[n]=false;out.pollValid=false;continue;}
  out.channelReads[n]=(std::uint64_t)(after.reads[n]-before.reads[n]);out.channelDown[n]=(std::uint64_t)(after.down[n]-before.down[n]);
  if(!Add(out.reads,out.channelReads[n])||!Add(out.down,out.channelDown[n]))out.pollValid=false;}
 out.valid=out.pollValid&&out.messageValid;return out;
}
struct Alias {int vk=0;Sample before;Delta delta;};
struct Evidence {bool measured=false,aliasAmbiguous=false,pollMeasured=false,messageMeasured=false,pollObserved=false,messageObserved=false,observed=false;};
// A related modifier high can come from another pre-held side. Never promote it
// to an exact-VK return, or turn it into a claim that the modifier was unread.
inline Evidence KeyEvidence(const Delta& exact,const std::array<Delta,2>& aliases){
 bool aliasesValid=true,aliasHigh=false;
 for(const auto& alias:aliases){aliasesValid=aliasesValid&&alias.pollValid;for(unsigned n=0;n<channels;++n)aliasHigh=aliasHigh||(alias.channelValid[n]&&alias.channelDown[n]>0);}
 Evidence out;for(unsigned n=0;n<channels;++n)out.pollObserved=out.pollObserved||(exact.channelValid[n]&&exact.channelDown[n]>0);
 out.aliasAmbiguous=!out.pollObserved&&aliasHigh;
 out.messageObserved=exact.messageValid&&exact.messages>0;
 out.pollMeasured=out.pollObserved||(exact.pollValid&&aliasesValid&&!aliasHigh);out.messageMeasured=exact.messageValid;
 out.observed=out.pollObserved||out.messageObserved;
 out.measured=out.observed||(out.pollMeasured&&out.messageMeasured);return out;
}
struct Key {int vk=0;Sample before;Delta delta;std::array<Alias,2> aliases{};};
struct State {bool started=false,frozen=false,pad=false,valid=true;std::uint64_t began=0,ended=0,padReturns=0;std::vector<Key> keys;};
}
