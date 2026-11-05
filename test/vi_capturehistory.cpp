#include "../src/vi_capture_history.h"
#include <cstdio>
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};
 vi_capture_history::History h;auto a=h.Observe("window1/full/print",1,100,1000);check(a.measured&&!a.comparable&&a.identical==1,"first screenshot has no prior comparison");
 a=h.Observe("window1/full/print",1,350,1000);check(a.comparable&&!a.changed&&a.identical==2&&a.unchangedMs==250,"same source hash retains exact unchanged interval");
 a=h.Observe("window1/full/print",2,500,1000);check(a.changed&&a.identical==1&&a.unchangedMs==0,"changed pixels reset the quiet interval");
 a=h.Observe("window1/crop/print",2,700,1000);check(!a.comparable,"another region cannot borrow full-frame history");
 a=h.Observe("window1/full/native",2,750,1000);check(!a.comparable,"another capture backend cannot borrow PrintWindow history");
 a=h.Observe("window2/full/print",2,800,1000);check(!a.comparable,"retargeted window lifetime starts a new observation");
 a=h.Observe("window1/full/print",2,600,1000);check(a.comparable&&a.identical==2&&a.unchangedMs==100,"other keys preserve this scope's independent history");
 a=h.Observe("window1/full/print",2,590,1000);check(!a.comparable&&a.identical==1,"regressed clock starts an unmeasured comparison");
 a=h.Observe("window1/full/print",2,610,2000);check(!a.comparable,"changed clock frequency cannot fabricate age");
 check(!h.Observe("x",0,0,1000).measured&&!h.Observe("x",0,1,0).measured&&!h.Observe(std::string(4097,'x'),0,1,1).measured,"invalid clock and unbounded key are unmeasured");
 for(unsigned i=0;i<33;++i)h.Observe("bounded"+std::to_string(i),0,1000,1000);
 a=h.Observe("bounded0",0,2000,1000);check(!a.comparable,"bounded history evicts oldest observations");
 printf("capture history: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;
}
