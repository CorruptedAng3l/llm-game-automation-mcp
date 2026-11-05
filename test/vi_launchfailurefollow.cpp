// Real owned process tree and Launch, terminal desktop writers, and a finite
// readiness-pipe surrogate in the selected child. No loader thread ever starts.
#include <windows.h>
static HANDLE WINAPI RefuseLoader(HANDLE,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD);
static BOOL WINAPI TrackedCreate(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
#define CreateRemoteThread RefuseLoader
#define CreateProcessW TrackedCreate
#define wmain ViFailureFollowCliEntry
#include <vinput_cli.cpp>
#undef wmain
#undef CreateRemoteThread
#undef CreateProcessW
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include <io.h>
#include <share.h>
static unsigned passed=0,failed=0,loaderAttempts=0;
static std::wstring actorMode,actorReceipt;
static HWND actorWindow=nullptr;
struct OwnedActor{HANDLE process=nullptr;DWORD pid=0;unsigned long long born=0;};static std::vector<OwnedActor> ownedActors;
static BOOL WINAPI TrackedCreate(LPCWSTR app,LPWSTR command,LPSECURITY_ATTRIBUTES ps,LPSECURITY_ATTRIBUTES ts,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process){
 wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);if(!app||_wcsicmp(app,own)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 if(!CreateProcessW(app,command,ps,ts,inherit,flags|CREATE_NO_WINDOW,env,cwd,startup,process))return FALSE;
 HANDLE retained=nullptr;FILETIME born{},ended{},kernel{},user{};ULARGE_INTEGER stamp{};DWORD id=GetProcessId(process->hProcess);
 const bool known=id==process->dwProcessId&&GetProcessTimes(process->hProcess,&born,&ended,&kernel,&user)&&DuplicateHandle(GetCurrentProcess(),process->hProcess,GetCurrentProcess(),&retained,0,FALSE,DUPLICATE_SAME_ACCESS);stamp.LowPart=born.dwLowDateTime;stamp.HighPart=born.dwHighDateTime;
 if(!known){TerminateProcess(process->hProcess,111);WaitForSingleObject(process->hProcess,1000);CloseHandle(process->hThread);CloseHandle(process->hProcess);*process={};SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
 ownedActors.push_back({retained,id,stamp.QuadPart});return TRUE;
}
static bool AccountActors(){bool good=true;for(auto& actor:ownedActors){DWORD code=999;FILETIME born{},ended{},kernel{},user{};ULARGE_INTEGER stamp{};
 const bool exited=WaitForSingleObject(actor.process,10000)==WAIT_OBJECT_0;const bool known=GetProcessId(actor.process)==actor.pid&&GetProcessTimes(actor.process,&born,&ended,&kernel,&user)&&GetExitCodeProcess(actor.process,&code);stamp.LowPart=born.dwLowDateTime;stamp.HighPart=born.dwHighDateTime;
 if(!exited){TerminateProcess(actor.process,111);WaitForSingleObject(actor.process,1000);}good=good&&exited&&known&&stamp.QuadPart==actor.born&&code==0;CloseHandle(actor.process);}ownedActors.clear();return good;}
static void Check(bool good,const char* name){printf("%s %s\n",good?"PASS":"FAIL",name);good?++passed:++failed;}
static bool FollowPolicy(DWORD pid,unsigned long long born,const std::string& raw){
#ifdef VI_LAUNCH_RETRY_POLICY
 return vi_launch_injection::MayFollowPreLoader(pid,born,raw);
#else
 (void)pid;(void)born;(void)raw;return false; // A baseline without this authorizer grants no continuation.
#endif
}
static bool SpawnChild(const std::wstring& suffix){
 const auto image=ExeDir()+L"\\vi_launchfailurefollow.exe";auto command=L"\""+image+L"\" --child \""+actorReceipt+suffix+L"\"";
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
 if(!TrackedCreate(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return false;
 CloseHandle(child.hThread);CloseHandle(child.hProcess);return true;
}
static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+41){if(actorMode!=L"sibling"&&!GetPropW(h,L"vi.failure.spawned")){SetPropW(h,L"vi.failure.spawned",(HANDLE)1);SpawnChild(L".child");if(actorMode==L"ambiguous")SpawnChild(L".second");}return 0;}
 return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);
}
static HANDLE WINAPI RefuseLoader(HANDLE process,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD){
 const auto pid=GetProcessId(process);wchar_t own[32768]{},image[32768]{};DWORD size=32768;
 GetModuleFileNameW(nullptr,own,32768);HWND window=VisibleWindowOf(pid);
 const bool exact=pid&&pid==g_initialLaunchPid&&pid==g_launchPid&&QueryFullProcessImageNameW(process,0,image,&size)&&!_wcsicmp(own,image)&&window&&WindowClass(window)=="ViFailureRoot";
 if(exact){++loaderAttempts;PostMessageW(window,WM_APP+41,0,0);}SetLastError(ERROR_ACCESS_DENIED);return nullptr;
}
static DWORD WINAPI Pipe(void*){
 const auto name=L"\\\\.\\pipe\\vinput_"+std::to_wstring(GetCurrentProcessId());
 for(unsigned requests=0;requests<16;++requests){
  HANDLE pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,16384,16384,0,nullptr);if(pipe==INVALID_HANDLE_VALUE)return 1;
  if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){std::string request;char c=0;DWORD count=0;while(request.size()<8192&&ReadFile(pipe,&c,1,&count,nullptr)&&count&&c!='\n')request+=c;
   char handle[32];sprintf_s(handle,"0x%llX",(unsigned long long)(ULONG_PTR)actorWindow);
   const std::string reply="{\"ok\":true,\"pid\":"+std::to_string(GetCurrentProcessId())+",\"hwnd\":\""+handle+"\",\"readiness_version\":1,\"hook_ready\":true,\"cursor_hooks_ready\":true,\"cursor_guard_version\":1,\"keybd_event_guard_version\":1,\"keybd_event_hooks_ready\":true,\"window_ready\":true,\"queue_ready\":true,\"frames\":0}\n";
   WriteFile(pipe,reply.data(),(DWORD)reply.size(),&count,nullptr);FlushFileBuffers(pipe);
  }DisconnectNamedPipe(pipe);CloseHandle(pipe);
 }return 0;
}
static int Actor(int argc,wchar_t** argv){
 if(argc<3)return 111;const bool child=!wcscmp(argv[1],L"--child");if(!child&&argc!=4)return 111;
 actorMode=child?L"child":argv[2];actorReceipt=argv[child?2:3];
 if(!vi_launch_ack::ArmNoActivation())return 111;
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=child?L"ViFailureChild":L"ViFailureRoot";if(!RegisterClassW(&cls))return 111;
 actorWindow=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Owned delayed follow",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,400,250,nullptr,nullptr,cls.hInstance,nullptr);if(!actorWindow)return 111;
 FILE* out=nullptr;_wfopen_s(&out,actorReceipt.c_str(),L"w");if(!out)return 111;fprintf(out,"%lu\n",GetCurrentProcessId());fclose(out);
 if(child){HANDLE thread=CreateThread(nullptr,0,Pipe,nullptr,0,nullptr);if(!thread)return 111;CloseHandle(thread);}
 const auto until=GetTickCount64()+5000;while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}DestroyWindow(actorWindow);return AccountActors()?0:111;
}
static std::string Read(const std::wstring& path){FILE* file=_wfsopen(path.c_str(),L"rb",_SH_DENYNO);if(!file)return {};char bytes[65537]{};const auto count=fread(bytes,1,65536,file);const bool valid=feof(file)&&!ferror(file);fclose(file);return valid?std::string(bytes,count):std::string{};}
static DWORD Published(const std::wstring& path){const auto value=Read(path);return value.empty()?0:(DWORD)strtoul(value.c_str(),nullptr,10);}
int wmain(int argc,wchar_t** argv){
 if(!vi_launch_ack::ArmDeadline()||!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 if(argc==2&&!wcscmp(argv[1],L"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
 else if(argc>1)return Actor(argc,argv);
 wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);
 const auto known=std::string("{\"ok\":false,\"pid\":123,\"process_created\":\"987\",\"injected\":false,\"loader_thread_started\":false,\"failure_stage\":\"create_remote_thread\"}");
 Check(FollowPolicy(123,987,known),"only exact typed process birth and pre-loader failure authorize a continuation");
 for(const char* bad:{"{\"ok\":false,\"pid\":123,\"process_created\":\"987\",\"injected\":null,\"loader_thread_started\":null,\"failure_stage\":\"create_remote_thread\"}","{\"ok\":false,\"pid\":123,\"process_created\":\"987\",\"injected\":false,\"loader_thread_started\":true,\"failure_stage\":\"create_remote_thread\"}","{\"ok\":false,\"pid\":123,\"process_created\":987,\"injected\":false,\"loader_thread_started\":false,\"failure_stage\":\"create_remote_thread\"}","{\"ok\":false,\"pid\":123,\"process_created\":\"987\",\"injected\":false,\"loader_thread_started\":false,\"failure_stage\":\"load_library_wait\"}"})Check(!FollowPolicy(123,987,bad),"unknown/started loader, wrong type and later failure stages cannot follow");
 Check(!FollowPolicy(124,987,known)&&!FollowPolicy(123,988,known)&&!FollowPolicy(123,0,known),"PID mismatch, creation mismatch and unavailable identity refuse continuation");
 for(const wchar_t* mode:{L"same",L"ambiguous",L"sibling"}){
  const auto receipt=ExeDir()+L"\\"+mode+L".pid",json=receipt+L".json";
  if(!wcscmp(mode,L"sibling")){actorReceipt=receipt;Check(SpawnChild(L".outsider"),"unrelated same-image sibling control starts outside the launched root");}
  Args args;args.opt={{"exe",W2U(own)},{"dll",W2U(own)},{"args",W2U(L"--stub "+std::wstring(mode)+L" \""+receipt+L"\"")},{"timeout","7"},{"settle","0"},{"fg-guard","0"}};
  FILE* output=_wfsopen(json.c_str(),L"w",_SH_DENYNO);if(!output)return 111;fflush(stdout);const int saved=_dup(_fileno(stdout));if(saved<0||_dup2(_fileno(output),_fileno(stdout)))return 111;
  loaderAttempts=0;const auto began=GetTickCount64();const int result=Launch(args);fflush(stdout);_dup2(saved,_fileno(stdout));_close(saved);fclose(output);
  const auto raw=Read(json);vi_ready_json::Reader parsed{raw,0,{},128};const bool valid=parsed.Parse();const DWORD child=Published(receipt+L".child");
  Check(valid,"actual Launch emits one complete typed JSON result");
  Check(loaderAttempts==1,"one failed loader-thread creation on original PID, never retried");
  const bool success=!wcscmp(mode,L"same");Check(success?(result==0&&child&&g_launchPid==child&&g_launchPid!=g_initialLaunchPid):(result!=0&&g_launchPid==g_initialLaunchPid),success?"delayed proven descendant is followed through normal child readiness":"two delayed sibling windows refuse arbitrary descendant selection");
  auto previous=parsed.fields.find("pre_follow_inject");bool preserved=false;
  if(valid&&previous!=parsed.fields.end()&&previous->second.type=='{'){vi_ready_json::Reader failure{previous->second.raw,0,{}};bool started=true;preserved=failure.Parse()&&failure.Bool("loader_thread_started",started)&&!started&&failure.fields.at("error").raw=="\"CreateRemoteThread failed 5\"";}
  Check(preserved,"original typed pre-loader failure receipt survives continuation or refusal");
  Check(GetTickCount64()-began<7500,"continuation respects the original launch budget");
  Check(AccountActors(),"original creator handles prove finite root/child exit with matching birth and zero exit code");
 }
 Check(vi_launch_terminal::Verify(),"terminal desktop writers remain guarded");
 printf("launch failure follow: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
