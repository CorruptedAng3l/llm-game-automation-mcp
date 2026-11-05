// Actual CLI crop/timing helpers over memory pixels. No CLI entry or target APIs.
#define wmain ViTextContextCli
#include "../src/vinput_cli.cpp"
#undef wmain
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++pass:++fail;};
 Frame source;source.img.w=100;source.img.h=50;source.img.pixels.resize(100*50*4);for(size_t i=0;i<source.img.pixels.size();++i)source.img.pixels[i]=(unsigned char)(i%251);source.ox=10;source.oy=20;source.sx=2;source.sy=3;source.captureQpc="1000";source.frameQpc="999";source.qpcFrequency="10000000";source.nativeFrame=42;source.captureScope="{}";
 Frame cropped;std::string error;check(CropClientFrame(source,"30,35,40,30",cropped,error)&&cropped.img.w==20&&cropped.img.h==10&&cropped.ox==30&&cropped.oy==35&&cropped.img.pixels[0]==source.img.pixels[(5*100+10)*4],"context crop selects exact source pixels through per-axis client transform");
 check(cropped.captureQpc==source.captureQpc&&cropped.frameQpc==source.frameQpc&&cropped.nativeFrame==42&&source.img.w==100,"primary/context crop preserves shared original QPC/index and source pixels");
 for(const auto text:{"0,0,0,1","1,2,3,4junk","-1,1,2,3","32768,0,1,1","1,2,3"}){vi_vision::Region r;check(!ClientRegion(text,r),"invalid bounded region refused");}
 check(!CropClientFrame(source,"0,0,20,20",cropped,error),"outside source region refuses instead of recapturing a later frame");
 vi_ready_json::Reader receipt{"{"+FrameTimingFields(source)+"}",0,{}};check(receipt.Parse()&&receipt.fields.at("captured_present").raw=="42"&&receipt.fields.at("capture_qpc_ticks").raw=="\"1000\"","text-match receipt exposes actual captured index and QPC");
 source.frameQpc.clear();vi_ready_json::Reader fallback{"{"+FrameTimingFields(source)+"}",0,{}};check(fallback.Parse()&&fallback.fields.at("captured_present").raw=="null"&&fallback.fields.at("frame_qpc_ticks").raw=="null","PrintWindow never invents a matched presented-frame index");
 const auto noPad=vi_doctor::ControllerCoverage(true,"{\"xinput\":4}","{\"virtual_returns\":\"0\"}");
 check(vi_doctor::Field(noPad,"virtual_return_observed")=="false"&&vi_doctor::Field(noPad,"actual_gamepad_api")=="null"&&vi_doctor::Field(vi_doctor::Field(noPad,"GameInput"),"instrumented")=="false","initial XInput calls cannot establish an active pad reader or another API");
 const auto pad=vi_doctor::ControllerCoverage(true,"{\"xinput\":5}","{\"virtual_returns\":\"18446744073709551615\"}");check(vi_doctor::Field(pad,"virtual_return_observed")=="true","exact native virtual return is positive evidence without naming game response");
 const auto unknown=vi_doctor::ControllerCoverage(false,"{\"xinput\":5}","{\"virtual_returns\":\"1\"}");check(vi_doctor::Field(unknown,"virtual_return_observed")=="null"&&vi_doctor::Field(unknown,"xinput_calls_since_injection")=="null","unavailable native state stays unknown");
 const auto malformed=vi_doctor::ControllerCoverage(true,"{\"xinput\":\"5\"}","{\"virtual_returns\":\"18446744073709551616\"}");check(vi_doctor::Field(malformed,"virtual_return_observed")=="null"&&vi_doctor::Field(malformed,"xinput_calls_since_injection")=="null","malformed and overflowing controller counters do not establish reads");
 printf("text context: %u PASS / %u FAIL\n",pass,fail);return fail?1:0;}
