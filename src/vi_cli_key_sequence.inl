#include "vi_key_sequence.h"
#include "vi_ready_json.h"
static Reply CmdKeySequence(DWORD pid,const Args& args) {
 if(args.pos.size()!=1)return Fail("seq needs one sequence file");
 for(const auto& option:args.opt)if(option.first!="replace")return Fail("seq accepts only --replace; it always returns a background job ID");
 const std::string replacement=GetOpt(args,"replace","false");if(replacement!="true"&&replacement!="false")return Fail("seq --replace must be true or false; no input was injected");
 FILE* file=nullptr;_wfopen_s(&file,U2W(args.pos[0]).c_str(),L"rb");if(!file)return Fail("cannot open sequence file");
 std::string script;char buffer[4096];size_t count=0;while((count=fread(buffer,1,sizeof(buffer),file))!=0){script.append(buffer,count);if(script.size()>32768)break;}const bool readError=ferror(file)!=0;fclose(file);if(readError)return Fail("cannot read sequence file");
 if(script.compare(0,3,"\xEF\xBB\xBF")==0)script.erase(0,3);
 vi_key_sequence::Plan plan;std::string error;if(!vi_key_sequence::Parse(script,plan,error))return Fail(error);
 std::string status;if(!Request(pid,"{\"cmd\":\"status\"}",status)||JsonNum(status,"key_sequence_version")<1)return Fail("target lacks key sequence jobs; restart the owned target with the matching updated DLL; no input was injected");
 std::string command="{\"cmd\":\"sequence\",\"script\":\""+JEsc(script)+"\",\"async\":true";if(replacement=="true")command+=",\"replace\":true";command+='}';
 std::string reply;const bool received=BoundedJobRequest(pid,command,6500,reply);vi_ready_json::Reader parsed{reply,0,{}};bool ok=false;
 if(received&&parsed.Parse()&&parsed.Bool("ok",ok)){
  if(!ok)return {false,reply};
  bool accepted=false;auto id=parsed.fields.find("job_id");
  if(parsed.Bool("accepted",accepted)&&accepted&&id!=parsed.fields.end()&&id->second.type=='"'&&id->second.raw.size()>2)return {true,reply};
 }
 return {false,"{\"ok\":false,\"input_injected\":null,\"delivery_unknown\":true,\"may_have_started\":true,\"error\":\"sequence acknowledgement unavailable or invalid; no input was retried; any accepted job retains its target-side120-second lifetime cap\",\"reply_raw\":\""+JobRawBytes(reply)+"\",\"raw_encoding\":\"json-u00-byte-escapes\"}"};
}
