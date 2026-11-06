#include <vi_key_drive.h>
#include <vi_key_sequence.h>
#include <cstdio>
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;}
static bool Has(const vi_key_drive::Sample& sample,int key){return std::find(sample.desired.begin(),sample.desired.end(),key)!=sample.desired.end();}
static bool PwmPlan(const vi_key_sequence::Plan& plan){
#ifdef VI_KEY_SEQUENCE_PWM_VERSION
 return plan.pwm;
#else
 (void)plan;return false;
#endif
}
int main(){
 using namespace vi_key_drive;Bindings bindings;std::string error;Plan plan;
 Check(ParseBindings({"w+shift","a","d","s"},bindings,error),"throttle chord and separate steering/brake keyboard resources parse");
 for(const auto& names:std::vector<std::array<std::string,4>>{{"w+a","a","d","s"},{"w+shift","lshift","d","s"},{"w","a+d","d","s"},{"lbutton","a","d","s"},{"w+q+e+r+t","a","d","s"}}){Bindings invalid;Check(!ParseBindings(names,invalid,error),"overlapping aliases, oversized chords and mouse bindings refuse");}
 Check(Parse("drive 600 1 -0.3 0\ndrive 400 1 0.5 0\ndrive 200 0 0 1\n",bindings,60,plan,error)&&plan.duration==1200&&plan.phases.size()==3&&plan.transitionBound==44&&plan.keys.size()==5,"bounded driving phases preserve exact duration, finite edge bound and reserved used-key union");
 auto sample=At(plan,0);Check(Has(sample,0x10)&&Has(sample,'W')&&Has(sample,'A')&&!Has(sample,'D')&&!Has(sample,'S'),"initial throttle chord and left PWM are independent states");
 sample=At(plan,18);Check(Has(sample,'W')&&Has(sample,0x10)&&!Has(sample,'A')&&sample.next==60,"PWM off interval preserves held throttle");
 sample=At(plan,600);Check(Has(sample,'W')&&Has(sample,'D')&&!Has(sample,'A')&&sample.phase==1,"phase direction change never requests both steering keys");
 sample=At(plan,1000);Check(!Has(sample,'W')&&!Has(sample,'A')&&!Has(sample,'D')&&Has(sample,'S'),"brake phase requests only its owned brake key");
 sample=At(plan,1200);Check(sample.finished&&sample.desired.empty(),"finite plan endpoint has no desired held key");
 vi_key_pwm::Cursor cursor;sample=At(plan,0);Check(cursor.Observe(0,sample.ordinal),"PWM cursor accepts its initial anchor");sample=At(plan,465);Check(cursor.Observe(465,sample.ordinal)&&cursor.skipped==14&&!Has(sample,'A')&&Has(sample,'W')&&sample.next==480,"late sample skips fourteen expired boundaries and returns only current state");
 Check(!cursor.Observe(464,sample.ordinal),"clock regression refuses instead of replaying earlier states");
 vi_key_pwm::Spec pwm;for(const auto duty:{0.00001,0.01,0.99,0.99999})Check(!vi_key_pwm::Make(duty,60,pwm,error),"unrepresentable or sub10ms PWM interval refuses explicitly");
 Check(vi_key_pwm::Make(0,60,pwm,error)&&!vi_key_pwm::At(pwm,0,800).down&&vi_key_pwm::TransitionBound(pwm,800,1)==0,"zero duty creates no planned input edge");
 Check(vi_key_pwm::Make(1,60,pwm,error)&&vi_key_pwm::At(pwm,799,800).down&&!vi_key_pwm::At(pwm,800,800).down&&vi_key_pwm::TransitionBound(pwm,800,1)==2,"full duty is one finite hold and release");
 for(const auto& script:{"drive 60001 1 0 0\n","drive 60000 1 0.5 0\n","drive 10 2 0 0\n","drive 10 1 nan 0\n","drive 10 1 0 0 extra\n"}){Plan invalid;Check(!Parse(script,bindings,20,invalid,error)&&invalid.phases.empty(),"invalid/over-budget drive plans fail before publishing a result");}
 std::string excessive;for(unsigned i=0;i<129;++i)excessive+="drive 10 0 0 0\n";Check(!Parse(excessive,bindings,100,plan,error),"more than128 phases refuse");
 Check(Parse("drive 100 1 0 0\ndrive 100 1 0 0\n",bindings,100,plan,error),"contiguous throttle phases parse");vi_key_pwm::Cursor continuous;sample=At(plan,0);continuous.Observe(0,sample.ordinal);sample=At(plan,100);Check(continuous.Observe(100,sample.ordinal)&&continuous.skipped==0&&Has(sample,'W'),"coincident phase boundary is one deadline and does not invent skipped work");
 vi_key_sequence::Plan sequence;
 Check(vi_key_sequence::Parse("pwm a 800 0.3 60\nwait 20\n",sequence,error)&&PwmPlan(sequence)&&sequence.duration==820&&sequence.transitions==28,"user-facing PWM sequence step parses fractional duty without modifier-time padding");
 Check(vi_key_sequence::Parse("pwm a 800 0 60\n",sequence,error)&&sequence.transitions==0&&sequence.keys.empty(),"zero-duty sequence does not reserve or release an unrelated key");
 for(const auto& script:{"pwm a 800 0.01 60\n","pwm a 800 30 60\n","pwm shift+lshift 800 0.3 60\n","pwm a 60000 0.5 20\n"})Check(!vi_key_sequence::Parse(script,sequence,error),"invalid PWM fractional duty, aliases and transition budget refuse");
 for(const auto& key:{"lbutton","VK_RBUTTON","mbutton","xbutton1","vk00006","VK_0x02","w+vk1"})Check(!vi_key_sequence::Parse(std::string("pwm ")+key+" 800 0.5 200\n",sequence,error),"PWM rejects mouse aliases and lower numeric VK values before any result");
 Check(vi_key_sequence::Parse("hold lbutton 10\nretap lbutton lbutton 800 200 10\n",sequence,error),"existing mouse hold/retap syntax remains compatible outside keyboard-only PWM");
 std::printf("key drive policy: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
