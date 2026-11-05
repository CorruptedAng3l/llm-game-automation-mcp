// Actual CLI crop/timing helpers over memory pixels. No CLI entry or target APIs.
#define wmain ViTextContextCli
#include "../src/vinput_cli.cpp"
#undef wmain
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++pass:++fail;};
 vi_text_stability::Gate stable(300);const std::array<int,4> box{10,20,80,30};
 check(stable.Observe(box,"scope","1000","1000",0)&&!stable.ready,"one OCR observation cannot authorize a stability click");
 check(stable.Observe(box,"scope","1299","1000",299)&&!stable.ready&&stable.samples==2,"capture-clock window is enforced before a click");
 check(stable.Observe(box,"scope","1300","1000",300)&&stable.ready&&stable.observed==300,"same exact box and distinct captures satisfy the finite window");
 check(stable.Observe({11,20,80,30},"scope","1400","1000",400)&&!stable.ready&&stable.samples==1,"sliding box resets stability without an input retry");
 check(stable.Observe({11,20,80,30},"scope","1400","1000",800)&&!stable.ready&&stable.samples==1,"a duplicated capture clock cannot supply the second witness");
 check(stable.Observe({11,20,80,30},"new-scope","1900","1000",900)&&!stable.ready&&stable.samples==1,"a new window scope cannot reuse old box stability");
 check(!stable.Observe({11,20,80,30},"new-scope","1800","1000",1000),"backwards native clock is refused");
 check(!stable.Observe({11,20,80,30},"new-scope","2000","2000",1000),"frequency drift is refused");
 vi_text_stability::Gate latency(1000);check(latency.Observe(box,"scope","1000","1000",500)&&latency.Observe(box,"scope","1750","1000",1600)&&!latency.ready&&latency.observed==750,"slow second OCR cannot turn750ms of capture evidence into1000ms stability");
 stable.Reset();check(!stable.started&&!stable.ready&&stable.samples==0,"missing text resets the entire observed window");
 check(!stable.Observe(box,"","7","1000",1100)&&!stable.Observe(box,"scope","0","1000",1100)&&!stable.Observe(box,"scope","18446744073709551616","1000",1100),"missing scope, zero and overflowing clocks cannot authorize input");
 vi_text_stability::Gate bounded(3000);check(!bounded.Observe(box,"scope","1","18446744073709551615",0),"unrepresentable capture interval refuses instead of overflowing");
 const std::string good=R"({"ok":true,"capture_scope_checked":true,"button_released":true,"pointer_scope_version":1,"input_admission_version":1,"button_down_count":1,"last_press_x":101,"last_press_y":201,"admission_deadline_qpc_ticks":"444"})";
 std::string ack=good,stats;check(StableTextAcknowledgement(true,ack,"444",101,201,stats),"exact scoped single-press/release/deadline ACK confirms the one click");
 ack=good;stats.clear();check(!StableTextAcknowledgement(false,ack,"444",101,201,stats)&&stats.find("\"input_may_have_been_applied\":true")!=std::string::npos,"lost transport retains uncertainty despite otherwise complete ACK text");
 ack=good;stats.clear();check(!StableTextAcknowledgement(true,ack,"445",101,201,stats),"wrong native admission deadline cannot confirm a click");
 ack=good;stats.clear();check(!StableTextAcknowledgement(true,ack,"444",102,201,stats),"wrong native press position cannot confirm a click");
 ack=R"({"ok":false,"input_injected":false,"error":"expired"})";stats.clear();check(!StableTextAcknowledgement(false,ack,"444",101,201,stats)&&stats.find("\"input_may_have_been_applied\":false")!=std::string::npos,"typed pre-input refusal remains definite without a retry");
 ack="{";stats.clear();const bool malformedAck=StableTextAcknowledgement(true,ack,"444",101,201,stats);vi_ready_json::Reader malformedResult{"{\"base\":true"+stats+",\"click\":"+ack+"}",0,{}};
 check(!malformedAck&&malformedResult.Parse()&&stats.find("\"click_raw_hex\":\"7B\"")!=std::string::npos,"malformed ACK stays exact byte evidence inside valid outer JSON");
 ack=std::string(1,(char)0xFF);stats.clear();const bool invalidUtf8=StableTextAcknowledgement(false,ack,"444",101,201,stats);vi_ready_json::Reader invalidUtf8Result{"{\"base\":true"+stats+",\"click\":"+ack+"}",0,{}};
 check(!invalidUtf8&&invalidUtf8Result.Parse()&&stats.find("\"click_raw_hex\":\"FF\"")!=std::string::npos,"non-UTF8 ACK bytes cannot corrupt the uncertainty result JSON");
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
