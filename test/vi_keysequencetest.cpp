#include "../src/vi_key_sequence.h"
#include <cstdio>
static int failed=0,passed=0;
static void Check(bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(ok)++passed;else ++failed;}
int main(){vi_key_sequence::Plan plan;std::string error;
 Check(vi_key_sequence::Parse("# flight\nhold w+space 1500\nwait 100\nkey ctrl+a 50\n",plan,error)&&plan.steps.size()==3&&plan.keys.size()==4&&plan.duration==1710&&plan.transitions==8,"chords, explicit waits and modifier ordering budget");
 for(const char* text:{"hold w 1\nlaunch x 1","hold w 1\nhold zzz 2","key w -1","key w 1.5","key w 120001","key w 999999999999999999999999","key w 1 extra","key w 118000\nwait 1"}){plan.keys={123};Check(!vi_key_sequence::Parse(text,plan,error)&&plan.keys.empty()&&!error.empty(),"invalid complete script refused with no partial plan");}
 Check(vi_key_sequence::Parse("wait 1\nsleep 9",plan,error)&&plan.keys.empty()&&plan.duration==10&&plan.steps[1].at==1,"wait-only plan has anchored offsets and no input resources");
 Check(vi_key_sequence::Parse("at 50 key space 20\nat 100 key space 10",plan,error)&&plan.absolute&&plan.steps[0].at==50&&plan.steps[1].at==100&&plan.duration==110,"absolute offsets retain initial and inter-step gaps");
 for(const char* text:{"at 0 key space 20\nat 10 key space 20","at 0 key ctrl+a 20\nat 49 key b 1","at 1.5 key space 20","at 118000 key space 1","wait 10\nat 9 key space 1"})Check(!vi_key_sequence::Parse(text,plan,error)&&plan.steps.empty(),"absolute overlap, malformed offset and duration overflow refuse atomically");
 Check(vi_key_sequence::Parse("key ctrl+num+ 0\r\nsleep 1",plan,error)&&plan.steps[0].keys.size()==2,"numpad plus chord and CRLF");
 std::string many;for(int i=0;i<1024;++i)many+="key w 0\n";Check(vi_key_sequence::Parse(many,plan,error),"exact1024-step boundary");many+="key w 0\n";Check(!vi_key_sequence::Parse(many,plan,error),"step cap");
 std::string bytes="key w 0\n#";bytes.resize(32768,'x');Check(vi_key_sequence::Parse(bytes,plan,error),"exact32768-byte boundary");bytes+='x';Check(!vi_key_sequence::Parse(bytes,plan,error),"byte cap");
 std::string transitions;for(int i=0;i<1024;++i)transitions+="key w+d 0\n";Check(vi_key_sequence::Parse(transitions,plan,error)&&plan.transitions==4096,"exact4096-transition boundary");transitions.replace(0,7,"key w+d+s");Check(!vi_key_sequence::Parse(transitions,plan,error)&&error.find("4096")!=std::string::npos,"transition cap");
 Check(vi_key_sequence::Parse("hold ctrl+a 117970",plan,error)&&plan.duration==118000,"exact118000ms including modifier overhead");Check(!vi_key_sequence::Parse("hold ctrl+a 117971",plan,error),"duration cap includes modifier overhead");
 Check(!vi_key_sequence::Parse(std::string("key w 1\0key d 1",15),plan,error),"NUL refused");
 Check(vi_key_sequence::Parse("retap w+shift shift 1500 300 10",plan,error)&&plan.duration==1530&&plan.transitions==12&&plan.keys.size()==2,"retap reserves complete chord and counts each release/repress pair");
 Check(vi_key_sequence::Parse("at 50 retap w+shift shift 1500 300 10",plan,error)&&plan.absolute&&plan.duration==1580,"retap shares the absolute sequence anchor and modifier budget");
 for(const char* text:{"retap w+shift a 1500 300 10","retap w+shift shift+w 1500 300 10","retap w+shift shift 1500 10 10","retap w+shift shift 300 300 10","retap w+shift shift 1500 300 0","retap w+shift shift 1500 300 1001","retap w+shift shift 1500 300 10 extra","retap w w 10000 2 1","retap w+shift+lshift shift 1000 200 10","retap w+lshift+rshift lshift 1000 200 10"})Check(!vi_key_sequence::Parse(text,plan,error)&&plan.steps.empty(),"invalid retap target/timing/transition budget refuses before input");
 std::printf("%d PASS / %d FAIL\n",passed,failed);return failed?1:0;}
