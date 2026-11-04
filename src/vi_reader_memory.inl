// Native readers validate their output pointers. A private native buffer must not
// accidentally remove that validation. Do not use WriteProcessMemory: it can make
// read-only image pages writable. Probe protection, then isolate a racing fault.
bool ReaderBufferWritable(const void* pointer,size_t bytes){
    if(!bytes)return true;
    ULONG_PTR at=(ULONG_PTR)pointer;
    if(!at||bytes>(size_t)(~(ULONG_PTR)0-at))return false;
    const ULONG_PTR end=at+bytes;
    while(at<end){
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery((const void*)at,&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const DWORD access=info.Protect&0xff;
        if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return false;
        const ULONG_PTR base=(ULONG_PTR)info.BaseAddress;
        if(info.RegionSize>(SIZE_T)(~(ULONG_PTR)0-base))return false;
        const ULONG_PTR next=base+info.RegionSize;
        if(next<=at)return false;at=next<end?next:end;
    }
    return true;
}
// Keep SEH in destructor-free helpers so callers can retain their ordinary locks
// and RAII buffers. The memory can be unmapped or protected after VirtualQuery.
bool ReaderBufferStore(void* output,const void* input,size_t bytes){
    if(!ReaderBufferWritable(output,bytes))return false;
    __try{if(bytes)memcpy(output,input,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReaderBufferLoad(const void* input,void* output,size_t bytes){
    __try{if(bytes)memcpy(output,input,bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
