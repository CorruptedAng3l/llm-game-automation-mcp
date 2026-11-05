#include "vi_motion.h"
#include <cstdio>
#include <cmath>
static unsigned passed=0,failed=0;static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
int main(){
 std::vector<std::uint8_t> black(16*16*4,0),white(black.size(),255),alpha=black;
 auto d=vi_motion::Compare(black,white,16,16);Check(d.valid&&d.changedPct==100&&d.meanPct==100,"known all-black to all-white change is exactly 100 percent");
 for(size_t i=3;i<alpha.size();i+=4)alpha[i]=255;d=vi_motion::Compare(black,alpha,16,16);Check(d.valid&&d.changedPct==0&&d.meanPct==0,"alpha changes do not masquerade as motion");
 auto quarter=black;for(size_t i=0;i<quarter.size()/4;i+=4)quarter[i]=255;d=vi_motion::Compare(black,quarter,16,16);Check(d.valid&&d.changedPct==25&&std::abs(d.meanPct-25.0/3)<.00001,"one-channel quarter-frame change has exact changed area and RGB mean");
 d=vi_motion::Compare(black,white,16,16,255);Check(d.valid&&d.changedPct==0&&d.meanPct==100,"pixel tolerance affects changed area independently of mean difference");
 Check(vi_motion::BlackPct(black,16,16)==100&&vi_motion::BlackPct(white,16,16)==0,"black-frame coverage has both positive and negative controls");
 Check(!vi_motion::Compare(black,white,8,8).valid&&!vi_motion::Compare(black,white,16,16,256).valid&&vi_motion::BlackPct({},0,0)<0,"invalid dimensions and tolerance never fabricate a zero-change observation");
 unsigned w=0,h=0;Check(vi_motion::Sample({30,20,10,0},1,1,w,h)=="0A141E"&&w==1&&h==1,"sample encodes RGB order from known BGRA source bytes");
 std::vector<std::uint8_t> checker(32*32*4);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)for(unsigned c=0;c<3;++c)checker[((size_t)y*32+x)*4+c]=(x+y)%2?255:0;
 const auto grid=vi_motion::Sample(checker,32,32,w,h);bool average=w==16&&h==16&&grid.size()==1536;for(size_t i=0;i<grid.size();i+=6)average=average&&grid.substr(i,6)=="7F7F7F";Check(average,"area averages cover all pixels instead of aliasing a checkerboard to black");
 Check(vi_motion::Sample({},1,1,w,h).empty()&&w==0&&h==0,"invalid motion samples are unavailable");
 printf("motion metrics: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
