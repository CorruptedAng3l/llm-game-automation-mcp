#include "vi_key_sequence.h"
#include "vi_ready_json.h"
static Reply CmdKeySequence(DWORD pid,const Args& args) {
 if(args.pos.size()!=1)return Fail("seq needs one sequence file");
 for(const auto& option:args.opt)if(option.first!="replace"&&option.first!="guard-ms"&&option.first!="scope-json"&&option.first!="start-qpc"&&option.first!="start-frequency"&&option.first!="max-lateness-ms")return Fail("seq accepts --replace, --guard-ms, --scope-json, --start-qpc, --start-frequency and --max-lateness-ms; it always returns a background job ID");
 for(const char* name:{"replace","guard-ms","scope-json","start-qpc","start-frequency","max-lateness-ms"}){unsigned n=0;for(const auto& option:args.opt)if(option.first==name)++n;if(n>1)return Fail(std::string("duplicate seq --")+name);}
 const std::string replacement=GetOpt(args,"replace","false");if(replacement!="true"&&replacement!="false")return Fail("seq --replace must be true or false; no input was injected");
 unsigned guard=0;if(HasOpt(args,"guard-ms")&&(!vi_key_sequence::Milliseconds(GetOpt(args,"guard-ms"),10000,guard)||guard<200))return Fail("seq --guard-ms must be an integer200..10000; no input was injected");
 const auto scope=GetOpt(args,"scope-json");if(HasOpt(args,"scope-json")&&(scope.size()<2||scope.size()>2048||scope.find('\0')!=std::string::npos))return Fail("seq --scope-json needs one bounded scope receipt; no input was injected");
 unsigned lateness=0;if(HasOpt(args,"max-lateness-ms")&&(!vi_key_sequence::Milliseconds(GetOpt(args,"max-lateness-ms"),5000,lateness)||!lateness))return Fail("seq --max-lateness-ms requires integer1..5000; no input was injected");
 const auto startQpc=GetOpt(args,"start-qpc"),startFrequency=GetOpt(args,"start-frequency");const bool anchored=HasOpt(args,"start-qpc")||HasOpt(args,"start-frequency");
 auto decimal=[](const std::string& s){return !s.empty()&&s.size()<=20&&s[0]!='0'&&s.find_first_not_of("0123456789")==std::string::npos;};
 if(anchored&&(scope.empty()||!decimal(startQpc)||!decimal(startFrequency)))return Fail("seq event anchor needs --scope-json plus decimal --start-qpc and --start-frequency; no input was injected");
 FILE* file=nullptr;_wfopen_s(&file,U2W(args.pos[0]).c_str(),L"rb");if(!file)return Fail("cannot open sequence file");
 std::string script;char buffer[4096];size_t count=0;while((count=fread(buffer,1,sizeof(buffer),file))!=0){script.append(buffer,count);if(script.size()>32768)break;}const bool readError=ferror(file)!=0;fclose(file);if(readError)return Fail("cannot read sequence file");
 if(script.compare(0,3,"\xEF\xBB\xBF")==0)script.erase(0,3);
 vi_key_sequence::Plan plan;std::string error;if(!vi_key_sequence::Parse(script,plan,error))return Fail(error);
 std::string status;if(!Request(pid,"{\"cmd\":\"status\"}",status)||JsonNum(status,"key_sequence_version")<1)return Fail("target lacks key sequence jobs; restart the owned target with the matching updated DLL; no input was injected");
 if(plan.absolute&&JsonNum(status,"sequence_timing_version")<1)return Fail("absolute at schedules require sequence_timing_version >=1; no input was injected");
 if((guard||!scope.empty()||lateness||anchored)&&JsonNum(status,"sequence_guard_version")<1)return Fail("conditional sequences require sequence_guard_version >=1; no input was injected");
 if(!scope.empty()&&JsonNum(status,"input_scope_version")<1)return Fail("conditional sequences require input_scope_version >=1; no input was injected");
 std::string command="{\"cmd\":\"sequence\",\"script\":\""+JEsc(script)+"\",\"async\":true";if(replacement=="true")command+=",\"replace\":true";if(guard)command+=",\"guard_ms\":"+std::to_string(guard);if(!scope.empty())command+=",\"expected_capture_scope\":\""+JEsc(scope)+"\"";if(lateness)command+=",\"max_lateness_ms\":"+std::to_string(lateness);if(anchored)command+=",\"start_qpc_ticks\":\""+startQpc+"\",\"start_qpc_frequency\":\""+startFrequency+"\"";command+='}';
 std::string reply;const bool received=BoundedJobRequest(pid,command,6500,reply);vi_ready_json::Reader parsed{reply,0,{}};bool ok=false;
 if(received&&parsed.Parse()&&parsed.Bool("ok",ok)){
  if(!ok)return {false,reply};
  bool accepted=false;auto id=parsed.fields.find("job_id");
  if(parsed.Bool("accepted",accepted)&&accepted&&id!=parsed.fields.end()&&id->second.type=='"'&&id->second.raw.size()>2)return {true,reply};
 }
 return {false,"{\"ok\":false,\"input_injected\":null,\"delivery_unknown\":true,\"may_have_started\":true,\"error\":\"sequence acknowledgement unavailable or invalid; no input was retried; any accepted job retains its target-side120-second lifetime cap\",\"reply_raw\":\""+JobRawBytes(reply)+"\",\"raw_encoding\":\"json-u00-byte-escapes\"}"};
}
