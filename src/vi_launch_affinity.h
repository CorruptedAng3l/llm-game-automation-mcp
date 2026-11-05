#pragma once
#include <cstdint>
#include <limits>
#include <string>
inline bool ParseLaunchAffinity(const std::string& text,std::uint64_t maximum,std::uint64_t& mask){
    mask=0;if(text.size()<3||text[0]!='0'||(text[1]!='x'&&text[1]!='X'))return false;
    for(size_t i=2;i<text.size();++i){const char c=text[i];const unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;
        if(d>15||d>maximum||mask>(maximum-d)/16)return false;mask=mask*16+d;
    }
    return mask!=0;
}
