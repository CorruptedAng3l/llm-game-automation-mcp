#pragma once
#include <cstdint>
#include <string>
#include <limits>
#include "vi_qpc.h"
namespace vi_sequence_anchor {
inline bool Parse(const std::string& text,std::uint64_t& value){
 if(text.empty()||text.size()>20||(text.size()>1&&text[0]=='0'))return false;value=0;
 for(char c:text){if(c<'0'||c>'9'||value>((std::numeric_limits<std::uint64_t>::max)()-(c-'0'))/10)return false;value=value*10+(c-'0');}return value!=0;
}
inline bool Current(std::uint64_t anchor,std::uint64_t frequency,std::uint64_t now,std::uint64_t nativeFrequency){
 return anchor&&now>=anchor&&frequency&&frequency==nativeFrequency&&now-anchor<=vi_qpc::Milliseconds(6000,frequency);
}
}
