// Bounded empty-result OCR retries. The caller supplies one immutable captured image.
#pragma once
#include "vi_text.h"
#include <cwctype>

namespace vi_ocr_read {
struct Pass { double scale=1; std::string preprocessing="none"; };
struct Attempt { Pass pass; bool ok=false; size_t lines=0; std::string error; };
struct Result {
    bool ok=false; std::vector<vi_text::Line> lines; std::string language,error;
    std::vector<Attempt> attempts; size_t selected=0;
};
inline bool HasText(const std::vector<vi_text::Line>& lines) {
    for(const auto& line:lines)for(wchar_t c:line.text)if(!iswspace(c))return true;
    return false;
}
inline std::vector<Pass> Plan(double scale,bool explicitScale,const std::string& preprocessing,bool explicitPreprocess,bool retryEmpty) {
    std::vector<Pass> plan{{scale,preprocessing=="auto"?"contrast":preprocessing}};
    if(!retryEmpty)return plan;
    const auto add=[&](double s,const std::string& p){for(const auto& old:plan)if(old.scale==s&&old.preprocessing==p)return;plan.push_back({s,p});};
    if(!explicitScale&&scale<2)add(2,plan[0].preprocessing);
    const std::string mode=explicitPreprocess&&preprocessing!="auto"?preprocessing:"contrast";
    add(explicitScale?scale:(std::max)(2.0,scale),mode);
    if(!explicitScale)add((std::max)(3.0,scale),mode);
    return plan;
}
// Initial recognition errors remain errors. Optional retry errors are retained as
// evidence alongside the valid initial empty result; they never erase earlier text.
template<class Recognize> Result Read(const std::vector<Pass>& plan,Recognize recognize) {
    Result out;
    for(const auto& pass:plan){
        std::vector<vi_text::Line> lines;std::string lang,error;
        const bool ok=recognize(pass,lines,lang,error);
        out.attempts.push_back({pass,ok,lines.size(),error});
        if(!ok){if(out.attempts.size()==1){out.error=error;return out;}continue;}
        out.ok=true;out.lines=std::move(lines);out.language=std::move(lang);out.selected=out.attempts.size()-1;
        if(HasText(out.lines))break;
    }
    return out;
}
}
