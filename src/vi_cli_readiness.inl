// Unlike legacy Request, readiness includes a bounded receive deadline. A hung
// pipe peer cannot make the injector claim success or wait indefinitely.
#include "vi_ready_json.h"
static bool InjectHooksOnly(const Args& args,bool& value,std::string& error) {
 value=false;unsigned seen=0;
 for(const auto& option:args.opt)if(option.first=="hooks-only") {
  if(++seen>1){error="--hooks-only may appear only once";return false;}
  if(option.second!="true"&&option.second!="false"){error="--hooks-only must be true or false (use --hooks-only=true or --hooks-only=false)";return false;}
  value=option.second=="true";
 }
 if(seen&&!args.pos.empty()){error="inject takes no positional arguments; use --hooks-only=true or --hooks-only=false for an explicit boolean";return false;}
 return true;
}
static bool ReadinessTransport(DWORD pid,const std::string& request,DWORD budget,std::string& response,HANDLE target=nullptr) {
 const auto deadline=GetTickCount64()+budget;
 HANDLE pipe=OpenPipe(pid,budget,target);if(pipe==INVALID_HANDLE_VALUE)return false;
 struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{pipe};
 const std::string wire=StampOwner(request)+"\n";
 DWORD written=0;if(!WriteFile(pipe,wire.data(),(DWORD)wire.size(),&written,nullptr)||written!=wire.size())return false;
 response.clear();
 while(GetTickCount64()<deadline) {
  if(target&&WaitForSingleObject(target,0)==WAIT_OBJECT_0)return false;
  DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))return false;
  if(!available){Sleep(2);continue;}
  char block[2048];DWORD read=0;if(!ReadFile(pipe,block,(std::min)(available,(DWORD)sizeof(block)),&read,nullptr)||!read)return false;
  response.append(block,read);if(response.size()>65536)return false;
  auto newline=response.find('\n');if(newline!=std::string::npos){response.resize(newline);return true;}
 }
 return false;
}
static bool RequestInputReady(DWORD pid,bool hooksOnly,std::string& response,HANDLE target=nullptr) {
 return ReadinessTransport(pid,std::string("{\"cmd\":\"readiness\",\"wait_ms\":5000,\"hooks_only\":")+(hooksOnly?"true":"false")+"}",6500,response,target);
}
#include "vi_cli_cursor_guard.inl"
static int ReportInputReady(FILE* output,DWORD pid,const std::wstring& dll,bool quiet,bool hooksOnly,bool already,HANDLE target=nullptr) {
 std::string response;const bool received=RequestInputReady(pid,hooksOnly,response,target);
 const auto reply=vi_ready_json::Parse(response);const bool supported=received&&reply.supported;
 const bool ok=supported&&reply.ok&&reply.hooks&&CursorGuardHandshakeReady(response)&&(hooksOnly||(reply.window&&reply.queue));
 if(!quiet||!ok)fprintf(output,"{\"ok\":%s,\"pid\":%lu,\"injected\":true,\"%s\":\"%s\",\"already_injected\":%s,\"readiness\":%s%s%s}\n",ok?"true":"false",pid,already?"requested_dll":"dll",JEsc(W2U(dll)).c_str(),already?"true":"false",supported?response.c_str():"null",already?",\"note\":\"already injected\"":"",ok?"":received?(supported?",\"error\":\"DLL loaded but input infrastructure readiness failed; inspect readiness\"":",\"error\":\"DLL pipe exists but readiness v1 is unsupported; restart the owned target with the matching updated DLL\""):",\"error\":\"DLL loaded but readiness transport/deadline failed; input readiness is unconfirmed\"");
 return ok?0:2;
}
