// Bounded local named-pipe transport. Each pending I/O owns its byte buffer and
// OVERLAPPED storage. Cancellation is requested without an unbounded drain wait.
// If cancellation completion is not yet observable, retain that storage until
// this short-lived CLI exits; closing the pipe does not make premature reuse safe.
struct CliPipeOperation {
 OVERLAPPED operation{};std::vector<char> bytes;
 explicit CliPipeOperation(size_t size):bytes(size){operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);}
 ~CliPipeOperation(){if(operation.hEvent)CloseHandle(operation.hEvent);}
};
static bool CliPipeTransfer(HANDLE pipe,bool write,void* bytes,DWORD size,DWORD& done,ULONGLONG deadline,HANDLE target){
 done=0;if(!CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),MAXDWORD)))return false;
 auto pending=std::make_unique<CliPipeOperation>(size);if(!pending->operation.hEvent)return false;if(write)memcpy(pending->bytes.data(),bytes,size);
 BOOL ok=write?WriteFile(pipe,pending->bytes.data(),size,nullptr,&pending->operation):ReadFile(pipe,pending->bytes.data(),size,nullptr,&pending->operation);
 if(!ok&&GetLastError()==ERROR_IO_PENDING){
  const DWORD budget=CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),MAXDWORD));HANDLE waits[]={pending->operation.hEvent,target};
  const DWORD waited=target?WaitForMultipleObjects(2,waits,FALSE,budget):WaitForSingleObject(waits[0],budget);
  if(waited!=WAIT_OBJECT_0){
   CancelIoEx(pipe,&pending->operation);DWORD ignored=0;
   if(!GetOverlappedResult(pipe,&pending->operation,&ignored,FALSE)&&GetLastError()==ERROR_IO_INCOMPLETE)(void)pending.release();
   done=0;return false;
  }
  ok=GetOverlappedResult(pipe,&pending->operation,&done,FALSE);
 }else if(ok)ok=GetOverlappedResult(pipe,&pending->operation,&done,FALSE);
 if(!ok||done>size)return false;if(!write&&done)memcpy(bytes,pending->bytes.data(),done);return true;
}
static bool BoundedCliRequest(DWORD pid,const std::string& request,DWORD budget,std::string& response,HANDLE target=nullptr,size_t maxBytes=65536){
 response.clear();
 budget=CliBudget(budget);if(!budget||request.size()>1024*1024||!maxBytes||maxBytes>1024*1024)return false;
 const ULONGLONG deadline=vi_cli_deadline::End(GetTickCount64(),budget);const auto name=PipeName(pid);
 struct Pipe{HANDLE handle=INVALID_HANDLE_VALUE;~Pipe(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} pipe;
 while(CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),MAXDWORD))){
  if(target&&WaitForSingleObject(target,0)==WAIT_OBJECT_0)return false;
  pipe.handle=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe.handle!=INVALID_HANDLE_VALUE)break;
  Sleep(CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),2)));
 }
 if(pipe.handle==INVALID_HANDLE_VALUE)return false;
 ULONG server=0;if(!GetNamedPipeServerProcessId(pipe.handle,&server)||server!=pid)return false;
 std::string wire=StampOwner(request)+"\n";DWORD done=0;if(!CliPipeTransfer(pipe.handle,true,&wire[0],(DWORD)wire.size(),done,deadline,target)||done!=wire.size())return false;
 response.clear();while(CliBudget(vi_cli_deadline::Remaining(deadline,GetTickCount64(),MAXDWORD))){
  char block[4096];done=0;if(!CliPipeTransfer(pipe.handle,false,block,sizeof(block),done,deadline,target)||!done)return false;
  response.append(block,done);if(response.size()>maxBytes)return false;const auto newline=response.find('\n');if(newline!=std::string::npos){response.resize(newline);return true;}
 }
 return false;
}
