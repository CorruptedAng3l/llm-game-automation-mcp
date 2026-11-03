// Actual kernel pipe-access probe. Restricted identities are temporary thread tokens;
// this never creates users, changes credentials, or modifies network configuration.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include <cstdio>
#pragma comment(lib,"advapi32.lib")

static std::string Utf8(const wchar_t* text) {
    int length=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    std::string result(length?length:1,'\0');
    if(length)WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),length,nullptr,nullptr);
    if(!result.empty())result.pop_back();return result;
}
static std::string Escape(const std::string& text){std::string out;for(char c:text){if(c=='\\'||c=='"')out+='\\';if((unsigned char)c>=32)out+=c;}return out;}
static bool Transfer(HANDLE pipe,bool writing,void* bytes,DWORD size,DWORD& done) {
    OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!operation.hEvent)return false;
    BOOL ok=writing?WriteFile(pipe,bytes,size,&done,&operation):ReadFile(pipe,bytes,size,&done,&operation);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(operation.hEvent,1500)==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&operation,&done,FALSE);
        else{CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&done,TRUE);}}
    CloseHandle(operation.hEvent);return ok!=FALSE;
}
static int ReferenceServer(){
    // Fresh owner-only ping reference WITHOUT remote rejection. It has no control,
    // file-writing or input command: a UNC success proves the transport is available.
    HANDLE token=nullptr;DWORD bytes=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return 3;
    GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> user(bytes);
    if(!bytes||!GetTokenInformation(token,TokenUser,user.data(),bytes,&bytes)){CloseHandle(token);return 3;}CloseHandle(token);
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(((TOKEN_USER*)user.data())->User.Sid,&sid))return 3;
    std::wstring sddl=L"D:(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return 3;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),sd,FALSE};wchar_t name[80];swprintf_s(name,L"\\\\.\\pipe\\vinput_%lu",GetCurrentProcessId());bool announced=false;
    for(;;){HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,PIPE_UNLIMITED_INSTANCES,4096,4096,0,&attributes);if(pipe==INVALID_HANDLE_VALUE){LocalFree(sd);return 3;}
        if(!announced){printf("{\"ready\":true,\"pid\":%lu,\"remote_rejection\":false,\"commands\":\"ping_only\"}\n",GetCurrentProcessId());fflush(stdout);announced=true;}
        if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){char request[4096];DWORD n=0;if(ReadFile(pipe,request,sizeof(request),&n,nullptr)&&n){const char reply[]="{\"ok\":true}\n";WriteFile(pipe,reply,sizeof(reply)-1,&n,nullptr);FlushFileBuffers(pipe);}}
        DisconnectNamedPipe(pipe);CloseHandle(pipe);
    }
}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&!wcscmp(argv[1],L"--reference"))return ReferenceServer();
    if(argc<3)return 2;DWORD target=(DWORD)wcstoul(argv[1],nullptr,10);std::wstring identity=argv[2];
    const wchar_t* host=argc>3?argv[3]:L".";
    HANDLE original=nullptr,restricted=nullptr;bool impersonating=false;DWORD setupError=0,attributes=0;std::wstring userSid;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,&original))setupError=GetLastError();
    std::vector<BYTE> user;DWORD needed=0;
    if(!setupError){GetTokenInformation(original,TokenUser,nullptr,0,&needed);user.resize(needed);
        if(!needed||!GetTokenInformation(original,TokenUser,user.data(),needed,&needed))setupError=GetLastError();}
    if(!setupError){LPWSTR sid=nullptr;if(ConvertSidToStringSidW(((TOKEN_USER*)user.data())->User.Sid,&sid)){userSid=sid;LocalFree(sid);}else setupError=GetLastError();}
    if(!setupError&&identity!=L"owner"){
        SID_AND_ATTRIBUTES deny{},restricting{};BYTE world[SECURITY_MAX_SID_SIZE];DWORD worldLength=sizeof(world);
        if(identity==L"deny-owner")deny.Sid=((TOKEN_USER*)user.data())->User.Sid;
        else if(identity==L"restrict-world"){if(!CreateWellKnownSid(WinWorldSid,nullptr,world,&worldLength))setupError=GetLastError();restricting.Sid=world;}
        else setupError=ERROR_INVALID_PARAMETER;
        if(!setupError&&!CreateRestrictedToken(original,DISABLE_MAX_PRIVILEGE,deny.Sid?1:0,deny.Sid?&deny:nullptr,0,nullptr,restricting.Sid?1:0,restricting.Sid?&restricting:nullptr,&restricted))setupError=GetLastError();
        if(!setupError){DWORD count=0;GetTokenInformation(restricted,TokenUser,nullptr,0,&count);std::vector<BYTE> info(count);
            if(!count||!GetTokenInformation(restricted,TokenUser,info.data(),count,&count))setupError=GetLastError();else attributes=((TOKEN_USER*)info.data())->User.Attributes;}
        if(!setupError){impersonating=ImpersonateLoggedOnUser(restricted)!=FALSE;if(!impersonating)setupError=GetLastError();}
    }
    wchar_t path[256];swprintf_s(path,L"\\\\%s\\pipe\\vinput_%lu",host,target);
    HANDLE pipe=INVALID_HANDLE_VALUE;DWORD connectError=setupError;bool ping=false;std::string descriptor;DWORD descriptorError=0;unsigned aceCount=0;
    if(!setupError){ULONGLONG until=GetTickCount64()+1500;do{pipe=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);connectError=pipe==INVALID_HANDLE_VALUE?GetLastError():0;
        if(pipe!=INVALID_HANDLE_VALUE||connectError!=ERROR_PIPE_BUSY)break;WaitNamedPipeW(path,25);}while(GetTickCount64()<until);}
    if(pipe!=INVALID_HANDLE_VALUE){
        PSECURITY_DESCRIPTOR sd=nullptr;PACL dacl=nullptr;descriptorError=GetSecurityInfo(pipe,SE_KERNEL_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&sd);
        if(!descriptorError){if(dacl)aceCount=dacl->AceCount;LPWSTR text=nullptr;if(ConvertSecurityDescriptorToStringSecurityDescriptorW(sd,SDDL_REVISION_1,DACL_SECURITY_INFORMATION,&text,nullptr)){descriptor=Utf8(text);LocalFree(text);}else descriptorError=GetLastError();LocalFree(sd);}
        char request[]="{\"cmd\":\"ping\"}\n",reply[4096]{};DWORD n=0;
        if(Transfer(pipe,true,request,sizeof(request)-1,n)&&n==sizeof(request)-1&&Transfer(pipe,false,reply,sizeof(reply)-1,n)){reply[n]=0;ping=std::string(reply).find("\"ok\":true")!=std::string::npos;}
        CloseHandle(pipe);
    }
    const bool reverted=!impersonating||RevertToSelf()!=FALSE;
    if(restricted)CloseHandle(restricted);if(original)CloseHandle(original);
    printf("{\"identity\":\"%s\",\"host\":\"%s\",\"setup_error\":%lu,\"token_user_sid\":\"%s\",\"token_user_attributes\":%lu,\"impersonated\":%s,\"reverted\":%s,\"connected\":%s,\"connect_error\":%lu,\"ping\":%s,\"dacl\":\"%s\",\"dacl_error\":%lu,\"ace_count\":%u}\n",
        Escape(Utf8(identity.c_str())).c_str(),Escape(Utf8(host)).c_str(),setupError,Escape(Utf8(userSid.c_str())).c_str(),attributes,impersonating?"true":"false",reverted?"true":"false",pipe!=INVALID_HANDLE_VALUE?"true":"false",connectError,ping?"true":"false",Escape(descriptor).c_str(),descriptorError,aceCount);
    return setupError||!reverted?2:0;
}
