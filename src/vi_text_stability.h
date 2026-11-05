#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>
namespace vi_text_stability {
inline bool Positive(const std::string& text,std::uint64_t& value){
 if(text.empty()||text.size()>20)return false;std::uint64_t result=0;
 for(char c:text){if(c<'0'||c>'9'||result>((std::numeric_limits<std::uint64_t>::max)()-(c-'0'))/10)return false;result=result*10+(c-'0');}
 if(!result)return false;value=result;return true;
}
struct Gate {
 unsigned requested=0,samples=0,anchor=0,observed=0;bool started=false,ready=false;
 std::array<int,4> box{};std::string scope;std::uint64_t anchorQpc=0,lastQpc=0,frequency=0;
 explicit Gate(unsigned ms):requested(ms){}
 void Reset(){samples=anchor=observed=0;started=ready=false;scope.clear();anchorQpc=lastQpc=frequency=0;}
 bool Observe(const std::array<int,4>& current,const std::string& currentScope,const std::string& qpcText,const std::string& frequencyText,unsigned now){
  std::uint64_t qpc=0,freq=0;
  if(requested<100||requested>3000||current[0]<0||current[1]<0||current[2]<1||current[3]<1||
     (long long)current[0]+current[2]>32768||(long long)current[1]+current[3]>32768||currentScope.empty()||
     !Positive(qpcText,qpc)||!Positive(frequencyText,freq))return false;
  const auto max=(std::numeric_limits<std::uint64_t>::max)(),whole=freq/1000,remainder=freq%1000;
  if(whole>max/requested)return false;const auto base=whole*requested,tail=(remainder*requested+999)/1000;
  if(base>max-tail)return false;const auto requiredTicks=base+tail;
  if(started&&(now<anchor||freq!=frequency||qpc<lastQpc))return false;
  if(!started||current!=box||currentScope!=scope){box=current;scope=currentScope;frequency=freq;anchorQpc=lastQpc=qpc;anchor=now;samples=1;observed=0;started=true;ready=false;return true;}
  if(qpc==lastQpc){ready=false;return true;} // one captured observation cannot supply two witnesses
  lastQpc=qpc;++samples;const auto span=qpc-anchorQpc;
  const long double milliseconds=(long double)span*1000/freq;
  observed=milliseconds>=(std::numeric_limits<unsigned>::max)()?(std::numeric_limits<unsigned>::max)():(unsigned)milliseconds;
  ready=samples>=2&&span>=requiredTicks;return true; // capture timestamps, not variable OCR processing time
 }
};
}
