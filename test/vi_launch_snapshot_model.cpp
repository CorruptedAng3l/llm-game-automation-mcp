// Exact production OS adapter with private, finite Toolhelp/handle/time callbacks.
// Never opens a real process, creates a window, or calls native Win32 endpoints.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include "vi_launch_follow.h"
struct Row{DWORD pid=20,parent=10,handlePid=20;std::wstring name=L"Game.exe",image=L"E:\\Owned\\Game.exe";unsigned long long born=50,ended=0;bool alive=true,times=true,path=true;};
std::vector<Row> modelRows;size_t cursor=0;unsigned long long bound=100;unsigned timeReads=0,snapshotCalls=0,openCalls=0,pathReads=0,idReads=0,closed=0;bool timeBefore=false,sameHandle=true;HANDLE lastOpened=nullptr;
FILETIME Stamp(unsigned long long n){ULARGE_INTEGER x{};x.QuadPart=n;return {x.LowPart,x.HighPart};}
void WINAPI ModelTime(LPFILETIME value){++timeReads;timeBefore=snapshotCalls==0;*value=Stamp(bound);}
HANDLE WINAPI ModelSnapshot(DWORD,DWORD){++snapshotCalls;cursor=0;return reinterpret_cast<HANDLE>(1);}
BOOL RowAt(LPPROCESSENTRY32W e){if(cursor>=modelRows.size())return FALSE;const auto& row=modelRows[cursor];*e=PROCESSENTRY32W{sizeof(*e)};e->th32ProcessID=row.pid;e->th32ParentProcessID=row.parent;wcscpy_s(e->szExeFile,row.name.c_str());return TRUE;}
BOOL WINAPI ModelFirst(HANDLE,LPPROCESSENTRY32W e){cursor=0;return RowAt(e);}
BOOL WINAPI ModelNext(HANDLE,LPPROCESSENTRY32W e){++cursor;return RowAt(e);}
HANDLE WINAPI ModelOpen(DWORD,BOOL,DWORD pid){++openCalls;for(auto& row:modelRows)if(row.pid==pid){lastOpened=&row;return lastOpened;}return nullptr;}
DWORD WINAPI ModelId(HANDLE h){++idReads;sameHandle=sameHandle&&h==lastOpened;return static_cast<Row*>(h)->handlePid;}
BOOL WINAPI ModelTimes(HANDLE h,LPFILETIME b,LPFILETIME e,LPFILETIME k,LPFILETIME u){sameHandle=sameHandle&&h==lastOpened;const auto& row=*static_cast<Row*>(h);if(!row.times)return FALSE;*b=Stamp(row.born);*e=Stamp(row.ended);*k=*u={};return TRUE;}
BOOL WINAPI ModelPath(HANDLE h,DWORD,LPWSTR text,PDWORD length){++pathReads;sameHandle=sameHandle&&h==lastOpened;const auto& row=*static_cast<Row*>(h);if(!row.path||row.image.size()>=*length)return FALSE;wcscpy_s(text,*length,row.image.c_str());*length=static_cast<DWORD>(row.image.size());return TRUE;}
DWORD WINAPI ModelWait(HANDLE h,DWORD){sameHandle=sameHandle&&h==lastOpened;return static_cast<Row*>(h)->alive?WAIT_TIMEOUT:WAIT_OBJECT_0;}
BOOL WINAPI ModelClose(HANDLE){++closed;return TRUE;}
HANDLE WINAPI ModelCurrent(){return reinterpret_cast<HANDLE>(2);}
BOOL WINAPI ModelDuplicate(HANDLE,HANDLE source,HANDLE,LPHANDLE copy,DWORD,BOOL,DWORD){*copy=source;return TRUE;}
#define GetSystemTimeAsFileTime ModelTime
#define CreateToolhelp32Snapshot ModelSnapshot
#define Process32FirstW ModelFirst
#define Process32NextW ModelNext
#define OpenProcess ModelOpen
#define GetProcessId ModelId
#define GetProcessTimes ModelTimes
#define QueryFullProcessImageNameW ModelPath
#define WaitForSingleObject ModelWait
#define CloseHandle ModelClose
#define GetCurrentProcess ModelCurrent
#define DuplicateHandle ModelDuplicate
#include "actual_launch_adapter.inl"
unsigned passed=0,failed=0;
void Check(bool b,const char* what){printf("%s %s\n",b?"PASS":"FAIL",what);b?++passed:++failed;}
void Reset(Row row=Row{}){modelRows={row};cursor=0;bound=100;timeReads=snapshotCalls=openCalls=pathReads=idReads=closed=0;timeBefore=false;sameHandle=true;lastOpened=nullptr;}
std::vector<vi_launch_follow::Process> Snapshot(){return LaunchSnapshot(nullptr,L"Game.exe");}
int main(){
 using namespace vi_launch_follow;
 Reset();auto found=Snapshot();Check(found.size()==1&&found[0].born==50&&found[0].parent==10&&found[0].alive,"stable snapshot row retains exact observed process identity and parent");
 Check(timeReads==1&&timeBefore&&snapshotCalls==1,"system FILETIME is sampled strictly before Toolhelp snapshot creation");
 Check(openCalls==1&&pathReads==1&&idReads==1&&sameHandle&&closed==2,"PID creation time and full image name are read from one opened handle and handles close");
 Row row;row.born=150;Reset(row);found=Snapshot();Tree tree({10,1,10,0,L"Stub.exe",true,false});for(auto& p:found)p.window=true;tree.Observe(found);Check(found.empty()&&tree.Candidates(found,L"Game.exe").empty(),"replacement PID born after snapshot bound never inherits stale parent/name");
 row={};row.image=L"E:\\Other\\Unrelated.exe";Reset(row);Check(Snapshot().empty(),"same PID with a different handle image basename cannot inherit snapshot name");
 row={};row.handlePid=21;Reset(row);Check(Snapshot().empty(),"handle PID mismatch cannot establish snapshot identity");
 row={};row.path=false;Reset(row);Check(Snapshot().empty(),"unknown handle image fails closed before descendant admission");
 row={};row.times=false;Reset(row);found=Snapshot();Check(found.empty()||!found[0].born,"unknown creation time cannot establish identity");
 row={};row.born=101;Reset(row);found=Snapshot();Check(found.empty(),"new process after pre-snapshot cutoff is deferred");bound=200;found=Snapshot();Check(found.size()==1&&found[0].born==101,"deferred legitimate child is visible on the next fresh snapshot");
 row={};row.born=100;row.image=L"E:/Owned/GAME.EXE";Reset(row);found=Snapshot();Check(found.size()==1&&found[0].born==100,"cutoff equality and ASCII basename case preserve legitimate identity");
 row={};row.name=L"Other.exe";row.parent=99;Reset(row);Check(Snapshot().empty()&&openCalls==0,"unrelated snapshot rows do not open arbitrary processes");
 Reset();bound=0;Check(Snapshot().empty()&&snapshotCalls==0&&openCalls==0,"unavailable pre-snapshot timestamp refuses discovery");
 Check(SnapshotIdentity(20,L"Game.exe",20,L"E:\\Owned\\Game.exe",50,100)&&!SnapshotIdentity(20,L"Game.exe",20,L"E:\\Owned\\Game.exe",101,100)&&!SnapshotIdentity(20,L"Game.exe",20,L"E:\\Owned\\Wrong.exe",50,100),"actual shared identity predicate binds birth and basename to the snapshot row");
 printf("launch snapshot model: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
