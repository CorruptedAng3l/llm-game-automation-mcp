// Narrow OS adapter for the pure process-tree selection policy.
#include "vi_launch_follow.h"
static unsigned long long LaunchStamp(FILETIME t){ULARGE_INTEGER n{};n.LowPart=t.dwLowDateTime;n.HighPart=t.dwHighDateTime;return n.QuadPart;}
static vi_launch_follow::Process LaunchProcess(DWORD pid,DWORD parent,const std::wstring& name,HANDLE handle=nullptr,unsigned long long beforeSnapshot=0){
    vi_launch_follow::Process p{};p.pid=pid;p.parent=parent;p.name=name;
    HANDLE h=handle?handle:OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(h){FILETIME b{},e{},k{},u{};const auto handlePid=GetProcessId(h);if(handlePid==pid&&GetProcessTimes(h,&b,&e,&k,&u)){
        bool matches=true;
        if(beforeSnapshot){std::wstring image(32768,L'\0');DWORD size=(DWORD)image.size();matches=QueryFullProcessImageNameW(h,0,image.data(),&size)&&size&&size<image.size();if(matches){image.resize(size);matches=vi_launch_follow::SnapshotIdentity(pid,name,handlePid,image,LaunchStamp(b),beforeSnapshot);}}
        if(matches){p.born=LaunchStamp(b);p.ended=LaunchStamp(e);p.alive=WaitForSingleObject(h,0)==WAIT_TIMEOUT;}
    }if(!handle)CloseHandle(h);}
    return p;
}
static std::vector<vi_launch_follow::Process> LaunchSnapshot(const vi_launch_follow::Tree* tree=nullptr,const std::wstring& target=L""){
    std::vector<vi_launch_follow::Process> rows;
    FILETIME before{};GetSystemTimeAsFileTime(&before);const auto beforeSnapshot=LaunchStamp(before);if(!beforeSnapshot)return rows;
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W e{sizeof(e)};
    if(snap==INVALID_HANDLE_VALUE)return rows;
    if(Process32FirstW(snap,&e))do{vi_launch_follow::Process p{};p.pid=e.th32ProcessID;p.parent=e.th32ParentProcessID;p.name=e.szExeFile;rows.push_back(p);}while(Process32NextW(snap,&e));
    CloseHandle(snap);
    std::set<DWORD> relevant;
    const auto launcher=vi_launch_follow::Launcher(target);
    for(const auto& p:rows)if((tree&&tree->TrackedPid(p.pid))||(!target.empty()&&(vi_launch_follow::Name(p.name,target)||(!launcher.empty()&&vi_launch_follow::Name(p.name,launcher)))))relevant.insert(p.pid);
    bool added=true;while(added){added=false;for(const auto& p:rows)if(relevant.count(p.parent)&&!relevant.count(p.pid)){relevant.insert(p.pid);added=true;}}
    std::vector<vi_launch_follow::Process> observed;
    for(const auto& p:rows)if(relevant.count(p.pid)){auto current=LaunchProcess(p.pid,p.parent,p.name,nullptr,beforeSnapshot);if(current.born)observed.push_back(current);}
    return observed;
}
struct LaunchProcessHandles {
    struct Held {HANDLE handle;vi_launch_follow::Process process;};
    std::vector<Held> held;
    ~LaunchProcessHandles(){for(auto& h:held)CloseHandle(h.handle);}
    void Retain(const vi_launch_follow::Process& p,HANDLE original=nullptr){
        if(!p.born)return;
        for(const auto& h:held)if(h.process.pid==p.pid&&h.process.born==p.born)return;
        HANDLE h=nullptr;
        if(original)DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&h,0,FALSE,DUPLICATE_SAME_ACCESS);
        else h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.pid);
        if(h){auto now=LaunchProcess(p.pid,p.parent,p.name,h);if(now.born==p.born)held.push_back({h,p});else CloseHandle(h);}
    }
    void Refresh(vi_launch_follow::Tree& tree){for(auto& h:held)tree.Remember(LaunchProcess(h.process.pid,h.process.parent,h.process.name,h.handle));}
};
