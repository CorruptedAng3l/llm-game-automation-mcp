// Fresh full-readiness parity; no auto-retarget or selector-policy change.
#include "vi_launch_live.h"
static bool LaunchRetainedProcess(DWORD pid,HANDLE process,unsigned long long born){
 if(!process||GetProcessId(process)!=pid||WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
 FILETIME b{},e{},k{},u{};if(!born||!GetProcessTimes(process,&b,&e,&k,&u))return false;ULARGE_INTEGER stamp{};stamp.LowPart=b.dwLowDateTime;stamp.HighPart=b.dwHighDateTime;if(stamp.QuadPart!=born)return false;
 return true;
}
static bool LaunchLiveWindow(const std::string& text,DWORD pid,HWND expected,HANDLE process,unsigned long long born){
 std::uint64_t raw=0;if(!LaunchRetainedProcess(pid,process,born)||!vi_launch_live::QueuePending(text,pid,raw)||raw>(std::numeric_limits<ULONG_PTR>::max)())return false;
 const auto window=(HWND)(ULONG_PTR)raw;DWORD owner=0;return window==expected&&IsWindow(window)&&GetWindowThreadProcessId(window,&owner)&&owner==pid;
}
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
