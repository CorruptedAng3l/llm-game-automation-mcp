// Bounded, conservative same-image number observations.
// Windows.Media.Ocr has no confidence; pass agreement is not confidence.
#pragma once
#include "vi_ocr_read.h"

namespace vi_ocr_numbers {
using Pass=vi_ocr_read::Pass;
struct Observation {
    Pass pass; bool ok=false; std::string language,error,reason;
    std::wstring text,value; vi_text::Box box{};
    std::vector<vi_text::Line> lines;
};
struct Result {
    bool ok=false,complete=true; std::string error,status="unknown",reason="no_numeric_text";
    std::wstring value; vi_text::Box box{}; size_t agreement=0,selected=0;
    std::vector<Observation> observations;
};
inline std::wstring Trim(std::wstring text) {
    const auto space=[](wchar_t c){return iswspace(c)!=0;};
    while(!text.empty()&&space(text.back()))text.pop_back();
    size_t first=0;while(first<text.size()&&space(text[first]))++first;
    return text.substr(first);
}
inline bool Digits(const std::wstring& text) {
    return !text.empty()&&text.size()<=6&&std::all_of(text.begin(),text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});
}
inline Observation Evaluate(const Pass& pass,bool ok,std::vector<vi_text::Line> lines,std::string language,std::string error,int width,int height) {
    Observation out;out.pass=pass;out.ok=ok;out.lines=std::move(lines);out.language=std::move(language);out.error=std::move(error);
    const vi_text::Line* selected=nullptr;size_t groups=0;
    for(const auto& line:out.lines){if(!out.text.empty())out.text+=L"\n";out.text+=line.text;if(!Trim(line.text).empty()){++groups;selected=&line;}}
    if(!ok){out.reason="engine_error";return out;}
    if(!selected){out.reason="empty";return out;}
    if(groups!=1){out.reason="multiple_numeric_groups";return out;}
    if(selected->words.size()!=1){out.reason="multiple_numeric_groups";return out;}
    const auto& word=selected->words[0];const auto token=Trim(word.text);
    if(token!=Trim(selected->text)||!Digits(token)){out.reason="non_numeric";return out;}
    // A glyph reaching a crop edge may have been clipped. Unknown is preferable
    // to silently interpreting a partial digit as a complete game count.
    if(width<=0||height<=0||word.box.x<=0||word.box.y<=0||word.box.w<=0||word.box.h<=0||
       (long long)word.box.x+word.box.w>=width||(long long)word.box.y+word.box.h>=height){out.reason="clipped";return out;}
    out.value=token;out.box=word.box;out.reason="eligible_numeric";return out;
}
inline std::vector<Pass> Plan(double scale,bool explicitScale,const std::string& mode,bool retry) {
    std::vector<Pass> out{{scale,mode=="auto"?"contrast":mode}};
    if(!retry)return out;
    const auto add=[&](double s,const std::string& p){for(const auto& old:out)if(old.scale==s&&old.preprocessing==p)return;out.push_back({s,p});};
    add(explicitScale?scale:3,"contrast");
    add(explicitScale?scale:2,"white_glyph");
    if(!explicitScale)add(4,"white_glyph");
    return out;
}
template<class Recognize> Result Read(const std::vector<Pass>& plan,int width,int height,Recognize recognize) {
    Result out;
    if(plan.empty()||plan.size()>4){out.error="digits plan must contain one to four passes";out.complete=false;return out;}
    for(size_t i=0;i<plan.size();++i)for(size_t j=0;j<i;++j)if(plan[i].scale==plan[j].scale&&plan[i].preprocessing==plan[j].preprocessing){out.error="digits plan must contain distinct passes";out.reason="duplicate_plan";out.complete=false;return out;}
    std::wstring candidate;bool conflict=false,multiple=false,clipped=false;
    for(const auto& pass:plan){
        std::vector<vi_text::Line> lines;std::string language,error;
        const bool ok=recognize(pass,lines,language,error);
        out.observations.push_back(Evaluate(pass,ok,std::move(lines),std::move(language),std::move(error),width,height));
        const auto& observation=out.observations.back();
        if(!ok){out.complete=false;if(out.observations.size()==1){out.error=observation.error;return out;}continue;}
        out.ok=true;
        multiple=multiple||observation.reason=="multiple_numeric_groups";clipped=clipped||observation.reason=="clipped";
        if(observation.value.empty())continue;
        if(candidate.empty()){candidate=observation.value;out.selected=out.observations.size()-1;out.box=observation.box;}
        if(candidate==observation.value)++out.agreement;else conflict=true;
    }
    if(conflict){out.status="ambiguous";out.reason="numeric_disagreement";return out;}
    if(!out.complete){out.reason="incomplete_engine_analysis";return out;}
    if(multiple){out.reason="multiple_numeric_groups";return out;}
    if(clipped){out.reason="clipped";return out;}
    if(out.agreement>=2){out.status="numeric_observed";out.reason="same_frame_pass_agreement";out.value=candidate;}
    else if(!candidate.empty())out.reason="insufficient_agreement";
    return out;
}
}
