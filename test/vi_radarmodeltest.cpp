#include "../src/vi_radar.h"
#include "../src/vi_radar_json.h"
#include <cstdio>
static unsigned passed=0,failed=0;
static void Check(bool good,const char* name){std::printf("%s %s\n",good?"PASS":"FAIL",name);good?++passed:++failed;}
int main(){
 const std::string captured="{\"ok\":true,\"capture_scope\":{\"hwnd\":\"0x123\"},\"capture_qpc_ticks\":\"123456\",\"qpc_frequency\":\"1000\",\"region_client_w\":128}";vi_ready_json::Reader decoded{captured};std::string stamp;double width=0;std::uint64_t ticks=0;
 Check(decoded.Parse()&&vi_radar_json::String(decoded,"capture_qpc_ticks",stamp)&&vi_radar_json::U64(stamp,ticks)&&ticks==123456&&vi_radar_json::ExactObject(decoded,"capture_scope","{\"hwnd\":\"0x123\"}")&&vi_radar_json::Number(decoded,"region_client_w",width)&&width==128,"actual strict parser field tags decode capture strings, scope and region numbers");
 Check(!vi_radar_json::String(decoded,"region_client_w",stamp)&&!vi_radar_json::ExactObject(decoded,"capture_qpc_ticks","\"123456\""),"type confusion cannot authorize a native capture");
 Check(!vi_radar_json::U64("18446744073709551616",ticks)&&!vi_radar_json::U64("01",ticks)&&!vi_radar_json::U64("0",ticks),"overflow, noncanonical and unavailable capture clocks stay unavailable");
 std::vector<unsigned char> pixels(128*128*4);vi_radar::Target target;target.colour={0,255,0};
 auto dot=[&](unsigned x,unsigned y){for(unsigned i=x-1;i<=x+1;++i){auto* p=pixels.data()+4u*(y*128+i);p[0]=0;p[1]=255;p[2]=0;p[3]=255;}};
 dot(64,24);auto found=vi_radar::Detect({pixels.data(),128,128},target);
 Check(found.complete&&found.found&&!found.ambiguous&&found.matchedPixels==3&&found.x==64&&found.y==24,"actual BGRA unique connected marker centroid");
 dot(100,80);auto two=vi_radar::Detect({pixels.data(),128,128},target);Check(two.complete&&!two.found&&two.ambiguous&&two.candidates==2,"two disjoint same-colour markers remain ambiguous");
 auto small=target;small.minPixels=4;auto missing=vi_radar::Detect({pixels.data(),128,128},small);Check(missing.complete&&!missing.found&&missing.candidates==0,"component minimum rejects noise after complete scan");
 Check(!vi_radar::Detect({pixels.data(),513,128},target).complete,"oversized image cannot claim marker absence");
 std::vector<unsigned char> icon(16*16*4);auto paint=[&](unsigned x,unsigned y,unsigned char r,unsigned char g,unsigned char b){auto* p=icon.data()+4u*(y*16+x);p[0]=b;p[1]=g;p[2]=r;p[3]=255;};paint(4,6,255,0,0);paint(5,6,0,255,0);
 vi_radar::Target shape;shape.w=2;shape.h=1;shape.tolerance=0;shape.rgba={255,0,0,255,0,255,0,255};auto templ=vi_radar::Detect({icon.data(),16,16},shape);
 Check(templ.complete&&templ.found&&templ.x==4.5&&templ.y==6,"template channels and centroid use exact opaque samples");
 paint(10,12,255,0,0);paint(11,12,0,255,0);templ=vi_radar::Detect({icon.data(),16,16},shape);Check(templ.complete&&templ.ambiguous&&!templ.found,"multiple template instances never choose an arbitrary target");
 shape.rgba[7]=0;Check(!vi_radar::Detect({icon.data(),16,16},shape).complete,"single opaque template pixel is insufficient evidence");
 std::vector<unsigned char> blank(256*256*4);shape.w=32;shape.h=32;shape.rgba.assign(32*32*4,255);shape.minScore=.5;const auto exhausted=vi_radar::Detect({blank.data(),256,256},shape);Check(!exhausted.complete&&exhausted.reason=="template_scan_budget_exhausted","exhausted template work budget is not a negative detection");
 vi_radar::Config config;config.centreX=64;config.centreY=64;vi_radar::State state;
 auto point=[&](double x,double y){vi_radar::Detection d;d.complete=d.found=true;d.x=x;d.y=y;return d;};
 auto d=vi_radar::Observe(state,config,point(64,20),1,1,1,0);Check(d.observed&&d.forward&&d.steer==0&&d.distance==44&&!d.done,"aligned marker admits forward intent without claiming arrival");
 d=vi_radar::Observe(state,config,point(64,30),1,1,2,100);Check(d.forward&&!d.done,"radial progress updates the progress window");
 d=vi_radar::Observe(state,config,point(64,30),1,1,3,1700);Check(d.done&&!d.forward&&d.steer==0&&d.reason=="no_marker_progress","static minimap distance stops despite unrelated world animation");
 state={};d=vi_radar::Observe(state,config,point(104,64),1,1,1,0);Check(d.bearing==90&&d.steer==1&&!d.forward,"right-hand marker requests right steering with forward gated");
 d=vi_radar::Observe(state,config,point(64,10),1,1,2,1400);Check(d.forward&&!d.done,"changing from bearing alignment to distance uses a fresh progress metric");
 d=vi_radar::Observe(state,config,point(64,20),1,1,3,2000);Check(!d.done&&d.forward,"distance improvement is not compared to an old angle value");
 state={};for(unsigned n=1;n<=3;++n)d=vi_radar::Observe(state,config,point(64,60),1,1,n,100*n);Check(d.done&&!d.forward&&d.reason=="radar_marker_near_centre","arrival requires the configured count of distinct capture witnesses");
 state={};vi_radar::Observe(state,config,point(64,60),1,1,7,0);d=vi_radar::Observe(state,config,point(64,60),1,1,7,100);Check(d.done&&!d.observed&&state.stable==1,"repeated QPC cannot manufacture extra arrival samples");
 state={};vi_radar::Observe(state,config,point(64,20),1,1,2,0);d=vi_radar::Observe(state,config,two,1,1,3,100);Check(d.done&&!d.observed&&!d.forward&&d.reason=="ambiguous_marker"&&!state.latest.observed,"ambiguous observation clears the control intent and marks bearings unavailable");
 config.turnOnly=true;config.degrees=90;config.samples=2;state={};d=vi_radar::Observe(state,config,point(100,64),1,1,1,0);Check(d.steer==1&&!d.forward&&!d.done,"reference turn starts in the requested signed direction");
 vi_radar::Observe(state,config,point(64,20),1,1,2,100);d=vi_radar::Observe(state,config,point(64,20),1,1,3,200);Check(d.done&&d.radarAngleDelta==90&&d.reason=="radar_bearing_aligned","turn closes on observed radar reference angle, not supplied mouse gain");
 config.degrees=360;Check(!vi_radar::Valid(config),"full rotations cannot alias to a zero-angle target");config.degrees=0;state={};d=vi_radar::Observe(state,config,point(64,64),1,1,1,0);Check(d.done&&!d.observed&&d.reason=="reference_marker_at_centre","reference at the centre has no usable bearing");
 Check(vi_radar::Angle(181)==-179&&vi_radar::Angle(-181)==179,"signed branch crossing uses shortest circular error");
 config.samples=1;state={};d=vi_radar::Observe(state,config,point(64,64),1,1,1,0);Check(d.done&&!d.observed&&d.reason=="reference_marker_at_centre"&&state.stable==0,"one-sample zero turn cannot use an undefined central bearing as success");
 Check(vi_radar::PulseDown(.01,100,9)&&!vi_radar::PulseDown(.01,100,10),"tiny duty retains a bounded ten-millisecond on interval");
 Check(vi_radar::PulseDown(1,100,999)&&!vi_radar::PulseDown(0,100,0),"full and zero duty keep exact digital endpoints");
 Check(!vi_radar::PulseDown(.5,100,1050)&&vi_radar::PulseDown(.5,100,1100),"late sampling evaluates one current PWM phase without catch-up edges");
 std::printf("radar model: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
