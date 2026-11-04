#include "../src/vi_key_sequence.h"
#include <cstdio>
static int failed=0,passed=0;
static void Check(bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(ok)++passed;else ++failed;}
int main(){vi_key_sequence::Plan plan;std::string error;
 Check(vi_key_sequence::Parse("# flight\nhold w+space 1500\nwait 100\nkey ctrl+a 50\n",plan,error)&&plan.steps.size()==3&&plan.keys.size()==4&&plan.duration==1710&&plan.transitions==8,"chords, explicit waits and modifier ordering budget");
 for(const char* text:{"hold w 1\nlaunch x 1","hold w 1\nhold zzz 2","key w -1","key w 1.5","key w 120001","key w 999999999999999999999999","wait 1","key w 1 extra","key w 118000\nwait 1"}){plan.keys={123};Check(!vi_key_sequence::Parse(text,plan,error)&&plan.keys.empty()&&!error.empty(),"invalid complete script refused with no partial plan");}
 Check(vi_key_sequence::Parse("key ctrl+num+ 0\r\nsleep 1",plan,error)&&plan.steps[0].keys.size()==2,"numpad plus chord and CRLF");
 std::string many;for(int i=0;i<1024;++i)many+="key w 0\n";Check(vi_key_sequence::Parse(many,plan,error),"exact1024-step boundary");many+="key w 0\n";Check(!vi_key_sequence::Parse(many,plan,error),"step cap");
 std::string bytes="key w 0\n#";bytes.resize(32768,'x');Check(vi_key_sequence::Parse(bytes,plan,error),"exact32768-byte boundary");bytes+='x';Check(!vi_key_sequence::Parse(bytes,plan,error),"byte cap");
 std::string transitions;for(int i=0;i<1024;++i)transitions+="key w+d 0\n";Check(vi_key_sequence::Parse(transitions,plan,error)&&plan.transitions==4096,"exact4096-transition boundary");transitions.replace(0,7,"key w+d+s");Check(!vi_key_sequence::Parse(transitions,plan,error)&&error.find("4096")!=std::string::npos,"transition cap");
 Check(vi_key_sequence::Parse("hold ctrl+a 117970",plan,error)&&plan.duration==118000,"exact118000ms including modifier overhead");Check(!vi_key_sequence::Parse("hold ctrl+a 117971",plan,error),"duration cap includes modifier overhead");
 Check(!vi_key_sequence::Parse(std::string("key w 1\0key d 1",15),plan,error),"NUL refused");
 std::printf("%d PASS / %d FAIL\n",passed,failed);return failed?1:0;}
