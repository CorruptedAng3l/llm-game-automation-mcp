#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <share.h>
#include <string>
// Test-only creator journal. Only these two exact private images may be spawned.
// No child handle is closed/transferred; harness makes ONE initial attachment,
// checks creator-observed creation against kernel PID/time/image, then retains it.
namespace vi_launch_spawn {
using CreateFn=BOOL(WINAPI*)(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION);
inline CreateFn original=nullptr;inline FILE* journal=nullptr;inline std::wstring target,childCli,nonce;
inline bool WaitForWindowPublication(DWORD pid){
 wchar_t path[2048]{};const DWORD length=GetEnvironmentVariableW(L"VI_LAUNCH_TARGET_LOG",path,2048);if(!length||length>=2048)return false;
 const auto until=GetTickCount64()+5000;
 do{
  FILE* input=_wfsopen(path,L"rb",_SH_DENYNO);
  if(input){char buffer[16385]{};const auto size=fread(buffer,1,16384,input);const bool valid=!ferror(input)&&feof(input);fclose(input);
   if(valid){std::string all(buffer,size);size_t start=0;
    while(start<all.size()){auto end=all.find('\n',start);if(end==std::string::npos)break;std::string line=all.substr(start,end-start);start=end+1;if(!line.empty()&&line.back()=='\r')line.pop_back();
     vi_ready_json::Reader row{line,0,{}};if(!row.Parse())continue;auto event=row.fields.find("event"),owner=row.fields.find("pid");
     if(event!=row.fields.end()&&event->second.raw=="\"windows_ready\""&&owner!=row.fields.end()&&owner->second.type=='n'&&owner->second.raw==std::to_string(pid))return true;
    }
   }
  }
  Sleep(2);
 }while(GetTickCount64()<until);return false;
}
inline BOOL WINAPI Create(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES processSecurity,LPSECURITY_ATTRIBUTES threadSecurity,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process){
 const char* role=application&&!_wcsicmp(application,target.c_str())?"target":application&&!_wcsicmp(application,childCli.c_str())?"cli":nullptr;
 if(!role||(flags&CREATE_SUSPENDED)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 // Safety adaptation for BOTH baselines/candidates: console children never
 // receive a real console before their own guards. GUI targets ignore this bit.
 const DWORD safeFlags=flags|CREATE_NO_WINDOW;
 const BOOL ok=original(application,command,processSecurity,threadSecurity,inherit,safeFlags,environment,directory,startup,process);const DWORD error=GetLastError();
 if(ok){
  FILETIME created{},exited{},kernel{},user{};const DWORD id=GetProcessId(process->hProcess);ULARGE_INTEGER stamp{};
  const bool known=id==process->dwProcessId&&GetProcessTimes(process->hProcess,&created,&exited,&kernel,&user);stamp.LowPart=created.dwLowDateTime;stamp.HighPart=created.dwHighDateTime;
  fprintf(journal,"{\"event\":\"spawned_owned\",\"parent_pid\":%lu,\"child_pid\":%lu,\"creation_filetime\":%llu,\"creation_known\":%s,\"role\":\"%s\",\"case_nonce\":\"%s\",\"requested_creation_flags\":%lu,\"effective_creation_flags\":%lu,\"no_console_enforced\":true}\n",GetCurrentProcessId(),id,stamp.QuadPart,known?"true":"false",role,vi_launch_ack::Escape(nonce.c_str()).c_str(),flags,safeFlags);fflush(journal);
  if(known&&!std::strcmp(role,"target")){
   const bool published=WaitForWindowPublication(id);
   fprintf(journal,"{\"event\":\"windows_publication_wait\",\"child_pid\":%lu,\"complete\":%s,\"deadline_ms\":5000}\n",id,published?"true":"false");fflush(journal);
  }
 }
 SetLastError(error);return ok;
}
inline bool Install(FILE* out){
 wchar_t path[32768]{},token[40]{};if(!GetEnvironmentVariableW(L"VI_LAUNCH_TARGET",path,32768)||GetEnvironmentVariableW(L"VI_LAUNCH_CASE_NONCE",token,40)!=32)return false;
 target=path;nonce=token;childCli=ExeDir()+L"\\vinput32.exe";journal=out;
 void* entry=(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateProcessW");void* trampoline=nullptr;
 if(!entry||VIMH_CreateHook(entry,(void*)&Create,&trampoline)!=MH_OK||!trampoline)return false;
 original=(CreateFn)trampoline;
 const bool ready=VIMH_EnableHook(entry)==MH_OK&&vi_cursor_spy::Reaches((void*)CreateProcessW,(void*)&Create);
 fprintf(out,"{\"event\":\"spawn_guard\",\"pid\":%lu,\"ready\":%s}\n",GetCurrentProcessId(),ready?"true":"false");fflush(out);return ready;
}
}
