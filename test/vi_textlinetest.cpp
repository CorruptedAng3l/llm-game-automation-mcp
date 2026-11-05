#include <vi_text.h>
#include <cstdio>
static unsigned passed=0,failed=0;
static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
static vi_text::Line Line(std::initializer_list<const wchar_t*> words,int y){vi_text::Line l;int x=10;for(auto text:words){vi_text::Word word{text,{x,y,(int)wcslen(text)*10,20}};if(!l.text.empty())l.text+=L' ';l.text+=text;l.box=l.words.empty()?word.box:vi_text::Union(l.box,word.box);l.words.push_back(word);x+=word.box.w+8;}return l;}
int main(){
    using vi_text::Find;using vi_text::Options;
    const std::vector<vi_text::Line> scene{Line({L"Click",L"Next",L"to",L"continue"},10),Line({L"Next"},100),Line({L"next"},200),Line({L"Player"},300)};
    Options legacy;legacy.whole_word=true;
    Check(Find(scene,L"Next",legacy).size()==3,"legacy whole-word mode still searches paragraph words");
    Options line;line.whole_word=true;
#ifdef VI_TEXT_LINE_MATCH
    line.exact_line=true;
#endif
    auto found=Find(scene,L"Next",line);
    Check(found.size()==2,"whole-line mode excludes Next embedded in paragraph");
    Check(found.size()==2&&found[0].line_text==L"Next"&&found[1].line_text==L"next","standalone alternatives preserve reading order and actual OCR line");
    Check(Find({scene[0]},L"Next",line).empty(),"paragraph-only hit is refused");
    Check(Find(scene,L"PLAY",line).empty(),"PLAY cannot select Player");
    Check(Find(scene,L" Click Next to continue ",line).size()==1,"complete multiword line retains whitespace normalization");
    line.exact=true;found=Find(scene,L"Next",line);
    Check(found.size()==1&&found[0].line_text==L"Next","case-sensitive whole-line mode keeps case contract");
    Check(Find(scene,L"next",line).size()==1,"case-sensitive lower-case standalone label remains eligible");
    line.exact=false;line.fuzzy=1;
    const std::vector<vi_text::Line> noisy{Line({L"Read",L"Nexf",L"carefully"},10),Line({L"Nexf"},100)};
    found=Find(noisy,L"Next",line);
    Check(found.size()==1&&found[0].line_text==L"Nexf"&&found[0].score<1,"fuzzy accepts a whole noisy label but not paragraph span");
    line.fuzzy=0;Check(Find(noisy,L"Next",line).empty(),"literal line mode does not invent an OCR correction");
    const std::vector<vi_text::Line> multi{Line({L"Please",L"Load",L"Game",L"now"},10),Line({L"Load",L"Game"},100)};
    found=Find(multi,L"load game",line);Check(found.size()==1&&found[0].first==0&&found[0].last==1,"whole-line multiword button spans exactly its complete words");
    Check(Find({scene[0]},L"Next",legacy).size()==1,"explicit line opt-out preserves intentional paragraph search");
    Check(Find(scene,L"",line).empty(),"empty query stays empty");
    printf("text line: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
