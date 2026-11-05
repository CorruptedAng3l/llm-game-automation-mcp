#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
#include "vi_ready_json.h"
static unsigned passed=0,failed=0;
static void Check(bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++passed:++failed;}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;g_pid=GetCurrentProcessId();
 SavedCaptureMap map;map.valid=true;map.w=640;map.h=359;map.cw=1920;map.ch=1080;map.ox=100.125;map.oy=50.25;map.scope.window=(HWND)(ULONG_PTR)123;map.scope.width=1920;map.scope.height=1080;map.scope.thread=77;map.scope.adoption=1;map.scope.lifetime=2;
 unsigned long long id=7;auto request=[&](){JObj j;j["capture_map_id"]={1,std::to_string(id)};j["expected_capture_scope"]={1,CaptureScopeJson(map.scope)};j["image_w"]={2,{},320};j["image_h"]={2,{},180};return j;};
 JOut fields;CaptureMappingFields(fields,map,id);const auto json=fields.done();vi_ready_json::Reader parsed{json,0,{}};Check(parsed.Parse(),"persisted geometry is valid typed JSON");
 vi_ready_json::Reader mapping{parsed.fields.at("capture_mapping").raw,0,{}};Check(mapping.Parse()&&std::strtod(mapping.fields.at("scale").raw.c_str(),nullptr)==(double)640/1920&&std::strtod(mapping.fields.at("scale_y").raw.c_str(),nullptr)==(double)359/1080,"independent image axes retain full precision across clients");
 auto j=request();Check(CaptureMappingResize(map,id,j)&&id==8&&map.w==320&&map.h==180&&map.ox==100.125&&map.oy==50.25&&map.cw==1920&&map.ch==1080,"matching resize CAS changes only returned image dimensions");
 Check(!CaptureMappingResize(map,id,j)&&id==8&&map.w==320,"old capture map identity cannot overwrite a later transform");
 j=request();j["expected_capture_scope"]={1,"{}"};Check(!CaptureMappingResize(map,id,j),"different window scope cannot resize a saved image map");
 for(const auto& value:std::vector<JV>{{1,"400"},{2,{},0},{2,{},-1},{2,{},1.5},{2,{},32769},{2,{},std::numeric_limits<double>::infinity()}}){j=request();j["image_w"]=value;Check(!CaptureMappingResize(map,id,j),"invalid dimension preserves the current capture map");}
 j=request();j["image_w"]={2,{},32768};j["image_h"]={2,{},32768};Check(!CaptureMappingResize(map,id,j),"oversized image area is refused without allocation or input");
 j=request();j["capture_map_id"]={1,"08"};Check(!CaptureMappingResize(map,id,j),"noncanonical identity cannot alias the current map");
 map.valid=false;j=request();Check(!CaptureMappingResize(map,id,j),"missing saved capture is not synthesized");
 g_hwnd=nullptr;g_savedCaptureMap=map;const auto refused=CaptureMappingCommand(JObj{},false);Check(refused.find("\"ok\":false")!=std::string::npos&&refused.find("\"input_injected\":false")!=std::string::npos,"actual RPC path refuses absent window/map before returning geometry");
 Check(vi_launch_terminal::Verify()&&!g_jobWorker&&!g_hwnd,"no window, worker, physical input or product Init");
 printf("capture mapping: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
