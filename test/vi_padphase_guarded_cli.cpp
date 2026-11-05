// Exact production CLI with terminal desktop writers installed before entry.
#define wmain ViPadPhaseEntry
#include "vinput_cli.cpp"
#undef wmain
#include "vi_launch_terminal_batch.h"
#include "vi_launch_ack.h"
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 wchar_t batchOption[8]{};const DWORD optionLength=GetEnvironmentVariableW(L"VI_PHASE_BATCH_GUARDS",batchOption,8);if(optionLength&&(optionLength!=1||(batchOption[0]!=L'0'&&batchOption[0]!=L'1')))return 111;
 const bool batched=!optionLength||batchOption[0]!=L'0';LARGE_INTEGER before{},after{},frequency{};QueryPerformanceCounter(&before);QueryPerformanceFrequency(&frequency);
 if(!vi_launch_ack::ArmDeadline()||!vi_launch_terminal_batch::Install(batched)||!vi_launch_terminal_batch::Verify()||!vi_launch_ack::ArmNoActivation())return 111;
 QueryPerformanceCounter(&after);fprintf(stderr,"PHASE_GUARDS batch=%d verified_exports=%u setup_ms=%.3f\n",batched?1:0,vi_launch_terminal_batch::verifiedExports,1000.0*(after.QuadPart-before.QuadPart)/frequency.QuadPart);
 wchar_t name[160]{};if(!GetEnvironmentVariableW(L"VI_PHASE_START_EVENT",name,160))return 111;HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,name);if(!event)return 111;
 const DWORD acknowledged=WaitForSingleObject(event,5000);CloseHandle(event);if(acknowledged!=WAIT_OBJECT_0)return 111;
 // Bind the adapter to the runner's freshly retained process, before any inject.
 wchar_t pidText[32]{},createdText[32]{},expected[32768]{};
 if(argc<5||wcscmp(argv[1],L"--json")||wcscmp(argv[2],L"--pid")||!GetEnvironmentVariableW(L"VI_PHASE_TARGET_PID",pidText,32)||wcscmp(argv[3],pidText)||!GetEnvironmentVariableW(L"VI_PHASE_TARGET_CREATED",createdText,32)||!GetEnvironmentVariableW(L"VI_PHASE_TARGET_IMAGE",expected,32768))return 111;
 const DWORD pid=wcstoul(pidText,nullptr,10);HANDLE target=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!target)return 111;
 struct Held{HANDLE value;~Held(){CloseHandle(value);}} held{target};wchar_t path[32768]{};DWORD length=32768;FILETIME created{},exited{},kernel{},user{};ULARGE_INTEGER stamp{};
 if(WaitForSingleObject(target,0)!=WAIT_TIMEOUT||!GetProcessTimes(target,&created,&exited,&kernel,&user)||!QueryFullProcessImageNameW(target,0,path,&length)||_wcsicmp(path,expected))return 111;
 stamp.LowPart=created.dwLowDateTime;stamp.HighPart=created.dwHighDateTime;if(stamp.QuadPart!=_wcstoui64(createdText,nullptr,10))return 111;
 const std::string command=W2U(argv[4]);bool allowed=false;for(const char* name:{"inject","status","job-status","job-cancel","pad","release","json"})if(command==name)allowed=true;
 if(command=="hold"||command=="scroll")for(int i=5;i+1<argc;++i)if(!wcscmp(argv[i],L"--stagger-ms")&&!wcscmp(argv[i+1],L"60"))allowed=true;
 if(!allowed)return 111;
 if(command=="json"){if(argc!=6)return 111;std::string text=W2U(argv[5]);vi_ready_json::Reader request{text,0,{}};if(!request.Parse())return 111;auto field=request.fields.find("cmd");if(field==request.fields.end()||field->second.type!='"')return 111;vi_ready_json::Reader token{field->second.raw,0,{}};std::string decoded;if(!token.String(decoded)||(decoded!="pad"&&decoded!="pad-timeline"))return 111;}
 const int result=ViPadPhaseEntry(argc,argv);
 return vi_launch_terminal_batch::Verify()?result:111;
}
