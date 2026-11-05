// Versioned map binding is enforced at the common Request boundary, including
// special drag/placement requests. Helpers never recurse through Request.
static bool CaptureScopeText(const vi_ready_json::Value& value,std::string& decoded){
 if(value.type!='"')return false;vi_ready_json::Reader token{value.raw,0,{}};
 std::string text;if(!token.String(text)||token.at!=value.raw.size())return false;
 decoded=std::move(text);return true;
}
static bool CaptureScopeTruthy(const vi_ready_json::Value& value){
 if(value.type=='b')return value.raw=="true";
 if(value.type=='n'){char* end=nullptr;const double number=strtod(value.raw.c_str(),&end);return end&&!*end&&number!=0;}
 std::string text;return CaptureScopeText(value,text)&&(text=="true"||text=="1"||text=="on");
}
static bool CaptureScopeSupported(DWORD pid){
 std::string response;const bool received=BoundedJobRequest(pid,"{\"cmd\":\"ping\",\"capture_scope_support\":true}",1000,response);
 vi_ready_json::Reader parsed{response,0,{}};bool ok=false;
 if(!received||!parsed.Parse()||!parsed.Bool("ok",ok)||!ok)return false;
 const auto version=parsed.fields.find("capture_scope_version");unsigned number=0;
 return version!=parsed.fields.end()&&version->second.type=='n'&&ExpectUnsigned(version->second.raw,1000,number)&&number>=1;
}
static bool CaptureScopeRefusal(std::string& response,const char* error,bool probed=true){
 response="{\"ok\":false,\"input_injected\":false,\"capture_scope_preflight\":true,\"capture_scope_support_checked\":"+std::string(probed?"true":"false")+",\"capture_scope_version_required\":1,\"coordinate_input_sent\":false,\"button_down_count\":0,\"cleanup_attempted\":false,\"error\":\""+JEsc(error)+"\"}";return true;
}
// Failure metadata is not authorization to send or clean up a partial request.
static std::string CaptureScopeParseError(const vi_ready_json::Reader& parsed){
 if(!parsed.duplicateKeyFound)return "malformed or over-limit outgoing JSON was refused locally; no original request was sent";
 const size_t limit=128;std::string error="duplicate decoded outgoing JSON property '"+JobRawBytes(parsed.duplicateKey.substr(0,limit))+"'";
 if(parsed.duplicateKey.size()>limit)error+=" [name truncated]";
 if(parsed.duplicateKey=="max_width")error+=" (CLI --max-width)";
 return error+=" was refused locally; no original request was sent";
}
// Preserve original key/value tokens when removing the decoded capture-space
// property. Strict parsing/unique-key validation already ran before this helper.
// Unknown Unicode keys must not be rewritten by the selector's ASCII key view.
static bool CaptureScopeWithoutSpace(const std::string& request,std::string& normalized){
 vi_ready_json::Reader cursor{request,0,{}};cursor.Space();
 if(cursor.at>=request.size()||request[cursor.at++]!='{')return false;
 std::string result="{";bool removed=false;
 for(;;){
  cursor.Space();if(cursor.at>=request.size())return false;
  if(request[cursor.at]=='}'){++cursor.at;cursor.Space();if(cursor.at!=request.size())return false;normalized=result+'}';return removed;}
  const size_t begin=cursor.at;std::string key;if(!cursor.String(key))return false;
  cursor.Space();if(cursor.at>=request.size()||request[cursor.at++]!=':')return false;
  cursor.Space();char type=0;if(!cursor.Any(1,type))return false;
  if(key=="space")removed=true;
  else{if(result.size()>1)result+=',';result.append(request,begin,cursor.at-begin);}
  cursor.Space();if(cursor.at>=request.size())return false;
  if(request[cursor.at]==','){++cursor.at;continue;}
  if(request[cursor.at]!='}')return false;
 }
}
// true means an explicit local reply was produced; false means send the admitted
// request once through the normal owner-stamping/overlay/transport path.
static bool CaptureScopePreflight(DWORD pid,std::string& request,std::string& response){
 vi_ready_json::Reader parsed{request,0,{}};
 if(!parsed.Parse()){const std::string error=CaptureScopeParseError(parsed);return CaptureScopeRefusal(response,error.c_str(),false);}
 const auto commandField=parsed.fields.find("cmd");std::string command;
 if(commandField==parsed.fields.end()||!CaptureScopeText(commandField->second,command))return CaptureScopeRefusal(response,"outgoing request needs one decoded string cmd; no original request was sent",false);
 const bool scopedPointer=parsed.fields.count("expected_capture_scope")&&(command=="click"||command=="mousedown"||command=="mouseup"||command=="mouse"||command=="move");
 if(!parsed.fields.count("expected_capture_scope")&&(parsed.fields.count("admit_before_qpc_ticks")||parsed.fields.count("admit_qpc_frequency"))&&
    (command=="click"||command=="mousedown"||command=="mouseup"||command=="mouse"||command=="move"))return CaptureScopeRefusal(response,"pointer admission deadlines require an expected capture scope; no input was sent",false);
 if(scopedPointer){
  std::string scope;if(!CaptureScopeText(parsed.fields.at("expected_capture_scope"),scope)||scope.empty())return CaptureScopeRefusal(response,"expected pointer scope must be a nonempty string; no pointer input was sent",false);
  std::string capability;const bool received=BoundedJobRequest(pid,"{\"cmd\":\"ping\",\"pointer_scope_support\":true}",1000,capability);vi_ready_json::Reader version{capability,0,{}};bool accepted=false;
  if(!received||!version.Parse()||!version.Bool("ok",accepted)||!accepted||version.fields.count("pointer_scope_version")==0||version.fields.at("pointer_scope_version").type!='n'||version.fields.at("pointer_scope_version").raw!="1"||version.fields.count("pid")==0||version.fields.at("pid").raw!=std::to_string(pid))return CaptureScopeRefusal(response,"target lacks exact pointer-scope enforcement or its capability was unconfirmed; no pointer input or cleanup was sent; restart with the matching DLL");
  if((parsed.fields.count("admit_before_qpc_ticks")||parsed.fields.count("admit_qpc_frequency"))&&
     (version.fields.count("input_admission_version")==0||version.fields.at("input_admission_version").type!='n'||version.fields.at("input_admission_version").raw!="1"))return CaptureScopeRefusal(response,"target lacks bounded pointer admission enforcement; no input was sent; restart with the matching DLL");
 }
 const bool positioned=parsed.fields.count("x")||parsed.fields.count("y");
 // Both motion commands branch on dx/dy presence, not their command name.
 // Without either delta, ResolveXY also maps implicit current x/y. With a
 // delta, norm/space do not scale device deltas, even when x/y are also present.
 const bool motion=command=="mouse"||command=="move";
 const bool relative=motion&&(parsed.fields.count("dx")||parsed.fields.count("dy"));
 const bool mapped=(motion&&!relative)||command=="placement-drag"||
  ((command=="click"||command=="mousedown"||command=="mouseup")&&positioned);
 if(!mapped)return false;
 const auto space=parsed.fields.find("space");if(space==parsed.fields.end())return false;
 std::string decodedSpace;if(!CaptureScopeText(space->second,decodedSpace))return CaptureScopeRefusal(response,"coordinate space must be a decoded string; no coordinate input was sent",false);
 if(decodedSpace!="capture")return false;
 const auto norm=parsed.fields.find("norm");
 if(norm!=parsed.fields.end()&&CaptureScopeTruthy(norm->second)){
  // Older DLLs preflight capture-map validity before applying norm precedence.
  // Canonicalize the already-admitted normalized request instead of sending an
  // irrelevant capture-space flag, retrying, or changing coordinate values.
  std::string normalized;
  if(!CaptureScopeWithoutSpace(request,normalized))return CaptureScopeRefusal(response,"normalized-coordinate request could not be canonicalized; no original input was sent",false);
  request=std::move(normalized);return false;
 }
 if(CaptureScopeSupported(pid))return false;
 if(command!="mouseup")return CaptureScopeRefusal(response,"target lacks capture-scope binding or its capability reply was unavailable; no coordinate input was sent; restart the owned target with the matching updated DLL");
 // Release the unambiguous selected button without coordinates, hover options
 // or stale mapping. Do not claim release merely because a request was attempted.
 std::string button="left";const auto selected=parsed.fields.find("button");
 if(selected!=parsed.fields.end()&&!CaptureScopeText(selected->second,button))return CaptureScopeRefusal(response,"invalid mouse button type; cleanup button was not guessed; send a separate coordinate-free mouseup");
 if(button!="left"&&button!="right"&&button!="middle"&&button!="x1"&&button!="x2")return CaptureScopeRefusal(response,"invalid mouse button; cleanup button was not guessed; send a separate coordinate-free mouseup");
 std::string cleanup="{";PutS(cleanup,"cmd","mouseup");PutS(cleanup,"button",button);
 for(const char* name:{"owner","force_claim"}){auto field=parsed.fields.find(name);if(field!=parsed.fields.end()){cleanup+=",\""+std::string(name)+"\":"+field->second.raw;}}
 // BoundedJobRequest applies StampOwner once to the wire, retaining global
 // --owner/VINPUT_OWNER and force-claim defaults as normal Request does. Do not
 // stamp here again: force-claim without an owner is not idempotent.
 cleanup+='}';std::string receipt;const bool received=BoundedJobRequest(pid,cleanup,1000,receipt);
 vi_ready_json::Reader cleaned{receipt,0,{}};bool cleanupOk=false;
 const bool acknowledged=received&&cleaned.Parse()&&cleaned.Bool("ok",cleanupOk)&&cleanupOk;
 response="{\"ok\":false,\"capture_scope_support_checked\":true,\"capture_scope_version_required\":1,\"coordinate_input_sent\":false,\"button_down_count\":0,\"cleanup_attempted\":true,\"cleanup_response_received\":"+std::string(received?"true":"false")+",\"cleanup_acknowledged\":"+(acknowledged?"true":"false")+",\"cleanup_raw\":\""+JobRawBytes(receipt)+"\",\"error\":\"capture-scope capability unavailable; positioned mouseup refused and coordinate-free cleanup attempted; actual button state requires a fresh target observation\"}";
 return true;
}
static bool CaptureScopeSpecialPreflight(DWORD pid,const std::string& command,const Args& args,std::string& response){
 if((command!="drag"&&command!="placement-drag")||!HasOpt(args,"capture-space"))return false;
 std::string request="{\"cmd\":\"mouse\",\"x\":0,\"y\":0,\"space\":\"capture\"";
 const bool legacy=command=="drag"&&!HasOpt(args,"placement");
 // Legacy drag already serializes norm=true whenever its norm option exists;
 // placement serializes the value. Preserve those existing precedence rules.
 if(legacy){if(HasOpt(args,"norm"))Put(request,"norm","true");}
 else for(const auto& option:args.opt)if(option.first=="norm")Put(request,"norm",option.second);
 request+='}';return CaptureScopePreflight(pid,request,response);
}
