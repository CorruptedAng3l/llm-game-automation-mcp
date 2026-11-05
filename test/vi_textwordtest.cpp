#include "vi_text.h"
#include <cstdio>
static unsigned passed=0,failed=0;
static void Check(bool good,const char* name){std::printf("%s %s\n",good?"PASS":"FAIL",name);if(good)++passed;else ++failed;}
// Old Options has no independent boundary setting: execute its actual unchanged
// legacy matcher, rather than transplanting the new predicate into the before.
template<class T>static auto Boundary(T& value,bool enabled,int)->decltype(value.whole_word=enabled,bool()){value.whole_word=enabled;return true;}
template<class T>static bool Boundary(T&,bool,...){return false;}
static vi_text::Line Line(std::initializer_list<const wchar_t*> words){vi_text::Line line;int x=0;for(auto word:words){vi_text::Word value;value.text=word;value.box={x,10,(int)value.text.size()*10,20};x+=value.box.w+5;if(!line.text.empty())line.text+=L' ';line.text+=word;line.words.push_back(value);}return line;}
int main(){
 vi_text::Options options;const std::vector<vi_text::Line> labels={Line({L"Player"}),Line({L"PLAY"}),Line({L"PLAYERS"}),Line({L"Play"})};
 Check(vi_text::Find(labels,L"play",options).size()==4,"legacy substring default preserved");
 Check(Boundary(options,true,0),"independent whole-word option exists");
 auto hits=vi_text::Find(labels,L"play",options);
 Check(hits.size()==2,"case-insensitive whole words exclude Player and PLAYERS");
 Check(vi_text::Find({Line({L"Player"})},L"PLAY",options).empty(),"PLAY never selects Player in whole-word mode");
 Check(vi_text::Find({Line({L"rePLAY"})},L"play",options).empty(),"mid-word prefix rejected");
 Check(vi_text::Find({Line({L"PLAYer"})},L"play",options).empty(),"mid-word suffix rejected");
 Check(vi_text::Find({Line({L"rePLAYer"})},L"play",options).empty(),"both partial boundaries rejected");
 Check(vi_text::Find({Line({L"PLAY"})},L"play",options).size()==1,"whole words remain case-insensitive");
 Check(vi_text::Find({Line({L"Load",L"Game"})},L"loadgame",options).size()==1,"whole multiword span ignores whitespace");
 Check(vi_text::Find({Line({L"Preload",L"Game"})},L"loadgame",options).empty(),"multiword partial first token rejected");
 Check(vi_text::Find({Line({L"Load",L"Gameplay"})},L"loadgame",options).empty(),"multiword partial last token rejected");
 Check(vi_text::Find({Line({L"Load",L"Game"})},L"  LOAD   GAME ",options).size()==1,"query whitespace and case preserved");
 Check(vi_text::Find({Line({L"\u0418\u0413\u0420\u0410"})},L"\u0438\u0433\u0440\u0430",options).size()==1,"existing Windows Unicode lowercase remains in use");
 Check(vi_text::Find({Line({L"\u0418\u0413\u0420\u0410\u0422\u042c"})},L"\u0438\u0433\u0440\u0430",options).empty(),"Unicode token suffix rejected");
 Check(vi_text::Find({Line({L"(PLAY)"})},L"PLAY",options).empty(),"boundaries are OCR tokens including their punctuation");
 options.exact=true;
 Check(vi_text::Find({Line({L"PLAY"})},L"play",options).empty(),"exact still requires case");
 Check(vi_text::Find({Line({L"PLAY"})},L"PLAY",options).size()==1,"exact full label still matches");
 Boundary(options,false,0);
 Check(vi_text::Find({Line({L"Player"})},L"Play",options).empty(),"exact retains boundaries when independent flag is false");
 options.exact=false;
 Check(vi_text::Find({Line({L"Player"})},L"play",options).size()==1,"explicit substring opt-out preserved");
 Boundary(options,true,0);options.fuzzy=1;
 hits=vi_text::Find({Line({L"BATTIE"})},L"BATTLE",options);
 Check(hits.size()==1&&hits[0].score<1,"explicit fuzzy still accepts whole OCR words");
 options.fuzzy=0;
 Check(vi_text::Find({Line({L"BATTIE"})},L"BATTLE",options).empty(),"no unrequested fuzzy fallback");
 Check(vi_text::Find(labels,L"",options).empty(),"empty query never matches");
 std::printf("{\"passed\":%u,\"failed\":%u,\"completed\":true}\n",passed,failed);return failed?1:0;
}
