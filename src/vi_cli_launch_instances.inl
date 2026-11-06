// Direct-exe duplicate prevention only. Existing processes are observed, never
// attached, claimed, queried through vinput, injected, or used as launch targets.
struct LaunchInstanceGuard {
    struct Existing { HANDLE handle=nullptr; DWORD pid=0; unsigned long long born=0; };
    HANDLE imageHandle=INVALID_HANDLE_VALUE, mutex=nullptr;
    bool held=false, complete=false, bypassed=false;
    DWORD error=0, uncertainPid=0;
    std::wstring image;
    std::string reason;
    std::vector<Existing> existing;
    LaunchInstanceGuard()=default;
    LaunchInstanceGuard(const LaunchInstanceGuard&)=delete;
    LaunchInstanceGuard& operator=(const LaunchInstanceGuard&)=delete;
    ~LaunchInstanceGuard(){ Release(); for(auto& p:existing)if(p.handle)CloseHandle(p.handle); }
    void Release(){
        if(held){ReleaseMutex(mutex);held=false;}
        if(mutex){CloseHandle(mutex);mutex=nullptr;}
        if(imageHandle!=INVALID_HANDLE_VALUE){CloseHandle(imageHandle);imageHandle=INVALID_HANDLE_VALUE;}
    }
    static bool Same(const std::wstring& a,const std::wstring& b){return !a.empty()&&!b.empty()&&CompareStringOrdinal(a.c_str(),(int)a.size(),b.c_str(),(int)b.size(),TRUE)==CSTR_EQUAL;}
    static std::wstring Base(const std::wstring& path){const auto slash=path.find_last_of(L"\\/");return slash==std::wstring::npos?path:path.substr(slash+1);}
    static bool FinalName(HANDLE file,std::wstring& path){
        std::vector<wchar_t> buffer(32769);
        const auto length=GetFinalPathNameByHandleW(file,buffer.data(),(DWORD)buffer.size(),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!length||length>=buffer.size())return false;
        path.assign(buffer.data(),length);
        if(path.compare(0,8,L"\\\\?\\UNC\\")==0)path=L"\\\\"+path.substr(8);
        else if(path.compare(0,4,L"\\\\?\\")==0)path.erase(0,4);
        return !path.empty();
    }
    bool Fail(const char* why,DWORD code=0,DWORD pid=0){reason=why;error=code;uncertainPid=pid;complete=false;return false;}
    bool Prepare(std::wstring& requested,bool newInstance){
        bypassed=newInstance;
        std::wstring absolute;
        if(!LaunchAbsolutePath(requested,absolute))return Fail("requested_image_path_unresolved",GetLastError());
        // Keep the requested file open without delete sharing through creation.
        // A concurrent path replacement cannot change this mutex's file identity.
        imageHandle=CreateFileW(absolute.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(imageHandle==INVALID_HANDLE_VALUE){
            const auto code=GetLastError();
            if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND){requested=absolute;image=absolute;error=code;complete=true;reason="requested_image_missing";return true;}
            return Fail("requested_image_open_failed",code);
        }
        BY_HANDLE_FILE_INFORMATION identity{};
        if(!GetFileInformationByHandle(imageHandle,&identity)||!FinalName(imageHandle,image))return Fail("requested_image_identity_unresolved",GetLastError());
        if(identity.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)return Fail("requested_image_is_directory",ERROR_DIRECTORY);
        requested=image;
        wchar_t name[160]{};
        swprintf_s(name,L"Local\\vinput.launch.exact-image.v1.%08lX.%08lX%08lX",identity.dwVolumeSerialNumber,identity.nFileIndexHigh,identity.nFileIndexLow);
        mutex=CreateMutexW(nullptr,FALSE,name);
        if(!mutex)return Fail("instance_mutex_open_failed",GetLastError());
        const DWORD budget=CliBudget(30000);
        if(!budget)return Fail("instance_mutex_deadline",ERROR_TIMEOUT);
        const DWORD waited=WaitForSingleObject(mutex,budget);
        if(waited!=WAIT_OBJECT_0&&waited!=WAIT_ABANDONED)return Fail(waited==WAIT_TIMEOUT?"instance_mutex_deadline":"instance_mutex_wait_failed",waited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());
        held=true;
        if(newInstance){complete=false;reason="explicit_new_instance";return true;}
        FILETIME before{};GetSystemTimeAsFileTime(&before);const auto cutoff=LaunchStamp(before);
        if(!cutoff)return Fail("inventory_clock_unavailable");
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE)return Fail("process_snapshot_failed",GetLastError());
        PROCESSENTRY32W row{sizeof(row)};BOOL next=Process32FirstW(snapshot,&row);
        if(!next){const auto code=GetLastError();CloseHandle(snapshot);return Fail("process_snapshot_empty_or_failed",code);}
        unsigned rows=0,candidates=0;bool good=true;
        do{
            if(++rows>65536||!CliBudget(1)){good=Fail("inventory_limit_or_deadline",ERROR_TIMEOUT);break;}
            if(!Same(Base(image),row.szExeFile))continue;
            if(++candidates>1024){good=Fail("inventory_candidate_limit",ERROR_MORE_DATA);break;}
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,row.th32ProcessID);
            if(!process){good=Fail("candidate_open_failed",GetLastError(),row.th32ProcessID);break;}
            DWORD alive=WaitForSingleObject(process,0);
            if(alive==WAIT_OBJECT_0){CloseHandle(process);continue;}
            FILETIME born{},ended{},kernel{},user{};std::wstring raw(32768,L'\0');DWORD size=(DWORD)raw.size();
            bool known=alive==WAIT_TIMEOUT&&GetProcessId(process)==row.th32ProcessID&&GetProcessTimes(process,&born,&ended,&kernel,&user)&&QueryFullProcessImageNameW(process,0,raw.data(),&size)&&size&&size<raw.size();
            if(known){raw.resize(size);known=vi_launch_follow::SnapshotIdentity(row.th32ProcessID,row.szExeFile,GetProcessId(process),raw,LaunchStamp(born),cutoff);}
            if(!known){const auto code=GetLastError();const bool exited=WaitForSingleObject(process,CliBudget(1500))==WAIT_OBJECT_0;CloseHandle(process);if(exited)continue;good=Fail("candidate_identity_unresolved",code,row.th32ProcessID);break;}
            HANDLE file=CreateFileW(raw.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            std::wstring canonical;known=file!=INVALID_HANDLE_VALUE&&FinalName(file,canonical);const auto code=known?0:GetLastError();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
            if(!known){const bool exited=WaitForSingleObject(process,CliBudget(1500))==WAIT_OBJECT_0;CloseHandle(process);if(exited)continue;good=Fail("candidate_image_path_unresolved",code,row.th32ProcessID);break;}
            alive=WaitForSingleObject(process,0);
            if(alive==WAIT_OBJECT_0){CloseHandle(process);continue;}
            if(alive!=WAIT_TIMEOUT){const auto waitError=GetLastError();CloseHandle(process);good=Fail("candidate_liveness_unresolved",waitError,row.th32ProcessID);break;}
            if(!Same(canonical,image)){CloseHandle(process);continue;}
            if(existing.size()>=64){CloseHandle(process);good=Fail("matching_instance_limit",ERROR_MORE_DATA,row.th32ProcessID);break;}
            existing.push_back({process,row.th32ProcessID,LaunchStamp(born)});
        }while((next=Process32NextW(snapshot,&row))!=FALSE);
        const auto enumerationError=GetLastError();CloseHandle(snapshot);
        if(!good)return false;
        if(enumerationError!=ERROR_NO_MORE_FILES)return Fail("process_snapshot_incomplete",enumerationError);
        // Recheck retained handles immediately before deciding; exited matches do
        // not block. A failed liveness check never becomes absence evidence.
        for(auto i=existing.begin();i!=existing.end();){
            const auto alive=WaitForSingleObject(i->handle,0);
            if(alive==WAIT_OBJECT_0){CloseHandle(i->handle);i=existing.erase(i);}
            else if(alive!=WAIT_TIMEOUT)return Fail("matching_instance_liveness_unresolved",GetLastError(),i->pid);
            else ++i;
        }
        complete=true;reason=existing.empty()?"no_existing_exact_image":"existing_exact_image";
        return existing.empty();
    }
    std::string Fields()const{
        std::string value=",\"duplicate_check\":{\"scope\":\"direct_exe_exact_canonical_image\",\"inventory_complete\":"+std::string(complete?"true":"false")+",\"bypassed\":"+(bypassed?"true":"false")+",\"reason\":\""+JEsc(reason)+"\",\"requested_image\":\""+JEsc(W2U(image))+"\",\"ownership\":\"not_verified\",\"win32_error\":"+std::to_string(error)+",\"uncertain_pid\":"+(uncertainPid?std::to_string(uncertainPid):"null")+"},\"existing_instances\":[";
        for(size_t i=0;i<existing.size();++i){if(i)value+=',';const auto& p=existing[i];value+="{\"pid\":"+std::to_string(p.pid)+",\"process_created\":\""+std::to_string(p.born)+"\",\"same_image_verified\":true}";}
        return value+"]";
    }
};
