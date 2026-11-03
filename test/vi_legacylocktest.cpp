#include "vi_sessions.h"
#include <fstream>
#include <iterator>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok)++failures;printf("%s %s\n",ok?"PASS":"FAIL",name);}
static bool Write(const std::wstring& path,const std::string& body){HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;DWORD n=0;bool ok=WriteFile(h,body.data(),(DWORD)body.size(),&n,nullptr)&&n==body.size();CloseHandle(h);return ok;}
static std::string Read(const std::wstring& path){std::ifstream f(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
static bool Age(const std::wstring& path,int seconds){HANDLE h=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,0,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)return false;FILETIME t{};GetSystemTimeAsFileTime(&t);ULARGE_INTEGER value{};value.QuadPart=vi_session::FileTimeValue(t)-(LONGLONG)seconds*10000000; t.dwLowDateTime=value.LowPart;t.dwHighDateTime=value.HighPart;bool ok=SetFileTime(h,nullptr,nullptr,&t)!=FALSE;CloseHandle(h);return ok;}
static bool Has(const std::string& s,const std::string& part){return s.find(part)!=std::string::npos;}
int wmain(int argc,wchar_t** argv){
 using namespace vi_session;
 if(argc!=2||GetFileAttributesW(argv[1])!=INVALID_FILE_ATTRIBUTES)return 2;
 const std::wstring path=argv[1];std::string error;
 const std::string legacy="{\"time\":\"2026-11-02\",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"owner\":\"Agent \\\"A\\\"\\n\\u001b[31m\\u2603\",\"purpose\":\"DO_NOT_PRINT_SECRET\"}\n";
 Check(Write(path,legacy)&&Age(path,90),"create own aged legacy holder file");
 {JsonLock a;auto s=InspectLock(path);Check(s.exists&&!s.format_ok&&!s.stale&&!s.owner_dead&&!s.expired&&s.pid==0,"legacy metadata never establishes lease identity or staleness");
 Check(!a.Acquire(path,"new-agent",120,error),"legacy holder acquisition refused");printf("DIAGNOSTIC %s\n",error.c_str());
 Check(Has(error,"holder UNVERIFIED")&&Has(error,"raw_pid="+std::to_string(GetCurrentProcessId())),"legacy PID explicitly unverified in busy diagnostic");
 Check(Has(error,"owner=\"Agent \\\"A\\\"\\u000a\\u001b[31m")&&!Has(error,"\x1b")&&!Has(error,"\n"),"owner control characters escaped on one diagnostic line");
 auto pos=error.find("file_age_ms=");std::uint64_t age=pos==std::string::npos?0:_strtoui64(error.c_str()+pos+12,nullptr,10);
 Check(Has(error,"age_source=file_mtime")&&age>=89000&&age<100000,"age derived from file mtime with explicit source");
 Check(!Has(error,"DO_NOT_PRINT_SECRET"),"unrelated file contents never printed");
 Check(!ReleaseOwnedLock(path,"Agent \"A\"\n\x1b[31m\xE2\x98\x83",error)&&Read(path)==legacy,"unverified owner cannot release or mutate lock");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own legacy fixture");
 const std::string malformed="{\"pid\":123,\"owner\":\"DO_NOT_PRINT_BAD\",oops";
 Check(Write(path,malformed),"create own malformed fixture");
 {JsonLock a;Check(!a.Acquire(path,"new",120,error)&&Has(error,"malformed")&&!Has(error,"DO_NOT_PRINT_BAD")&&!Has(error,"raw_pid="),"malformed bytes reported without partial holder claims");Check(Read(path)==malformed&&!ReleaseOwnedLock(path,"new",error),"malformed lock remains unchanged and unreleased");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own malformed fixture");
 const std::string oversized="{\"pid\":123,\"owner\":\"DO_NOT_PRINT_LARGE\"}"+std::string(8193,'x');
 Check(Write(path,oversized),"create own oversized fixture");
 {JsonLock a;auto start=GetTickCount64();Check(!a.Acquire(path,"new",120,error)&&Has(error,"too large")&&!Has(error,"DO_NOT_PRINT_LARGE")&&error.size()<512&&GetTickCount64()-start<250,"oversized lock diagnostic bounded without contents");Check(Read(path)==oversized,"oversized lock exact bytes preserved");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own oversized fixture");
 Check(Write(path,legacy),"create own unreadable fixture");
 {HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);Check(held!=INVALID_HANDLE_VALUE,"exclusive own handle denies inspection");JsonLock a;auto start=GetTickCount64();Check(!a.Acquire(path,"new",120,error)&&Has(error,"cannot be inspected")&&Has(error,"win32=")&&!Has(error,"raw_pid=")&&GetTickCount64()-start<250,"unreadable lock reason is bounded and claims no holder");if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);Check(Read(path)==legacy,"unreadable fixture bytes retained");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own unreadable fixture");
 const std::string longOwner(255,'a');const std::string longBody="{\"pid\":4294967296,\"owner\":\""+longOwner+"\xE2\x98\x83"+std::string(1000,'b')+"\"}";
 Check(Write(path,longBody),"create own bounded owner fixture");
 {JsonLock a;Check(!a.Acquire(path,"new",120,error)&&!Has(error,"raw_pid=")&&Has(error,"owner_truncated=true")&&Has(error,"owner=\""+longOwner+"\"")&&error.size()<1024,"owner truncation preserves UTF8 boundary and rejects oversized PID");Check(Read(path)==longBody,"truncated diagnostic never truncates file");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own bounded owner fixture");
 Check(Write(path,"{\"pid\":\"123\",\"owner\":\"test\"}")&&Age(path,-90),"create future mtime and quoted PID fixture");
 {JsonLock a;auto s=InspectLock(path);Check(!a.Acquire(path,"new",120,error)&&!Has(error,"raw_pid=")&&Has(error,"file_age_ms=0")&&Has(error,"age_source=file_mtime")&&!s.stale,"future file age clamps zero and quoted PID never promoted");}
 Check(DeleteFileW(path.c_str())!=FALSE,"remove own future fixture");
 printf("%s: %u checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
