"""Extract the production Inject remote-loader tail; all OS operations are fakes.
This exercises actual allocator/wait/free/readiness decisions without injection.
"""
import argparse
from pathlib import Path

parser=argparse.ArgumentParser();parser.add_argument('--source',required=True);parser.add_argument('--out',required=True);args=parser.parse_args()
text=Path(args.source).read_text(encoding='utf-8-sig');function=text.index('static int Inject(')
start=text.index('    HANDLE hp = OpenProcess(SYNCHRONIZE',function);end=text.index('\n}',start)
tail=text[start:end]
prefix=r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include "vi_cli_deadline.h"
struct Scenario{DWORD wait=WAIT_OBJECT_0,budget=20000;bool alloc=true,write=true,thread=true,exitCode=true,pipe=true,expireAfterWrite=false;} scenario;
static unsigned frees,threadsCreated,premature,ready,waits;static bool observedComplete;static std::string receipt;static char storage[1024];
static HANDLE Process(){return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1));}static HANDLE Thread(){return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(2));}
static HANDLE FakeOpenProcess(DWORD,BOOL,DWORD){return Process();}
static void* FakeVirtualAllocEx(HANDLE,void*,SIZE_T,DWORD,DWORD){return scenario.alloc?storage:nullptr;}
static BOOL FakeWriteProcessMemory(HANDLE,void*,const void*,SIZE_T bytes,SIZE_T* written){if(written)*written=scenario.write?bytes:0;if(scenario.expireAfterWrite)scenario.budget=0;return scenario.write;}
static BOOL FakeVirtualFreeEx(HANDLE,void*,SIZE_T,DWORD){++frees;if(threadsCreated&&!observedComplete)++premature;return TRUE;}
static HMODULE FakeGetModuleHandleW(const wchar_t*){return reinterpret_cast<HMODULE>(static_cast<ULONG_PTR>(3));}
static FARPROC FakeGetProcAddress(HMODULE,const char*){return reinterpret_cast<FARPROC>(static_cast<ULONG_PTR>(4));}
static HANDLE FakeCreateRemoteThread(HANDLE,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,void*,DWORD,DWORD*){if(!scenario.thread)return nullptr;++threadsCreated;return Thread();}
static DWORD FakeWaitForSingleObject(HANDLE,DWORD){++waits;observedComplete=scenario.wait==WAIT_OBJECT_0;return scenario.wait;}
static BOOL FakeGetExitCodeThread(HANDLE,DWORD* code){*code=1;return scenario.exitCode;}
static BOOL FakeCloseHandle(HANDLE){return TRUE;}static DWORD FakeGetLastError(){return ERROR_ACCESS_DENIED;}
static DWORD CliBudget(DWORD cap){return scenario.budget<cap?scenario.budget:cap;}
static void EmitInjectReply(FILE*,std::string*,const char* format,...){char buffer[8192]{};va_list values;va_start(values,format);vsnprintf(buffer,sizeof(buffer),format,values);va_end(values);receipt=buffer;}
static bool PrintInjectionExit(FILE*,HANDLE,DWORD,const char*,DWORD,std::string*){return false;}
static bool ReadinessTransport(DWORD,const std::string&,DWORD,std::string&,HANDLE){return scenario.pipe;}
static int ReportInputReady(FILE*,DWORD,const std::wstring&,bool,bool,bool,HANDLE,std::string*){++ready;return 0;}
#define OpenProcess FakeOpenProcess
#define VirtualAllocEx FakeVirtualAllocEx
#define WriteProcessMemory FakeWriteProcessMemory
#define VirtualFreeEx FakeVirtualFreeEx
#define GetModuleHandleW FakeGetModuleHandleW
#define GetProcAddress FakeGetProcAddress
#define CreateRemoteThread FakeCreateRemoteThread
#define WaitForSingleObject FakeWaitForSingleObject
#define GetExitCodeThread FakeGetExitCodeThread
#define CloseHandle FakeCloseHandle
#define GetLastError FakeGetLastError
static int Exercise(){const DWORD pid=77;const std::wstring dll=L"owned.dll";FILE* output=nullptr;bool quiet=true,hooksOnly=false;std::string resp;std::string* captured=nullptr;
'''
suffix=r'''
}
int main(){(void)CliBudget(1);unsigned pass=0,fail=0;auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);ok?++pass:++fail;};auto reset=[](){scenario=Scenario{};frees=threadsCreated=premature=ready=waits=0;observedComplete=false;receipt.clear();};
 reset();int result=Exercise();check(result==0&&threadsCreated==1&&ready==1,"completed loader reaches readiness once");check(frees==1&&premature==0,"completed loader releases remote path once");
 reset();scenario.wait=WAIT_TIMEOUT;result=Exercise();check(result!=0&&ready==0,"timed-out loader never becomes readiness success");check(frees==0&&premature==0,"timed-out loader keeps pending remote argument alive");check(receipt.find("\"injected\":null")!=std::string::npos&&receipt.find("\"load_library_completion_confirmed\":false")!=std::string::npos&&receipt.find("\"load_library_may_still_be_running\":true")!=std::string::npos,"timeout receipt preserves unknown injection and pending state");
 reset();scenario.wait=WAIT_FAILED;result=Exercise();check(result!=0&&ready==0&&frees==0&&premature==0,"failed wait cannot free or confirm a running loader");
 reset();scenario.alloc=false;result=Exercise();check(result!=0&&threadsCreated==0&&frees==0,"allocation failure refuses before remote execution");
 reset();scenario.write=false;result=Exercise();check(result!=0&&threadsCreated==0&&frees==1&&premature==0,"partial/failed write refuses before remote execution and cleans safe storage");
 reset();scenario.thread=false;result=Exercise();check(result!=0&&threadsCreated==0&&frees==1&&premature==0,"thread creation failure releases never-consumed argument");
 reset();scenario.expireAfterWrite=true;result=Exercise();check(result!=0&&threadsCreated==0&&frees==1&&premature==0,"deadline expiry before execution never starts a loader");
 reset();scenario.exitCode=false;result=Exercise();check(result!=0&&ready==0&&frees==1&&premature==0,"signalled loader with unknown result cleans only completed storage and retains failure");
 reset();scenario.pipe=false;result=Exercise();check(result!=0&&ready==0&&frees==1,"pipe failure after confirmed loader remains an injection failure");
 printf("CLI_INJECT_BUDGET_RESULT %u %u\n",pass,fail);return fail?1:0;}
'''
Path(args.out).parent.mkdir(parents=True,exist_ok=True);Path(args.out).write_text(prefix+tail+suffix,encoding='utf-8')
