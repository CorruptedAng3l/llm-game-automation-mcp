#include "vi_radar_json.h"
bool RadarString(const vi_ready_json::Reader& reader,const char* key,std::string& value){return vi_radar_json::String(reader,key,value);}
bool RadarU64(const std::string& value,std::uint64_t& number){return vi_radar_json::U64(value,number);}
bool RadarJsonNumber(const vi_ready_json::Reader& reader,const char* key,double& value){return vi_radar_json::Number(reader,key,value);}
std::string RadarRefusal(const std::string& reason){JOut out;out.bol("ok",false);out.bol("input_injected",false);out.str("error",reason);return out.done();}
struct RadarCapture {vi_radar::Detection marker;std::uint64_t qpc=0,frequency=0;ULONGLONG sampledAt=0;std::string error;};
RadarCapture CaptureRadar(const CaptureScope& scope,const std::string& region,const vi_radar::Target& target,int regionX,int regionY,const std::string& method){
 RadarCapture result;if(g_acProduct.load()){result.error="anticheat_detected";return result;}
 JObj request;request["hash_only"]=JV{3,{},0,true};request["observation_only"]=JV{3,{},0,true};request["preserve_capture_space"]=JV{3,{},0,true};request["region"]=JV{1,region};request["method"]=JV{1,method};request["timeout_ms"]=JV{2,{},250};
 const std::string raw=DoCapture(request);result.sampledAt=HousekeepingNow();vi_ready_json::Reader response{raw};bool ok=false,mapChanged=true;std::string qpc,frequency,clock;
 if(!response.Parse()||!response.Bool("ok",ok)||!ok){result.error="radar_capture_unavailable";return result;}
 if(!vi_radar_json::ExactObject(response,"capture_scope",CaptureScopeJson(scope))||!CaptureScopeCurrent(scope)||!response.Bool("capture_space_updated",mapChanged)||mapChanged||!RadarString(response,"timing_clock",clock)||clock!="QueryPerformanceCounter"||!RadarString(response,"capture_qpc_ticks",qpc)||!RadarString(response,"qpc_frequency",frequency)||!RadarU64(qpc,result.qpc)||!RadarU64(frequency,result.frequency)||result.frequency!=vi_qpc::Frequency()||result.qpc>vi_qpc::Now()){
  result.error="radar_capture_scope_or_clock_changed";return result;
 }
 std::string frameQpc;if(RadarString(response,"frame_qpc_ticks",frameQpc)){std::uint64_t frame=0;if(!RadarU64(frameQpc,frame)||frame>result.qpc||result.qpc-frame>result.frequency){result.error="radar_native_frame_stale";return result;}}
 if(g_capPix.size()!=(size_t)g_capW*g_capH*4){result.error="radar_pixel_buffer_incomplete";return result;}
 result.marker=vi_radar::Detect({g_capPix.data(),g_capW,g_capH},target);
 double originX=0,originY=0,width=0,height=0;
 if(!RadarJsonNumber(response,"origin_x",originX)||!RadarJsonNumber(response,"origin_y",originY)||!RadarJsonNumber(response,"region_client_w",width)||!RadarJsonNumber(response,"region_client_h",height)||width<=0||height<=0||!g_capW||!g_capH){result.error="radar_region_mapping_unavailable";return result;}
 if(result.marker.found){result.marker.x=result.marker.x*width/g_capW+originX-regionX;result.marker.y=result.marker.y*height/g_capH+originY-regionY;}
 return result;
}
bool RadarClientAlive(){if(g_currentClientPipe==INVALID_HANDLE_VALUE)return true;DWORD available=0;return PeekNamedPipe(g_currentClientPipe,nullptr,0,nullptr,&available,nullptr)!=FALSE;}
std::string RunRadarCommand(const JObj& request,bool turnOnly){
 static const char* allowed[]={"cmd","owner","force_claim","replace","request_id","radar_mode","radar_region","colour_r","colour_g","colour_b","colour_tolerance","min_pixels","max_pixels","template_rgba_hex","template_w","template_h","template_score","centre_x","centre_y","arrival_radius","angle_tolerance","forward_cone","full_steer_deg","steering_sign","stable_samples","progress_ms","period_ms","timeout_ms","poll_ms","throttle_while_turning","degrees","forward_key","left_key","right_key","expected_capture_scope","admit_before_qpc_ticks","admit_qpc_frequency","capture_method"};
 for(const auto& field:request)if(std::find(std::begin(allowed),std::end(allowed),field.first)==std::end(allowed))return RadarRefusal("unknown radar field: "+field.first);
 if(JStr(request,"radar_mode")!="heading_up")return RadarRefusal("radar_mode must explicitly be heading_up; north-up radar needs a heading observer");
 int x=0,y=0,w=0,h=0;char extra=0;const auto region=JStr(request,"radar_region");if(sscanf_s(region.c_str(),"%d,%d,%d,%d%c",&x,&y,&w,&h,&extra,1)!=4||x<0||y<0||x>32767||y>32767||w<8||h<8||w>512||h>512)return RadarRefusal("radar_region must be client x,y,w,h with sides8..512");
 auto value=[&](const char* key,double fallback,double low,double high,double& number,bool whole=false){return JobNumber(request,key,fallback,low,high,number)&&(!whole||std::floor(number)==number);};
 vi_radar::Config config;config.turnOnly=turnOnly;double duration=0,poll=0,temp=0;
 if(!value("timeout_ms",6000,250,15000,duration,true)||!value("poll_ms",100,50,1000,poll,true)||!value("centre_x",w*.5,0,w-1,config.centreX)||!value("centre_y",h*.5,0,h-1,config.centreY)||!value("arrival_radius",8,1,128,config.arrivalRadius)||!value("angle_tolerance",8,1,45,config.angleTolerance)||!value("forward_cone",25,1,90,config.forwardCone)||!value("full_steer_deg",60,10,180,config.fullSteer)||!value("steering_sign",1,-1,1,config.steeringSign)||!value("degrees",0,-179,179,config.degrees))return RadarRefusal("radar timing/centre/steering values are outside their documented bounds");
 if(!value("stable_samples",3,1,10,temp,true))return RadarRefusal("stable_samples must be1..10");config.samples=(unsigned)temp;
 if(!value("progress_ms",1500,250,10000,temp,true))return RadarRefusal("progress_ms must be250..10000");config.progressMs=(unsigned)temp;
 if(!value("period_ms",100,20,1000,temp,true))return RadarRefusal("period_ms must be20..1000");config.periodMs=(unsigned)temp;
 if(JHas(request,"throttle_while_turning")&&request.at("throttle_while_turning").t!=3)return RadarRefusal("throttle_while_turning must be boolean");config.throttleWhileTurning=JBool(request,"throttle_while_turning",false);
 if(!vi_radar::Valid(config)||(turnOnly&&!JHas(request,"degrees"))||(!turnOnly&&JHas(request,"degrees")))return RadarRefusal("turn_keys needs degrees within-179..179 and steering_sign must be1 or-1");
 vi_radar::Target target;
 if(!value("colour_tolerance",12,0,255,temp,true))return RadarRefusal("colour_tolerance must be0..255");target.tolerance=(unsigned)temp;
 if(JHas(request,"template_rgba_hex")){
  if(JHas(request,"colour_r")||JHas(request,"colour_g")||JHas(request,"colour_b")||w>256||h>256)return RadarRefusal("choose one marker colour or template; template radar sides must be<=256");
  if(!value("template_w",0,1,32,temp,true))return RadarRefusal("template width must be1..32");target.w=(unsigned)temp;
  if(!value("template_h",0,1,32,temp,true))return RadarRefusal("template height must be1..32");target.h=(unsigned)temp;
  if(!value("template_score",.95,.5,1,target.minScore))return RadarRefusal("template_score must be0.5..1");
  const auto hex=JStr(request,"template_rgba_hex");if(hex.size()!=(size_t)target.w*target.h*8)return RadarRefusal("template RGBA payload length mismatch");
  for(size_t i=0;i<hex.size();i+=2){const int high=vi_ready_json::Reader::Hex(hex[i]),low=vi_ready_json::Reader::Hex(hex[i+1]);if(high<0||low<0)return RadarRefusal("invalid template RGBA encoding");target.rgba.push_back((unsigned char)(high*16+low));}
 }else{
  if(JHas(request,"template_w")||JHas(request,"template_h")||JHas(request,"template_score"))return RadarRefusal("template dimensions require template_rgba_hex");
  unsigned char* colours[]={&target.colour.r,&target.colour.g,&target.colour.b};const char* names[]={"colour_r","colour_g","colour_b"};for(unsigned i=0;i<3;++i){if(!JHas(request,names[i])||!value(names[i],0,0,255,temp,true))return RadarRefusal("marker colour needs three byte components");*colours[i]=(unsigned char)temp;}
  if(!value("min_pixels",2,1,65536,temp,true))return RadarRefusal("min_pixels must be1..65536");target.minPixels=(unsigned)temp;if(!value("max_pixels",256,target.minPixels,65536,temp,true))return RadarRefusal("max_pixels must be>=min_pixels and<=65536");target.maxPixels=(unsigned)temp;
 }
 auto job=std::make_shared<InputJob>();job->kind=InputJob::Radar;job->duration=(ULONGLONG)duration;job->radarConfig=config;job->radarLeaseMs=(ULONGLONG)(std::max)(1000.0,poll*3);
 job->radarRequestId=JStr(request,"request_id");if(JHas(request,"request_id")&&(request.at("request_id").t!=1||job->radarRequestId.empty()||job->radarRequestId.size()>64||!std::all_of(job->radarRequestId.begin(),job->radarRequestId.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_';})))return RadarRefusal("request_id must contain1..64 ASCII letters/digits/dashes");
 job->radarForward=turnOnly?std::vector<int>{}:ParseCombo(JStr(request,"forward_key","w"));const auto left=ParseCombo(JStr(request,"left_key","a")),right=ParseCombo(JStr(request,"right_key","d"));
 if((!turnOnly&&(job->radarForward.empty()||job->radarForward.size()>4))||left.size()!=1||right.size()!=1)return RadarRefusal("forward_key needs<=4-key chord; left/right need one key each");job->radarLeft=left[0];job->radarRight=right[0];job->keys=job->radarForward;job->keys.push_back(left[0]);job->keys.push_back(right[0]);
 for(int key:job->keys){const int resource=JobResource(key);if(key<8||key>255||std::find(job->resources.begin(),job->resources.end(),resource)!=job->resources.end())return RadarRefusal("radar bindings must use distinct keyboard resources");job->resources.push_back(resource);}
 if(2*job->keys.size()*((unsigned)duration/(unsigned)poll+3)+2*((unsigned)duration/config.periodMs+1)>4096)return RadarRefusal("radar transition bound exceeds4096; lower timeout or increase poll/period");
 const auto method=JStr(request,"capture_method","auto");if(method!="auto"&&method!="native"&&method!="printwindow")return RadarRefusal("capture_method must be auto,native or printwindow");
 CaptureScope scope;if(!CaptureScopeTake(scope))return RadarRefusal("radar target scope unavailable");if(x+w>scope.width||y+h>scope.height)return RadarRefusal("radar region outside current client");
 if(JHas(request,"expected_capture_scope")&&(request.at("expected_capture_scope").t!=1||JStr(request,"expected_capture_scope")!=CaptureScopeJson(scope)))return RadarRefusal("radar capture scope changed before observation");
 auto first=CaptureRadar(scope,region,target,x,y,method);if(!first.error.empty()||!first.marker.complete||!first.marker.found)return RadarRefusal(first.error.empty()?first.marker.reason:first.error);
 job->qpcFrequency=first.frequency;job->radarLastQpc=first.qpc;job->radarLeaseAt=first.sampledAt;
 auto decision=vi_radar::Observe(job->radarState,config,first.marker,1,1,first.qpc,first.sampledAt);job->radarObservationCount=1;job->radarTrace.push_back({first.qpc,decision});
 if(decision.done&&decision.reason!="radar_bearing_aligned"&&decision.reason!="radar_marker_near_centre")return RadarRefusal(decision.reason);
 JObj admission=request;admission["async"]=JV{3,{},0,true};admission["expected_capture_scope"]=JV{1,CaptureScopeJson(scope)};
 const auto reserve=vi_qpc::Milliseconds(500,first.frequency);if(reserve>(std::numeric_limits<std::uint64_t>::max)()-first.qpc)return RadarRefusal("radar observation clock overflow");
 const auto freshDeadline=first.qpc+reserve;if(!JHas(request,"admit_before_qpc_ticks")){admission["admit_before_qpc_ticks"]=JV{1,std::to_string(freshDeadline)};admission["admit_qpc_frequency"]=JV{1,std::to_string(first.frequency)};}else{std::uint64_t given=0;if(!RadarU64(JStr(request,"admit_before_qpc_ticks"),given))return RadarRefusal("radar admission deadline malformed");if(given>freshDeadline)admission["admit_before_qpc_ticks"]=JV{1,std::to_string(freshDeadline)};}
 const auto admitted=ScheduleInputJob(job,admission);if(!job->id)return admitted;
 struct Cleanup {std::shared_ptr<InputJob> job;~Cleanup(){std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(!job->done)JobFinish(*job,"cancelled","radar_command_interrupted");}} cleanup{job};
 {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(decision.done)JobFinish(*job,"completed",decision.reason.c_str());}
 ULONGLONG next=HousekeepingNow()+(ULONGLONG)poll;
 for(;;){
  {std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(job->done)break;if(!RadarClientAlive()){JobFinish(*job,"cancelled","radar_client_disconnected");break;}}
  const auto now=HousekeepingNow();if(now<next){Sleep((DWORD)(std::min)((ULONGLONG)5,next-now));continue;}
  auto observation=CaptureRadar(scope,region,target,x,y,method);
  std::lock_guard<std::recursive_mutex> lock(g_inputMx);if(job->done)break;
  if(!observation.error.empty()||observation.frequency!=job->qpcFrequency||!CaptureScopeCurrent(scope)||!RadarClientAlive()){JobFinish(*job,"cancelled",observation.error.empty()?"radar_scope_clock_or_client_changed":observation.error.c_str());break;}
  if(HousekeepingNow()<observation.sampledAt||HousekeepingNow()-observation.sampledAt>=job->radarLeaseMs){JobFinish(*job,"cancelled","radar_observation_expired");break;}
  decision=vi_radar::Observe(job->radarState,config,observation.marker,1,1,observation.qpc,observation.sampledAt);job->radarLeaseAt=observation.sampledAt;job->radarLastQpc=observation.qpc;++job->radarObservationCount;
  if(job->radarTrace.size()==16){job->radarTrace.erase(job->radarTrace.begin());++job->radarTraceDropped;}job->radarTrace.push_back({observation.qpc,decision});
  if(decision.done){const bool normal=decision.reason=="radar_bearing_aligned"||decision.reason=="radar_marker_near_centre"||decision.reason=="no_marker_progress";JobFinish(*job,normal?"completed":"cancelled",decision.reason.c_str());break;}
  next=HousekeepingNow()+(ULONGLONG)poll;
 }
 std::lock_guard<std::recursive_mutex> lock(g_inputMx);return JobReplyLocked(*job);
}
