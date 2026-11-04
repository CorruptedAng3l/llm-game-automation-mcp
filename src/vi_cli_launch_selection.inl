// Fresh full-readiness parity; no auto-retarget or selector-policy change.
static bool LaunchReadinessTarget(const std::string& text,DWORD pid,HWND expected,HWND& actual,bool& validJson){
 actual=nullptr;vi_ready_json::Reader parsed{text,0,{}};validJson=parsed.Parse();if(!validJson)return false;
 const auto ready=vi_ready_json::Parse(text);
 if(!ready.supported||!ready.ok||!ready.hooks||!ready.window||!ready.queue||!CursorGuardHandshakeReady(text))return false;
 auto field=parsed.fields.find("hwnd");if(field==parsed.fields.end()||field->second.type!='"')return false;
 std::string value;vi_ready_json::Reader encoded{field->second.raw,0,{}};if(!encoded.String(value)||value.empty())return false;
 char* end=nullptr;const auto raw=strtoull(value.c_str(),&end,0);if(!end||*end||!raw||raw>(std::numeric_limits<ULONG_PTR>::max)())return false;
 actual=(HWND)(ULONG_PTR)raw;DWORD owner=0;
 return GetWindowThreadProcessId(actual,&owner)&&owner==pid&&actual==expected;
}
