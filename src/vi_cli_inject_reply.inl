// Injection records are formatted once, either to their original stream or into
// caller-owned memory. Launch captures them without redirecting global stdio.
#include <cstdarg>
#include <utility>
#include "vi_ready_json.h"
static void EmitInjectReply(FILE* output,std::string* captured,const char* format,...) {
    va_list arguments,measure;va_start(arguments,format);va_copy(measure,arguments);
    const int count=_vscprintf(format,measure);va_end(measure);
    std::string record;
    if(count<0)record="{\"ok\":false,\"error\":\"could not format injection reply\"}";
    else {
        std::vector<char> bytes((size_t)count+1);
        const int written=vsnprintf_s(bytes.data(),bytes.size(),_TRUNCATE,format,arguments);
        record=written<0?"{\"ok\":false,\"error\":\"could not format injection reply\"}":std::string(bytes.data(),(size_t)written);
    }
    va_end(arguments);
    while(!record.empty()&&(record.back()=='\r'||record.back()=='\n'))record.pop_back();
    if(captured)*captured=std::move(record);
    else if(output)fprintf(output,"%s\n",record.c_str());
}
struct InjectChildHandle {
    HANDLE value=nullptr;
    ~InjectChildHandle(){Reset();}
    void Reset(HANDLE replacement=nullptr){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=replacement;}
};
static bool CopyInjectChildStandard(DWORD standard,bool writable,InjectChildHandle& destination) {
    HANDLE source=GetStdHandle(standard),copy=nullptr;
    if(source&&source!=INVALID_HANDLE_VALUE) {
        if(!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&copy,0,TRUE,DUPLICATE_SAME_ACCESS))return false;
    }else {
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        copy=CreateFileW(L"NUL",writable?GENERIC_WRITE:GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
        if(copy==INVALID_HANDLE_VALUE)return false;
    }
    destination.Reset(copy);return true;
}

// Delegated standalone inject also captures privately; publish once at return.
// Quiet success stays silent, errors retain the caller's original stream.
struct StandaloneInjectChildReply {
 FILE* output;std::string* record;bool direct,quiet;
 ~StandaloneInjectChildReply(){
  if(!direct)return;
  vi_ready_json::Reader parsed{*record,0,{}};bool ok=false;
  const bool valid=parsed.Parse()&&parsed.Bool("ok",ok);
  if(quiet&&valid&&ok)return;
  fprintf(output,"%s\n",valid?record->c_str():"{\"ok\":false,\"error\":\"delegated injector reply unavailable\"}");
 }
};
