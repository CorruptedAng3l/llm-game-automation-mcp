// The only server/client is this console process. No target process, injection,
// GUI, graphics, input or desktop writer is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include "../src/vi_cli_deadline.h"
static ULONGLONG outerDeadline=0;static std::wstring privateName;static unsigned stamps=0;
static DWORD CliBudget(DWORD cap){return vi_cli_deadline::Remaining(outerDeadline,GetTickCount64(),cap);}
static std::wstring PipeName(DWORD){return privateName;}
static std::string StampOwner(const std::string& text){++stamps;return text;}
#include "../src/vi_cli_transport.inl"
struct Server {
 enum Mode{Healthy,StallRead,StallReply,Huge};Mode mode;HANDLE stop=nullptr,done=nullptr,pipe=INVALID_HANDLE_VALUE;std::thread worker;std::string received;std::atomic<bool> connected{false};
 explicit Server(Mode selected):mode(selected){static unsigned serial=0;privateName=L"\\\\.\\pipe\\vi_cli_budget_self_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(++serial);stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);done=CreateEventW(nullptr,TRUE,FALSE,nullptr);pipe=CreateNamedPipeW(privateName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,nullptr);
  if(!stop||!done||pipe==INVALID_HANDLE_VALUE)ExitProcess(111);worker=std::thread([this]{Run();});}
 void Run(){if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){connected=true;if(mode==StallRead)WaitForSingleObject(stop,5000);else{char byte=0;DWORD count=0;while(received.size()<1024*1024&&ReadFile(pipe,&byte,1,&count,nullptr)&&count){received+=byte;if(byte=='\n')break;}
   if(mode==StallReply)WaitForSingleObject(stop,5000);else{std::string reply=mode==Huge?std::string(1000,'x')+"\n":"{\"ok\":true}\n";DWORD written=0;WriteFile(pipe,reply.data(),(DWORD)reply.size(),&written,nullptr);}}
  }DisconnectNamedPipe(pipe);CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;SetEvent(done);}
 void Stop(){if(!worker.joinable())return;SetEvent(stop);const auto end=GetTickCount64()+2000;while(WaitForSingleObject(done,10)!=WAIT_OBJECT_0){CancelSynchronousIo((HANDLE)worker.native_handle());if(GetTickCount64()>=end)TerminateProcess(GetCurrentProcess(),111);}worker.join();}
 ~Server(){Stop();CloseHandle(stop);CloseHandle(done);}
};
int main(){unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};
 {Server server(Server::Healthy);std::string response;stamps=0;const bool ok=BoundedCliRequest(GetCurrentProcessId(),"{\"cmd\":\"ping\"}",1000,response);server.Stop();check(ok&&response=="{\"ok\":true}"&&server.received=="{\"cmd\":\"ping\"}\n"&&stamps==1,"actual owned pipe sends once and receives one complete bounded reply");}
 {Server server(Server::StallReply);std::string response;const auto start=GetTickCount64();const bool ok=BoundedCliRequest(GetCurrentProcessId(),"{}",80,response);const auto elapsed=GetTickCount64()-start;server.Stop();check(!ok&&elapsed<2000,"stalled actual pipe reply cannot block synchronous launch receive");}
 {Server server(Server::StallRead);std::string response;const auto start=GetTickCount64();const bool ok=BoundedCliRequest(GetCurrentProcessId(),std::string(131072,'x'),80,response);const auto elapsed=GetTickCount64()-start;server.Stop();check(!ok&&elapsed<2000,"backpressured actual pipe write has the same deadline");}
 {Server server(Server::StallRead);std::string response="stale";const bool ok=BoundedCliRequest(GetCurrentProcessId()^1,"{}",1000,response);server.Stop();check(!ok&&server.received.empty()&&response.empty(),"foreign server PID refuses before sending or retaining stale response");}
 {Server server(Server::Huge);std::string response;const bool ok=BoundedCliRequest(GetCurrentProcessId(),"{}",1000,response,nullptr,64);server.Stop();check(!ok,"actual oversized reply refuses within its declared byte cap");}
 {Server server(Server::StallReply);std::string response;outerDeadline=GetTickCount64()+80;const auto start=GetTickCount64();const bool ok=BoundedCliRequest(GetCurrentProcessId(),"{}",5000,response);const auto elapsed=GetTickCount64()-start;outerDeadline=0;server.Stop();check(!ok&&elapsed<2000,"shared launch deadline clips a longer transport stage");}
 {Server server(Server::StallRead);std::string response;const bool ok=BoundedCliRequest(GetCurrentProcessId(),"{}",0,response);server.Stop();check(!ok&&!server.connected,"zero budget starts no pipe connection");}
 printf("CLI_TRANSPORT_RESULT %u %u\n",pass,fail);return fail?1:0;}
