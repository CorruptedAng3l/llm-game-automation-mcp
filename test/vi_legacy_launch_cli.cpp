// Exact-plan test wrapper around the pinned production CLI. No retail binary.
#define wmain ViLegacyEntry
#include "vinput_cli.cpp"
#undef wmain
#include <share.h>
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include "vi_launch_spawn.h"
static std::wstring Env(const wchar_t* key){wchar_t value[32768]{};const DWORD n=GetEnvironmentVariableW(key,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring();}
static bool OwnTargetLedger(DWORD pid,const FILETIME& created){
 wchar_t path[2048]{},nonce[40]{};
 if(!GetEnvironmentVariableW(L"VI_LAUNCH_TARGET_LOG",path,2048)||GetEnvironmentVariableW(L"VI_LAUNCH_CASE_NONCE",nonce,40)!=32)return false;
 FILE* input=_wfsopen(path,L"rb",_SH_DENYNO);if(!input)return false;
 char bytes[16385]{};const size_t size=fread(bytes,1,16384,input);const bool valid=!ferror(input)&&feof(input);fclose(input);if(!valid)return false;
 ULARGE_INTEGER filetime{};filetime.LowPart=created.dwLowDateTime;filetime.HighPart=created.dwHighDateTime;
 bool identity=false,scope=false;std::string text(bytes,size);size_t start=0;
 while(start<text.size()){
  auto end=text.find('\n',start);if(end==std::string::npos)break;
  std::string line=text.substr(start,end-start);if(!line.empty()&&line.back()=='\r')line.pop_back();start=end+1;
  vi_ready_json::Reader row{line,0,{}};if(!row.Parse())return false;
  auto event=row.fields.find("event"),process=row.fields.find("pid");
  if(event==row.fields.end()||process==row.fields.end()||process->second.type!='n'||process->second.raw!=std::to_string(pid))continue;
  auto stamp=row.fields.find("creation_filetime");
  if(event->second.raw=="\"owned_identity\""&&stamp!=row.fields.end()&&stamp->second.type=='n'&&stamp->second.raw==std::to_string(filetime.QuadPart))identity=true;
  auto token=row.fields.find("case_nonce"),role=row.fields.find("role");
  if(event->second.raw=="\"guard_wait\""&&token!=row.fields.end()&&token->second.raw=="\""+W2U(nonce)+"\""&&role!=row.fields.end()&&role->second.raw=="\"target\"")scope=true;
 }
 return identity&&scope;
}
static bool LegacyRootArgs(int argc,wchar_t** argv){
 const std::wstring count=Env(L"VI_LEGACY_ARGC");if(count!=std::to_wstring(argc-1)||argc<2||argc>32)return false;
 for(int i=1;i<argc;++i){const auto key=L"VI_LEGACY_ARG_"+std::to_wstring(i-1);if(Env(key.c_str())!=argv[i])return false;}
 int command=1;if(!wcscmp(argv[command],L"--json"))++command;if(command>=argc||wcscmp(argv[command],L"launch"))return false;
 std::vector<std::string> values;for(int i=command+1;i<argc;++i)values.push_back(W2U(argv[i]));const auto parsed=SplitArgs(values);if(!parsed.pos.empty())return false;
 const std::set<std::string> allowed={"json","exe","args","dll","timeout","settle","fg-guard","no-inject","lock","owner","lock-retry","lock-max-wait"};std::set<std::string> seen;
 for(const auto& option:parsed.opt)if(!allowed.count(option.first)||!seen.insert(option.first).second)return false;
 return true; // The creator guard separately permits only the one built target.
}
static bool LegacyDelegatedArgs(int argc,wchar_t** argv){
 if(argc<4||wcscmp(argv[1],L"inject"))return false;std::vector<std::string> values;for(int i=2;i<argc;++i)values.push_back(W2U(argv[i]));const auto parsed=SplitArgs(values);if(!parsed.pos.empty())return false;
 const std::set<std::string> allowed={"pid","dll","hooks-only","quiet","readiness-wait-ms"};std::set<std::string> seen;for(const auto& option:parsed.opt)if(!allowed.count(option.first)||!seen.insert(option.first).second)return false;
 const auto id=GetOpt(parsed,"pid");char* end=nullptr;const auto pid=strtoul(id.c_str(),&end,10);if(!pid||!end||*end)return false;
 const auto target=Env(L"VI_LAUNCH_TARGET");HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!h)return false;wchar_t image[32768]{};DWORD size=32768;FILETIME b{},e{},k{},u{};bool valid=GetProcessId(h)==pid&&WaitForSingleObject(h,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(h,0,image,&size)&&size&&size<32768&&!_wcsicmp(image,target.c_str())&&GetProcessTimes(h,&b,&e,&k,&u);CloseHandle(h);if(!valid||!OwnTargetLedger(pid,b))return false;
 // The root creator journal plus target nonce/creation wait is independently
 // validated by the harness before this delegated CLI receives its ACK.
 const auto requested=U2W(GetOpt(parsed,"dll"));return requested.empty()||!_wcsicmp(requested.c_str(),Env(L"VI_LEGACY_DLL").c_str());
}
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);if(!vi_launch_ack::ArmDeadline())return 111;
 const auto folder=Env(L"VI_LAUNCH_GUARD_DIR");if(folder.empty())return 111;const auto path=folder+L"\\cli_"+std::to_wstring(GetCurrentProcessId())+L".jsonl";FILE* out=_wfsopen(path.c_str(),L"w",_SH_DENYNO);if(!out)return 111;
 const bool safe=vi_launch_terminal::Install()&&vi_launch_terminal::Verify();vi_launch_terminal::Record(out,"before_cli_entry",safe);if(!safe){fclose(out);return 111;}
 const auto root=Env(L"VI_LEGACY_ROOT_PID");const bool delegated=!root.empty();if(!(delegated?LegacyDelegatedArgs(argc,argv):LegacyRootArgs(argc,argv))){fprintf(stderr,"guarded legacy CLI rejected %s arguments\n",delegated?"delegated":"root");fclose(out);return 111;}
 if(!vi_launch_spawn::Install(out)||!vi_launch_ack::Wait(out,L"cli")){fprintf(stderr,"guarded legacy CLI spawn/ACK prerequisite failed\n");fclose(out);return 111;}
 if(!delegated)SetEnvironmentVariableW(L"VI_LEGACY_ROOT_PID",std::to_wstring(GetCurrentProcessId()).c_str());
 const int result=ViLegacyEntry(argc,argv);vi_launch_terminal::Record(out,"after_cli_entry",safe);fclose(out);return result;
}
