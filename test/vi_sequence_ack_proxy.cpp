// Test-only transport fault injector. Every forwarded request reaches the real
// native CLI once. Only an already received real sequence-observe ACK is hidden.
// Original child handles, creation/image and exit are retained/accounted; a
// private kill-on-close Job Object contains only that freshly created CLI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstdint>
#include "../src/vi_ready_json.h"
#pragma comment(lib,"shell32.lib")
static std::wstring Env(const wchar_t* name){wchar_t value[32768]{};const DWORD n=GetEnvironmentVariableW(name,value,32768);return n&&n<32768?std::wstring(value,n):std::wstring();}
static std::string Utf8(const std::wstring& text){if(text.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string out((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),(int)text.size(),out.data(),n,nullptr,nullptr);return out;}
static std::string Esc(const std::string& text){std::string out;for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c<32){char value[8];sprintf_s(value,"\\u%04x",(unsigned)c);out+=value;}else out+=(char)c;}return out;}
static std::wstring Quote(const std::wstring& text){std::wstring out=L"\"";size_t slashes=0;for(wchar_t c:text){if(c==L'\\'){slashes++;continue;}if(c==L'"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
static bool Text(const vi_ready_json::Reader& r,const char* key,std::string& value){auto p=r.fields.find(key);if(p==r.fields.end()||p->second.type!='"')return false;vi_ready_json::Reader token{p->second.raw,0,{}};return token.String(value)&&token.at==p->second.raw.size();}
// Terminal JobReply has more than64 fields. Validate that nested object using
// the reader's explicit128-field top-level budget, without relaxing requests.
static bool ReadAck(const std::string& text){
 if(text.empty()||text.size()>65536)return false;vi_ready_json::Reader r{text,0,{}};r.Space();if(r.at>=text.size()||text[r.at++]!='{')return false;
 std::vector<std::string> names;bool ok=false,ack=false,hasOk=false,hasAck=false;r.Space();
 for(;;){std::string key;if(names.size()>=16||!r.String(key))return false;for(const auto& name:names)if(name==key)return false;names.push_back(key);r.Space();if(r.at>=text.size()||text[r.at++]!=':')return false;r.Space();const auto start=r.at;char kind=0;
  if(r.at<text.size()&&text[r.at]=='{'){const std::string nested=text.substr(r.at);vi_ready_json::Reader object{nested,0,{}};object.maxTopLevelFields=128;if(!object.Object(0,true))return false;r.at+=object.at;kind='{';}
  else if(!r.Any(0,kind))return false;
  if(key=="ok"||key=="guard_ack"){if(kind!='b')return false;const bool value=text.compare(start,r.at-start,"true")==0;if(key=="ok"){ok=value;hasOk=true;}else{ack=value;hasAck=true;}}
  r.Space();if(r.at>=text.size())return false;if(text[r.at]=='}'){++r.at;r.Space();return r.at==text.size()&&hasOk&&hasAck&&ok&&ack;}if(text[r.at++]!=',')return false;r.Space();
 }
}
struct PipeText{std::string text;bool complete=false,limited=false;};
static void Drain(HANDLE pipe,PipeText& out){char bytes[2048];DWORD n=0;while(ReadFile(pipe,bytes,sizeof(bytes),&n,nullptr)&&n){if(out.text.size()+n<=65536)out.text.append(bytes,n);else out.limited=true;}out.complete=true;CloseHandle(pipe);}
static int Error(const char* text){printf("{\"ok\":false,\"error\":\"%s\"}\n",text);return 1;}
static bool Journal(const std::wstring& path,const std::string& text,bool append){HANDLE file=CreateFileW(path.c_str(),append?FILE_APPEND_DATA:GENERIC_WRITE,FILE_SHARE_READ,nullptr,append?OPEN_EXISTING:CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD written=0;const bool ok=file!=INVALID_HANDLE_VALUE&&WriteFile(file,text.data(),(DWORD)text.size(),&written,nullptr)&&written==text.size();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);return ok;}
int wmain(int argc,wchar_t** argv){
 const auto cli=Env(L"VINPUT_SEQUENCE_NATIVE_CLI"),folder=Env(L"VINPUT_SEQUENCE_PROXY_DIR"),control=Env(L"VINPUT_SEQUENCE_PROXY_CONTROL");
 if(cli.empty()||folder.empty()||control.empty())return Error("test proxy configuration missing");
 std::ifstream config{std::filesystem::path(control)};std::string mode,token;config>>mode>>token;config.close();
 if(token.empty()||token.size()>32)return Error("test proxy control token invalid");for(unsigned char c:token)if(!(c>='a'&&c<='z')&&!(c>='A'&&c<='Z')&&!(c>='0'&&c<='9')&&c!='_'&&c!='-')return Error("test proxy control token invalid");
 std::string command,request;bool abort=false,observer=false,readStatus=false,waitExit=false;
 for(int i=1;i<argc;i++){if(!wcscmp(argv[i],L"job-status"))readStatus=true;if(!wcscmp(argv[i],L"waitexit"))waitExit=true;if(!wcscmp(argv[i],L"json")&&i+1<argc){request=Utf8(argv[i+1]);vi_ready_json::Reader parsed{request,0,{}};observer=parsed.Parse()&&Text(parsed,"cmd",command)&&command=="sequence-observe"&&parsed.Bool("abort",abort);}}
 const auto path=folder+L"\\proxy_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())+L".jsonl";
 const auto identityText="\"proxy_pid\":"+std::to_string(GetCurrentProcessId())+",\"case_token\":\""+Esc(token)+"\"";
 if(!Journal(path,"{\"event\":\"proxy_start\","+identityText+"}\n",false))return Error("test proxy initial journal unavailable; no native CLI was started");
 const auto marker=folder+L"\\loss_"+std::wstring(token.begin(),token.end())+L".flag";
 const bool lostRead=mode=="regular_read_loss"&&readStatus&&GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES;
 PipeText output,error;DWORD exitCode=1;bool started=false,identity=false,exited=false,forced=false;DWORD childPid=0;std::uint64_t created=0;HANDLE job=nullptr;
 if(!lostRead){
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE outRead=nullptr,outWrite=nullptr,errRead=nullptr,errWrite=nullptr;
  if(!CreatePipe(&outRead,&outWrite,&sa,0)||!CreatePipe(&errRead,&errWrite,&sa,0))return Error("test proxy output pipe creation failed");
  SetHandleInformation(outRead,HANDLE_FLAG_INHERIT,0);SetHandleInformation(errRead,HANDLE_FLAG_INHERIT,0);
  HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
  job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit)))return Error("test proxy private CLI job unavailable");
  SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);auto list=(LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();
  HANDLE inherited[]={input,outWrite,errWrite};if(input==INVALID_HANDLE_VALUE||!InitializeProcThreadAttributeList(list,1,0,&bytes)||!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))return Error("test proxy private handle list unavailable");
  STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=input;si.StartupInfo.hStdOutput=outWrite;si.StartupInfo.hStdError=errWrite;si.lpAttributeList=list;
  std::wstring line=Quote(cli);for(int i=1;i<argc;i++)line+=L" "+Quote(argv[i]);PROCESS_INFORMATION pi{};
  started=CreateProcessW(cli.c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi)!=FALSE;
  DeleteProcThreadAttributeList(list);CloseHandle(input);CloseHandle(outWrite);CloseHandle(errWrite);
  if(!started)return Error("test proxy real CLI creation failed");childPid=pi.dwProcessId;
  FILETIME creation{},finish{},kernel{},user{};wchar_t actual[32768]{};DWORD length=32768;
  identity=GetProcessId(pi.hProcess)==pi.dwProcessId&&GetProcessTimes(pi.hProcess,&creation,&finish,&kernel,&user)&&QueryFullProcessImageNameW(pi.hProcess,0,actual,&length)&&!_wcsicmp(actual,cli.c_str());created=((std::uint64_t)creation.dwHighDateTime<<32)|creation.dwLowDateTime;
  const bool noted=Journal(path,"{\"event\":\"child_started\","+identityText+",\"child_pid\":"+std::to_string(childPid)+",\"child_created\":\""+std::to_string(created)+"\",\"original_handle_identity\":"+(identity?"true":"false")+"}\n",true);
  const bool assigned=identity&&created&&noted&&AssignProcessToJobObject(job,pi.hProcess)!=FALSE;
  std::thread stdoutReader(Drain,outRead,std::ref(output)),stderrReader(Drain,errRead,std::ref(error));
  if(!assigned||ResumeThread(pi.hThread)==(DWORD)-1){TerminateProcess(pi.hProcess,125);forced=true;}
  if(WaitForSingleObject(pi.hProcess,waitExit?45000:5000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,126);forced=true;}
  exited=WaitForSingleObject(pi.hProcess,1000)==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,&exitCode)!=FALSE&&exitCode!=STILL_ACTIVE;
  stdoutReader.join();stderrReader.join();CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);job=nullptr;
 }else{output.complete=error.complete=true;output.text="{\"ok\":false,\"error\":\"pipe i/o failed: test read-only status response withheld\"}";}
 bool withheld=false;
 if(observer&&started&&identity&&exited&&!forced&&exitCode==0&&!output.limited&&ReadAck(output.text)&&((mode=="regular"&&!abort)||(mode=="regular_read_loss"&&!abort)||(mode=="abort"&&abort))){HANDLE mark=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(mark!=INVALID_HANDLE_VALUE){withheld=true;CloseHandle(mark);}}
 const std::string trace="{\"event\":\"proxy_complete\","+identityText+",\"mode\":\""+Esc(mode)+"\",\"request\":\""+Esc(request)+"\",\"read_status\":"+(readStatus?"true":"false")+",\"waitexit\":"+(waitExit?"true":"false")+",\"child_started\":"+(started?"true":"false")+",\"child_pid\":"+std::to_string(childPid)+",\"child_created\":\""+std::to_string(created)+"\",\"original_handle_identity\":"+(identity?"true":"false")+",\"original_handle_exit_observed\":"+(exited?"true":"false")+",\"forced\":"+(forced?"true":"false")+",\"exit_code\":"+std::to_string(exitCode)+",\"stdout_complete\":"+(output.complete?"true":"false")+",\"stdout_limited\":"+(output.limited?"true":"false")+",\"native_reply\":\""+Esc(output.text)+"\",\"loss_injected\":"+(withheld?"true":"false")+",\"read_loss_injected\":"+(lostRead?"true":"false")+"}\n";
 const bool logged=Journal(path,trace,true);
 if(!logged)return Error("test proxy retained child evidence unavailable");if(started&&(!identity||!exited||forced||output.limited||error.limited))return Error("test proxy real child accounting incomplete");
 if(withheld)return Error("test transport ACK intentionally withheld");if(!error.text.empty())fwrite(error.text.data(),1,error.text.size(),stderr);fwrite(output.text.data(),1,output.text.size(),stdout);return (int)exitCode;
}
