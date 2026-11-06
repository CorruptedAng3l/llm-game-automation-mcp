// Actual Launch, controlled actor copies, original creator handles and terminal
// desktop-writer guards. Existing instances are never attached or injected.
#include <windows.h>
static BOOL WINAPI TrackedCreate(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
static HANDLE WINAPI TrackedOpen(DWORD,BOOL,DWORD);
static HANDLE WINAPI RefuseLoader(HANDLE,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD);
static HANDLE WINAPI TrackedFile(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static BOOL WINAPI TrackedQueryImage(HANDLE,DWORD,LPWSTR,PDWORD);
static DWORD WINAPI TrackedWait(HANDLE,DWORD);
#define CreateProcessW TrackedCreate
#define OpenProcess TrackedOpen
#define CreateRemoteThread RefuseLoader
#define CreateFileW TrackedFile
#define QueryFullProcessImageNameW TrackedQueryImage
#define WaitForSingleObject TrackedWait
#define wmain ViDuplicateCliEntry
#include <vinput_cli.cpp>
#undef wmain
#undef CreateProcessW
#undef OpenProcess
#undef CreateRemoteThread
#undef CreateFileW
#undef QueryFullProcessImageNameW
#undef WaitForSingleObject
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include <io.h>
#include <share.h>

static unsigned passed=0,failed=0,actorCreates=0,loaderCalls=0,pipeCalls=0;
static DWORD deniedPid=0;
enum class FaultMode {None,Identity,Canonical};
struct CandidateFault {FaultMode mode=FaultMode::None;DWORD pid=0;HANDLE trigger=nullptr;unsigned reads=0,positiveWaits=0;DWORD maximumWait=0;};
static CandidateFault candidateFault;
static std::wstring selfImage,actorA,actorB,missingFile,missingDirectoryFile;
struct Owned {HANDLE handle=nullptr;DWORD pid=0;unsigned long long born=0;std::wstring image;};
static std::vector<Owned> owned;
static std::vector<HANDLE> stops;
static void Check(bool ok,const char* what){printf("%s %s\n",ok?"PASS":"FAIL",what);ok?++passed:++failed;}
static bool Identity(HANDLE handle,DWORD pid,const std::wstring& image,unsigned long long& born){
 FILETIME created{},ended{},kernel{},user{};wchar_t actual[32768]{};DWORD size=32768;
 if(GetProcessId(handle)!=pid||!GetProcessTimes(handle,&created,&ended,&kernel,&user)||!QueryFullProcessImageNameW(handle,0,actual,&size)||!size||size>=32768||_wcsicmp(actual,image.c_str()))return false;
 born=LaunchStamp(created);return born!=0;
}
static BOOL WINAPI TrackedCreate(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES ps,LPSECURITY_ATTRIBUTES ts,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION result){
 if(!app||(_wcsicmp(app,selfImage.c_str())&&_wcsicmp(app,actorA.c_str())&&_wcsicmp(app,actorB.c_str())&&_wcsicmp(app,missingFile.c_str())&&_wcsicmp(app,missingDirectoryFile.c_str()))){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 if(!CreateProcessW(app,cmd,ps,ts,inherit,flags|CREATE_NO_WINDOW,env,cwd,startup,result))return FALSE;
 HANDLE retained=nullptr;unsigned long long born=0;
 if(!Identity(result->hProcess,result->dwProcessId,app,born)||!DuplicateHandle(GetCurrentProcess(),result->hProcess,GetCurrentProcess(),&retained,0,FALSE,DUPLICATE_SAME_ACCESS)){
  TerminateProcess(result->hProcess,111);WaitForSingleObject(result->hProcess,1500);CloseHandle(result->hThread);CloseHandle(result->hProcess);*result={};SetLastError(ERROR_INVALID_HANDLE);return FALSE;
 }
 owned.push_back({retained,result->dwProcessId,born,app});if(_wcsicmp(app,selfImage.c_str()))++actorCreates;return TRUE;
}
static HANDLE WINAPI TrackedOpen(DWORD rights,BOOL inherit,DWORD pid){if(pid&&pid==deniedPid){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return OpenProcess(rights,inherit,pid);}
static HANDLE WINAPI RefuseLoader(HANDLE,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD){++loaderCalls;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
static HANDLE WINAPI TrackedFile(LPCWSTR name,DWORD rights,DWORD share,LPSECURITY_ATTRIBUTES attrs,DWORD disposition,DWORD flags,HANDLE templateFile){
 if(name&&!_wcsnicmp(name,L"\\\\.\\pipe\\",9)){++pipeCalls;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 if(candidateFault.mode==FaultMode::Canonical&&name&&(share&FILE_SHARE_DELETE)&&!_wcsicmp(name,actorA.c_str())){++candidateFault.reads;if(candidateFault.trigger)SetEvent(candidateFault.trigger);SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 return CreateFileW(name,rights,share,attrs,disposition,flags,templateFile);
}
static BOOL WINAPI TrackedQueryImage(HANDLE process,DWORD flags,LPWSTR path,PDWORD size){
 if(candidateFault.mode==FaultMode::Identity&&GetProcessId(process)==candidateFault.pid){++candidateFault.reads;if(candidateFault.trigger)SetEvent(candidateFault.trigger);SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 return QueryFullProcessImageNameW(process,flags,path,size);
}
static DWORD WINAPI TrackedWait(HANDLE handle,DWORD budget){
 const auto error=GetLastError();const auto pid=GetProcessId(handle);SetLastError(error);
 if(candidateFault.mode!=FaultMode::None&&pid==candidateFault.pid&&budget){++candidateFault.positiveWaits;candidateFault.maximumWait=(std::max)(candidateFault.maximumWait,budget);}
 return WaitForSingleObject(handle,budget);
}
static std::string Read(const std::wstring& path){FILE* in=_wfsopen(path.c_str(),L"rb",_SH_DENYNO);if(!in)return {};char text[65537]{};const auto size=fread(text,1,65536,in);const bool complete=feof(in)&&!ferror(in);fclose(in);return complete?std::string(text,size):std::string{};}
static bool Write(const std::wstring& path,const std::string& text){FILE* out=nullptr;_wfopen_s(&out,path.c_str(),L"wb");if(!out)return false;const bool good=fwrite(text.data(),1,text.size(),out)==text.size()&&!ferror(out);return fclose(out)==0&&good;}
static std::string Field(const std::string& text,const char* key){vi_ready_json::Reader reader{text,0,{},128};if(!reader.Parse())return {};const auto at=reader.fields.find(key);return at==reader.fields.end()?std::string{}:at->second.raw;}
static DWORD Pid(const std::string& text){const auto value=Field(text,"pid");char* end=nullptr;const auto n=strtoull(value.c_str(),&end,10);return n&&end&&!*end&&n<=MAXDWORD?(DWORD)n:0;}
static std::string WaitRead(const std::wstring& path,DWORD budget=7000){const auto until=GetTickCount64()+budget;std::string result;do{result=Read(path);if(!result.empty())return result;Sleep(5);}while(GetTickCount64()<until);return {};}
static HANDLE Stop(const std::wstring& label,std::wstring& name){
 name=L"Local\\vi.launch.duplicate."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L"."+label;
 SetLastError(0);HANDLE event=CreateEventW(nullptr,TRUE,FALSE,name.c_str());if(!event||GetLastError()==ERROR_ALREADY_EXISTS){if(event)CloseHandle(event);return nullptr;}stops.push_back(event);return event;
}
static bool Finish(){
 for(auto event:stops)SetEvent(event);bool good=true;
 for(auto& process:owned){FILETIME created{},ended{},kernel{},user{};DWORD code=999;const bool exited=WaitForSingleObject(process.handle,10000)==WAIT_OBJECT_0;
  // Image was verified on the original creator handle before publication.
  // QueryFullProcessImageName can fail after exit; the same retained kernel
  // object's PID + creation + exit code establish completion without reopening.
  const bool known=GetProcessId(process.handle)==process.pid&&GetProcessTimes(process.handle,&created,&ended,&kernel,&user)&&LaunchStamp(created)==process.born&&GetExitCodeProcess(process.handle,&code);
  fprintf(stderr,"[duplicate fixture accounting] pid=%lu birth=%llu exited=%s exact_original=%s code=%lu cached_image=true\n",process.pid,process.born,exited?"true":"false",known?"true":"false",code);
  if(!exited){TerminateProcess(process.handle,111);WaitForSingleObject(process.handle,1500);}good=good&&exited&&known&&code==0;CloseHandle(process.handle);
 }
 owned.clear();for(auto event:stops)CloseHandle(event);stops.clear();return good;
}
static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
static int Actor(int argc,wchar_t** argv){
 if(argc!=5||!vi_launch_ack::ArmNoActivation())return 111;
 HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!stop)return 111;
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"ViDuplicateOwned";if(!RegisterClassW(&cls)){CloseHandle(stop);return 111;}
 const bool visible=wcscmp(argv[4],L"hidden")!=0;
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Controlled exact-image actor",WS_OVERLAPPEDWINDOW|(visible?WS_VISIBLE:0),-28000,600,400,250,nullptr,nullptr,cls.hInstance,nullptr);
 unsigned long long born=0;wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
 const bool ready=window&&Identity(GetCurrentProcess(),GetCurrentProcessId(),image,born)&&Write(argv[3],"{\"pid\":"+std::to_string(GetCurrentProcessId())+",\"process_created\":\""+std::to_string(born)+"\"}");
 const auto until=GetTickCount64()+30000;bool stopped=false;
 while(ready&&GetTickCount64()<until){if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0){stopped=true;break;}MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(2);}
 if(window)DestroyWindow(window);CloseHandle(stop);return ready&&stopped?0:111;
}
static std::wstring ActorArgs(const std::wstring& stop,const std::wstring& receipt,bool hidden=false){return L"--actor \""+stop+L"\" \""+receipt+L"\" "+(hidden?L"hidden":L"visible");}
static bool Spawn(const std::wstring& image,const std::wstring& arguments){auto command=L"\""+image+L"\" "+arguments;STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNOACTIVATE;PROCESS_INFORMATION process{};if(!TrackedCreate(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;}
static Args Options(const std::wstring& image,const std::wstring& stop,const std::wstring& receipt){Args args;args.opt={{"exe",W2U(image)},{"args",W2U(ActorArgs(stop,receipt))},{"no-inject","true"},{"timeout","4"},{"settle","0"},{"fg-guard","0"}};return args;}
struct DuplicateReply {int code=111;std::string json;};
static DuplicateReply Call(const Args& args,const std::wstring& receipt){
 FILE* out=_wfsopen(receipt.c_str(),L"wb",_SH_DENYNO);if(!out)return {};fflush(stdout);const int saved=_dup(_fileno(stdout));if(saved<0||_dup2(_fileno(out),_fileno(stdout))){if(saved>=0)_close(saved);fclose(out);return {};}
 const int code=Launch(args);fflush(stdout);const bool restored=_dup2(saved,_fileno(stdout))==0;_close(saved);fclose(out);return {restored?code:111,Read(receipt)};
}
static bool Refused(const DuplicateReply& reply){return reply.code==2&&Field(reply.json,"ok")=="false"&&Field(reply.json,"launched")=="false"&&Field(reply.json,"injected")=="false"&&Field(reply.json,"pid").empty()&&Field(reply.json,"automatic_attach_attempted")=="false";}
static bool Contains(const std::string& list,DWORD pid,unsigned long long born){return list.find("\"pid\":"+std::to_string(pid)+",")!=std::string::npos&&list.find("\"process_created\":\""+std::to_string(born)+"\"")!=std::string::npos;}
struct StopAfterRead {HANDLE trigger=nullptr,stop=nullptr;};
static DWORD WINAPI StopOnFailedRead(void* pointer){const auto value=*(StopAfterRead*)pointer;delete (StopAfterRead*)pointer;if(WaitForSingleObject(value.trigger,4000)!=WAIT_OBJECT_0)return 111;Sleep(120);return SetEvent(value.stop)?0:111;}
static bool CandidateExitControls(){
 for(const auto mode:{FaultMode::Identity,FaultMode::Canonical})for(const bool dying:{false,true}){
  const auto label=std::wstring(mode==FaultMode::Identity?L"identity":L"canonical")+(dying?L"_dying":L"_live");const auto stem=ExeDir()+L"\\"+label;
  std::wstring oldStopName,newStopName;HANDLE oldStop=Stop(label+L"_old",oldStopName),newStop=Stop(label+L"_new",newStopName);if(!oldStop||!newStop)return false;
  Check(Spawn(actorA,ActorArgs(oldStopName,stem+L".actor",true))&&!WaitRead(stem+L".actor").empty(),"owned unresolved-candidate control begins with a verified live actor");if(owned.empty())return false;const auto actor=owned.back();
  HANDLE trigger=nullptr,thread=nullptr;if(dying){trigger=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!trigger)return false;thread=CreateThread(nullptr,0,StopOnFailedRead,new StopAfterRead{trigger,oldStop},0,nullptr);if(!thread){CloseHandle(trigger);return false;}}
  candidateFault={mode,actor.pid,trigger};const auto before=actorCreates;const auto started=GetTickCount64();const auto result=Call(Options(actorA,newStopName,stem+L".replacement"),stem+L".json");const auto elapsed=GetTickCount64()-started;const auto observation=candidateFault;candidateFault={};
  if(thread){const bool joined=WaitForSingleObject(thread,5000)==WAIT_OBJECT_0;Check(joined,"bounded owned stop worker finishes before its private handles are closed");if(!joined)return false;CloseHandle(thread);CloseHandle(trigger);}
  if(dying){
   Check(result.code==0&&Pid(result.json)&&Pid(result.json)!=actor.pid&&actorCreates==before+1&&WaitForSingleObject(actor.handle,0)==WAIT_OBJECT_0,"only original-handle signaled exit permits a relaunch after unresolved identity/path");
   Check(observation.reads&&observation.positiveWaits&&observation.maximumWait<=1500&&elapsed>=80&&elapsed<3500,"terminating candidate uses a finite retained-handle wait before absence evidence");
  }else{
   Check(Refused(result)&&WaitForSingleObject(actor.handle,0)==WAIT_TIMEOUT&&actorCreates==before&&Field(result.json,"duplicate_check").find(mode==FaultMode::Identity?"candidate_identity_unresolved":"candidate_image_path_unresolved")!=std::string::npos,"genuinely live unresolved candidate still refuses without creating a process");
   Check(observation.reads&&observation.positiveWaits&&observation.maximumWait<=1500&&elapsed>=1200&&elapsed<3500,"live candidate waits at most1500ms then retains the incomplete-inventory refusal");
  }
  Check(Finish(),"all unresolved-candidate actors finish through original handles with exact exit accounting");
 }
 // A short common deadline must cap the newly added wait, not gain1500ms.
 std::wstring stopName;HANDLE stop=Stop(L"deadline",stopName);if(!stop)return false;const auto stem=ExeDir()+L"\\deadline";
 Check(Spawn(actorA,ActorArgs(stopName,stem+L".actor",true))&&!WaitRead(stem+L".actor").empty(),"deadline control starts one verified owned live candidate");if(owned.empty())return false;const auto actor=owned.back();candidateFault={FaultMode::Identity,actor.pid};
 const auto before=actorCreates;const auto started=GetTickCount64();bool accepted=false;std::string reason;{CliDeadlineScope budget(80);LaunchInstanceGuard guard;auto path=actorA;accepted=guard.Prepare(path,false);reason=guard.reason;}const auto elapsed=GetTickCount64()-started;const auto observation=candidateFault;candidateFault={};
 Check(!accepted&&reason=="candidate_identity_unresolved"&&observation.positiveWaits&&observation.maximumWait<=80&&elapsed<500&&WaitForSingleObject(actor.handle,0)==WAIT_TIMEOUT&&actorCreates==before,"common80ms CLI budget caps unresolved-candidate wait without skipping a live process");
 return Finish();
}
static void MissingImageControls(){
 for(const auto& item:std::vector<std::pair<std::wstring,DWORD>>{{missingFile,ERROR_FILE_NOT_FOUND},{missingDirectoryFile,ERROR_PATH_NOT_FOUND}}){auto options=Options(item.first,L"unused",L"unused");options.opt.push_back({"cwd",W2U(ExeDir())});const auto before=actorCreates;const auto result=Call(options,ExeDir()+L"\\missing_"+std::to_wstring(item.second)+L".json");
  Check(result.code==2&&Field(result.json,"win32_error")==std::to_string(item.second)&&Field(result.json,"error").find("CreateProcess failed")!=std::string::npos&&Field(result.json,"duplicate_check").find("requested_image_missing")!=std::string::npos&&Field(result.json,"duplicate_check").find(JEsc(W2U(item.first)))!=std::string::npos&&actorCreates==before,"missing file/directory preserves literal requested path and native error2/3 at process creation");
 }
}
static int Peer(int argc,wchar_t** argv){
 if(argc!=7)return 111;HANDLE start=OpenEventW(SYNCHRONIZE,FALSE,argv[2]),stop=OpenEventW(SYNCHRONIZE,FALSE,argv[3]);if(!start||!stop){if(start)CloseHandle(start);if(stop)CloseHandle(stop);return 111;}
 if(WaitForSingleObject(start,7000)!=WAIT_OBJECT_0){CloseHandle(start);CloseHandle(stop);return 111;}
 const auto result=Call(Options(argv[4],argv[3],std::wstring(argv[5])+L".actor"),argv[5]);
 const bool ready=Write(argv[6],"{\"code\":"+std::to_string(result.code)+",\"actor_creates\":"+std::to_string(actorCreates)+",\"loader_calls\":"+std::to_string(loaderCalls)+",\"pipe_calls\":"+std::to_string(pipeCalls)+",\"terminal_verified\":"+(vi_launch_terminal::Verify()?"true":"false")+",\"result\":"+result.json+"}");
 const bool stopped=WaitForSingleObject(stop,15000)==WAIT_OBJECT_0;CloseHandle(start);CloseHandle(stop);return ready&&stopped&&Finish()?0:111;
}
int wmain(int argc,wchar_t** argv){
 if(!vi_launch_ack::ArmDeadline()||!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
 wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);selfImage=own;
 actorA=ExeDir()+L"\\actor_a\\vi_launchduplicatetest.exe";actorB=ExeDir()+L"\\actor_b\\vi_launchduplicatetest.exe";
 missingFile=ExeDir()+L"\\missing target with spaces.exe";missingDirectoryFile=ExeDir()+L"\\missing parent with spaces\\missing target.exe";
 if(argc>1&&!wcscmp(argv[1],L"--actor"))return Actor(argc,argv);
 if(argc>1&&!wcscmp(argv[1],L"--launch-peer"))return Peer(argc,argv);
 if(argc!=2||wcscmp(argv[1],L"--await-start"))return 111;char ack[8]{};if(!fgets(ack,sizeof(ack),stdin)||strcmp(ack,"RUN\n"))return 111;
 if(!CreateDirectoryW((ExeDir()+L"\\actor_a").c_str(),nullptr)||!CreateDirectoryW((ExeDir()+L"\\actor_b").c_str(),nullptr)||!CopyFileW(own,actorA.c_str(),TRUE)||!CopyFileW(own,actorB.c_str(),TRUE))return 111;
 std::wstring stopName;HANDLE stop=Stop(L"ordinary",stopName);if(!stop)return 111;
 const auto base=ExeDir()+L"\\ordinary";auto options=Options(actorA,stopName,base+L".first");
 const auto first=Call(options,base+L".first.json");Check(first.code==0&&Pid(first.json)&&actorCreates==1,"first default direct launch ignores a different full-path controller with the same basename");
 const Owned firstActor=owned.empty()?Owned{}:owned.front();const auto creates=actorCreates,loaders=loaderCalls,pipes=pipeCalls;const auto cursor=vi_cursor_spy::Total(),foreground=vi_cursor_spy::foregroundEffects.load();
 options=Options(actorA,stopName,base+L".duplicate");const auto duplicate=Call(options,base+L".duplicate.json");
 Check(Refused(duplicate)&&Field(duplicate.json,"already_running")=="true","second default launch refuses without top-level target PID or adoption");
 Check(Contains(Field(duplicate.json,"existing_instances"),firstActor.pid,firstActor.born),"refusal reports exact PID and creation from the existing retained actor identity");
 Check(actorCreates==creates&&loaderCalls==loaders&&pipeCalls==pipes&&vi_cursor_spy::Total()==cursor&&vi_cursor_spy::foregroundEffects.load()==foreground,"duplicate refusal creates no process, loader, target-pipe request or desktop write");
 options.opt.push_back({"new-instance","false"});const auto explicitFalse=Call(options,base+L".false.json");Check(Refused(explicitFalse)&&actorCreates==creates,"explicit false preserves duplicate refusal");
 options=Options(actorA,stopName,base+L".second");options.opt.push_back({"new-instance","true"});const auto second=Call(options,base+L".second.json");
 Check(second.code==0&&Pid(second.json)&&Pid(second.json)!=firstActor.pid&&actorCreates==creates+1,"explicit true creates a distinct controlled second instance");
 const auto secondActor=owned.size()>1?owned[1]:Owned{};
 const auto ambiguous=Call(Options(actorA,stopName,base+L".ambiguous"),base+L".ambiguous.json");const auto list=Field(ambiguous.json,"existing_instances");
 Check(Refused(ambiguous)&&Contains(list,firstActor.pid,firstActor.born)&&Contains(list,secondActor.pid,secondActor.born)&&actorCreates==creates+1,"multiple existing exact images are all reported and none is selected");
 const auto different=Call(Options(actorB,stopName,base+L".different"),base+L".different.json");Check(different.code==0&&Pid(different.json)&&actorCreates==creates+2,"same basename in another canonical directory remains a permitted direct launch");
 deniedPid=firstActor.pid;const auto beforeUnknown=actorCreates;const auto unknown=Call(Options(actorA,stopName,base+L".unknown"),base+L".unknown.json");deniedPid=0;
 const auto unknownAlready=Field(unknown.json,"already_running");const bool truthfulUnknown=unknownAlready=="null"||(unknownAlready=="true"&&Contains(Field(unknown.json,"existing_instances"),secondActor.pid,secondActor.born));
 Check(Refused(unknown)&&truthfulUnknown&&Field(unknown.json,"duplicate_check").find("\"inventory_complete\":false")!=std::string::npos&&actorCreates==beforeUnknown,"unqueryable matching candidate refuses incomplete inventory without asserting absence before creation");
 for(const auto& flags:std::vector<std::vector<std::pair<std::string,std::string>>>{{{"new-instance","yes"}},{{"new-instance","true"},{"new-instance","false"}}}){
  auto invalid=Options(actorA,stopName,base+L".invalid");for(const auto& flag:flags)invalid.opt.push_back(flag);const auto before=actorCreates;const auto result=Call(invalid,base+L".invalid.json");Check(result.code==2&&Field(result.json,"launched")=="false"&&actorCreates==before,"malformed or duplicate native new-instance flags fail before startup");
 }
 Check(Finish(),"all ordinary actors exit by own stop event and original handles verify birth and zero exit");
 stop=Stop(L"hidden",stopName);if(!stop)return 111;const auto hiddenPath=ExeDir()+L"\\hidden";
 Check(Spawn(actorA,ActorArgs(stopName,hiddenPath+L".actor",true))&&!WaitRead(hiddenPath+L".actor").empty(),"controlled hidden-window actor publishes after native window creation");
 const auto beforeHidden=actorCreates;const auto hidden=Call(Options(actorA,stopName,hiddenPath+L".extra"),hiddenPath+L".json");Check(Refused(hidden)&&actorCreates==beforeHidden,"hidden existing exact-image instance blocks without window selection");Check(Finish(),"hidden actor original-handle cleanup accounts exact exit");
 if(!CandidateExitControls())return 111;MissingImageControls();
 // Both peer CLIs execute the real Launch entry in separate address spaces.
 // Their common actor stop event keeps the winner live through the loser's census.
 std::wstring raceStopName,startName;HANDLE raceStop=Stop(L"race_stop",raceStopName),start=Stop(L"race_start",startName);if(!raceStop||!start)return 111;
 const auto race=ExeDir()+L"\\race";
 for(unsigned i=0;i<2;++i){const auto stem=race+std::to_wstring(i);const auto args=L"--launch-peer \""+startName+L"\" \""+raceStopName+L"\" \""+actorA+L"\" \""+stem+L".json\" \""+stem+L".done\"";Check(Spawn(selfImage,args),"controlled concurrent CLI starts with an original retained handle");}
 SetEvent(start);const auto race0=WaitRead(race+L"0.done"),race1=WaitRead(race+L"1.done");
 const auto result0=Field(race0,"result"),result1=Field(race1,"result");
 const bool won0=Field(result0,"ok")=="true",won1=Field(result1,"ok")=="true";
 Check(won0!=won1&&((won0&&Field(result1,"already_running")=="true")||(won1&&Field(result0,"already_running")=="true")),"concurrent default Launch pair creates one winner and one duplicate refusal");
 Check((Field(race0,"actor_creates")=="1"&&Field(race1,"actor_creates")=="0")||(Field(race0,"actor_creates")=="0"&&Field(race1,"actor_creates")=="1"),"original-creator peer journals prove exactly one actual actor CreateProcess across the concurrent pair");
 Check(Field(race0,"loader_calls")=="0"&&Field(race1,"loader_calls")=="0"&&Field(race0,"pipe_calls")=="0"&&Field(race1,"pipe_calls")=="0"&&Field(race0,"terminal_verified")=="true"&&Field(race1,"terminal_verified")=="true","both concurrent CLI peers retain terminal guards and send no loader or target-pipe request");
 Check(Field(won0?result1:result0,"pid").empty(),"concurrent loser cannot become an adopted top-level target");
 Check(Finish(),"concurrent peers account their actor original handles and controller accounts peer original handles");
 Check(loaderCalls==0&&pipeCalls==0&&vi_launch_terminal::Verify(),"all controlled cases retain terminal guards and send no loader or target-pipe request");
 printf("launch duplicate guard: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
