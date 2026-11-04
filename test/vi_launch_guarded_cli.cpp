// Compile against an explicitly pinned source root, never use a retail CLI here.
#define wmain ViLaunchEntry
#include "vinput_cli.cpp"
#undef wmain
#include <share.h>
#include <cstdlib>
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include "vi_launch_spawn.h"
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
static bool GuardedArguments(int count,wchar_t** values){
 wchar_t target[32768]{},allowedDll[32768]{};
 if(!GetEnvironmentVariableW(L"VI_LAUNCH_TARGET",target,32768)||!GetEnvironmentVariableW(L"VI_LAUNCH_DLL",allowedDll,32768))return false;
 int commandIndex=1;if(commandIndex<count&&!wcscmp(values[commandIndex],L"--json"))++commandIndex;
 if(commandIndex>=count)return false;
 const std::string command=W2U(values[commandIndex]);if(command!="launch"&&command!="inject"&&command!="status")return false;
 std::vector<std::string> options;for(int i=commandIndex+1;i<count;++i)options.push_back(W2U(values[i]));
 const Args parsed=SplitArgs(options);if(!parsed.pos.empty())return false;std::set<std::string> seen;
 for(const auto& option:parsed.opt){
  if(!seen.insert(option.first).second)return false;
  const bool allowed=command=="launch"?(option.first=="exe"||option.first=="args"||option.first=="dll"||option.first=="settle"||option.first=="fg-guard"||option.first=="timeout"||option.first=="no-inject"||option.first=="allow-dialog"):(option.first=="pid"||option.first=="dll"||option.first=="hooks-only"||option.first=="quiet");
  if(!allowed)return false; // includes every spelling/value form of early/Steam/AUMID
 }
 const std::wstring dll=U2W(GetOpt(parsed,"dll"));if(!dll.empty()&&_wcsicmp(dll.c_str(),allowedDll))return false;
 if(command=="launch")return !_wcsicmp(U2W(GetOpt(parsed,"exe")).c_str(),target)&&GetOpt(parsed,"fg-guard")=="0"&&GetOpt(parsed,"settle")=="0"&&GetOpt(parsed,"timeout")=="10";
 const std::string pid=GetOpt(parsed,"pid");char* end=nullptr;const auto id=strtoul(pid.c_str(),&end,10);if(!id||!end||*end)return false;
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,id);if(!process)return false;
 wchar_t image[32768]{};DWORD size=32768;FILETIME created{},exited{},kernel{},user{};
 const bool same=WaitForSingleObject(process,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(process,0,image,&size)&&!_wcsicmp(image,target)&&GetProcessTimes(process,&created,&exited,&kernel,&user)&&OwnTargetLedger(id,created);CloseHandle(process);return same;
}
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(!vi_launch_ack::ArmDeadline())return 111;
 wchar_t folder[2048]{};const DWORD n=GetEnvironmentVariableW(L"VI_LAUNCH_GUARD_DIR",folder,2048);
 if(!n||n>=2048){fprintf(stderr,"guarded CLI requires its private guard receipt directory\n");return 111;}
 wchar_t file[2200]{};swprintf_s(file,L"%s\\cli_%lu.jsonl",folder,GetCurrentProcessId());
 FILE* out=_wfsopen(file,L"w",_SH_DENYNO);if(!out)return 111;
 const bool installed=vi_launch_terminal::Install()&&vi_launch_terminal::Verify();
 vi_launch_terminal::Record(out,"before_cli_entry",installed);
 if(!installed){fclose(out);return 111;}
 if(!GuardedArguments(argc,argv)||!vi_launch_spawn::Install(out)||!vi_launch_ack::Wait(out,L"cli")){fclose(out);return 111;}
 // Every physical/foreground/queue/capture write from the CLI, including any
 // restore attempt and delegated x86 CLI, reaches an absorber installed first.
 const int result=ViLaunchEntry(argc,argv);
 vi_launch_terminal::Record(out,"after_cli_entry",installed);fclose(out);return result;
}
