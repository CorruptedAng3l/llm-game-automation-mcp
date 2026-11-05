#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "vi_d3d9_fixture_geometry.h"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){if(argc==2&&!strcmp(argv[1],"--await-start")){char line[16]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};
 const RECT before{-28000,600,-27200,1200},desktop{-1920,0,3840,2160};RECT after{};
 check(vi_d9_fixture_geometry::Predict(before,desktop,0,0,640,480,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER,after)&&after.left==-28000&&after.right==-27360,"bounded original offscreen client resize predicted");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,0,0,640,480,SWP_NOACTIVATE,after),"onscreen movement refused");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,0,0,0,480,SWP_NOMOVE,after),"empty size refused");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,0,0,8193,480,SWP_NOMOVE,after),"oversized geometry refused");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,LONG_MAX,600,640,480,0,after),"coordinate overflow refused");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,0,0,640,480,SWP_NOMOVE|SWP_SHOWWINDOW,after),"visibility mutation refused");
 check(!vi_d9_fixture_geometry::Predict(before,desktop,0,0,640,480,SWP_NOMOVE|SWP_ASYNCWINDOWPOS,after),"unconfirmed asynchronous positioning refused");
 check(!vi_d9_fixture_geometry::Predict(RECT{0,0,800,600},desktop,0,0,640,480,SWP_NOMOVE,after),"initial onscreen window refused");
 check(vi_d9_fixture_geometry::Predict(before,desktop,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE,after)&&after.right==before.right,"unchanged proved offscreen geometry accepted");
 printf("D9_GEOMETRY_RESULT %u %u\n",pass,fail);return fail?1:0;}
