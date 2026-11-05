static bool CursorGuardHandshakeReady(const std::string& response){vi_ready_json::Reader parsed{response,0,{}};bool ready=false,ok=false,hooks=false,keyboard=false;if(!parsed.Parse()||!parsed.Bool("ok",ok)||!ok||!parsed.Bool("hook_ready",hooks)||!hooks||!parsed.Bool("cursor_hooks_ready",ready)||!ready||!parsed.Bool("keybd_event_hooks_ready",keyboard)||!keyboard)return false;auto capability=parsed.fields.find("keybd_event_guard_version");if(capability==parsed.fields.end()||capability->second.type!='n'||capability->second.raw!="1")return false;auto version=parsed.fields.find("cursor_guard_version");return version!=parsed.fields.end()&&version->second.type=='n'&&version->second.raw=="1";}
static bool CursorGuardSpecialCommand(const std::string& command){for(const char* name:{"seq","sequence","hold-while","pad-hold-while","click-text","click-image","scroll","drag","placement-drag"})if(command==name)return true;return false;}
static bool NeedsCursorGuard(const std::string& request){
 vi_ready_json::Reader parsed{request,0,{}};if(!parsed.Parse())return true;auto field=parsed.fields.find("cmd");if(field==parsed.fields.end()||field->second.type!='"')return false;vi_ready_json::Reader token{field->second.raw,0,{}};std::string command;if(!token.String(command)||token.at!=field->second.raw.size())return true;
 if(command=="pad"){auto names=parsed.fields.find("buttons");if(names!=parsed.fields.end()&&names->second.type=='"'){vi_ready_json::Reader text{names->second.raw,0,{}};std::string value;if(text.String(value)){for(char& c:value)c=(char)tolower((unsigned char)c);if(value=="off")return false;}}}
 for(const char* producer:{"key","hold","pulse","down","type","seq","sequence","mouse","move","click","mousedown","drag","placement-drag","wheel","pad","pad-ramp","pad-timeline"})if(command==producer)return true;return false;
}
static bool CursorKeybdCapabilityReady(const std::string& response){vi_ready_json::Reader parsed{response,0,{}};bool keyboard=false;if(!parsed.Parse()||!parsed.Bool("keybd_event_hooks_ready",keyboard)||!keyboard)return false;auto version=parsed.fields.find("keybd_event_guard_version");return version!=parsed.fields.end()&&version->second.type=='n'&&version->second.raw=="1";}
static std::string CursorGuardHandshakeFailureText(const std::string& response){
 vi_ready_json::Reader parsed{response,0,{}};bool keyboard=false;
 if(!parsed.Parse())return "cursor/keybd_event readiness response malformed; no input was sent";
 auto version=parsed.fields.find("keybd_event_guard_version");if(version==parsed.fields.end()||version->second.type!='n'||version->second.raw!="1"||!parsed.Bool("keybd_event_hooks_ready",keyboard))return "target lacks the required typed keybd_event guard capability; no input was sent; restart the owned target with the updated DLL";
 if(!keyboard)return "required keybd_event guard is unarmed or entry coverage changed; no input was sent";
 return "matching legacy cursor safety guards are not armed; no input was sent";
}
static thread_local std::string g_cursorGuardFailure;
static std::string CursorGuardFailureText(){return g_cursorGuardFailure.empty()?"matching cursor safety/keybd_event guards are not armed; no input was sent":g_cursorGuardFailure;}
// An anti-cheat refusal from the target is the real reason and is passed through
// unchanged (no input was sent either way); every other not-ready reply gets a typed reason.
static bool RequireCursorGuard(DWORD pid,std::string* refusal=nullptr){
 std::string response;if(!ReadinessTransport(pid,"{\"cmd\":\"readiness\",\"hooks_only\":true,\"wait_ms\":0}",1000,response)){g_cursorGuardFailure="cursor/keybd_event safety readiness transport unavailable; no input was sent";return false;}
 if(CursorGuardHandshakeReady(response)){g_cursorGuardFailure.clear();return true;}
 if(refusal){vi_ready_json::Reader parsed{response,0,{}};bool flagged=false;if(parsed.Parse()&&parsed.Bool("anticheat_refused",flagged)&&flagged)*refusal=response;}
 g_cursorGuardFailure=CursorGuardHandshakeFailureText(response);return false;
}
