static bool CursorGuardHandshakeReady(const std::string& response){vi_ready_json::Reader parsed{response,0,{}};bool ready=false,ok=false,hooks=false;if(!parsed.Parse()||!parsed.Bool("ok",ok)||!ok||!parsed.Bool("hook_ready",hooks)||!hooks||!parsed.Bool("cursor_hooks_ready",ready)||!ready)return false;auto version=parsed.fields.find("cursor_guard_version");return version!=parsed.fields.end()&&version->second.type=='n'&&version->second.raw=="1";}
static bool CursorGuardSpecialCommand(const std::string& command){for(const char* name:{"seq","sequence","hold-while","pad-hold-while","click-text","click-image","scroll","drag","placement-drag"})if(command==name)return true;return false;}
static bool NeedsCursorGuard(const std::string& request){
 vi_ready_json::Reader parsed{request,0,{}};if(!parsed.Parse())return true;auto field=parsed.fields.find("cmd");if(field==parsed.fields.end()||field->second.type!='"')return false;vi_ready_json::Reader token{field->second.raw,0,{}};std::string command;if(!token.String(command)||token.at!=field->second.raw.size())return true;
 if(command=="pad"){auto names=parsed.fields.find("buttons");if(names!=parsed.fields.end()&&names->second.type=='"'){vi_ready_json::Reader text{names->second.raw,0,{}};std::string value;if(text.String(value)){for(char& c:value)c=(char)tolower((unsigned char)c);if(value=="off")return false;}}}
 for(const char* producer:{"key","hold","pulse","down","type","seq","sequence","mouse","move","click","mousedown","drag","placement-drag","wheel","pad","pad-ramp","pad-timeline"})if(command==producer)return true;return false;
}
// An anti-cheat refusal from the target is the real reason and is passed through
// unchanged (no input was sent either way); every other not-ready reply stays generic.
static bool RequireCursorGuard(DWORD pid,std::string* refusal=nullptr){std::string response;const bool received=ReadinessTransport(pid,"{\"cmd\":\"readiness\",\"hooks_only\":true,\"wait_ms\":0}",1000,response);
 if(received&&CursorGuardHandshakeReady(response))return true;
 if(refusal&&received){vi_ready_json::Reader parsed{response,0,{}};bool flagged=false;if(parsed.Parse()&&parsed.Bool("anticheat_refused",flagged)&&flagged)*refusal=response;}
 return false;}
