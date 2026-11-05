// Virtual buffer packets retain the registered destination's thread and lifetime.
// A reader on another thread cannot divert, consume or confer provenance on them.
const wchar_t kRawBufferWindowProperty[]=L"vinput.raw.buffer.window_generation.v1";
bool RawBufferCurrent(const RawBufferedPacket& packet,ULONGLONG now){
    DWORD owner=0;
    if(now>=packet.expires||!packet.window||!packet.thread||!packet.stamp||
       GetWindowThreadProcessId(packet.window,&owner)!=packet.thread||owner!=g_pid||
       (ULONG_PTR)GetPropW(packet.window,kRawBufferWindowProperty)!=packet.stamp||!WndBindingRawAllowed(packet.window))return false;
    return packet.selected?packet.selected->window==g_hwnd&&WndBindingLive(packet.selected)&&WndBindingFind(packet.selected->window)==packet.selected:!g_hwnd;
}
void RawBufferPruneLocked(ULONGLONG now){
    // A burst normally shares one destination/binding. Repeating its USER32
    // lifetime probes for every retained packet on every admission makes the
    // burst quadratic and can expire accepted packets before the reader runs.
    // Cache only within this pass; each admission/drain probes lifetimes anew.
    using Lifetime=std::tuple<ULONG_PTR,DWORD,ULONG_PTR,ULONG_PTR>;
    std::map<Lifetime,bool> current;
    for(auto it=g_rawQ.begin();it!=g_rawQ.end();){
        bool live=false;
        if(now<it->expires){
            const Lifetime key{(ULONG_PTR)it->window,it->thread,it->stamp,(ULONG_PTR)it->selected.get()};
            auto found=current.find(key);
            if(found==current.end())found=current.emplace(key,RawBufferCurrent(*it,now)).first;
            live=found->second;
        }
        if(!live){it=g_rawQ.erase(it);++g_rawBufferRevoked;}else ++it;
    }
}
bool RawBufferRecent(DWORD thread){
    if(!thread)return false;const auto now=HousekeepingNow();std::lock_guard<std::mutex> lock(g_rawQMx);auto it=g_rawBufferReaders.find(thread);
    if(it==g_rawBufferReaders.end())return false;
    if(now<it->second->seen||now-it->second->seen>=3000||WaitForSingleObject(it->second->thread,0)!=WAIT_TIMEOUT){g_rawBufferReaders.erase(it);return false;}
    return true;
}
void RawBufferReaderSeen(DWORD thread){
    const auto now=HousekeepingNow();std::lock_guard<std::mutex> lock(g_rawQMx);
    for(auto it=g_rawBufferReaders.begin();it!=g_rawBufferReaders.end();)if(now<it->second->seen||now-it->second->seen>=3000||(it->first==thread&&WaitForSingleObject(it->second->thread,0)!=WAIT_TIMEOUT))it=g_rawBufferReaders.erase(it);else ++it;
    auto found=g_rawBufferReaders.find(thread);if(found!=g_rawBufferReaders.end()){found->second->seen=now;return;}
    if(g_rawBufferReaders.size()>=64)return;
    HANDLE owned=nullptr;if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&owned,SYNCHRONIZE,FALSE,0))return;
    auto reader=std::make_shared<RawBufferReader>();reader->seen=now;reader->thread=owned;g_rawBufferReaders.emplace(thread,std::move(reader));
}
int RawBufferAdmit(const RAWINPUT& record,HWND window){
    if(record.header.dwSize<sizeof(RAWINPUTHEADER)||record.header.dwSize>sizeof(record))return 4;
    RawBufferedPacket packet;packet.record=record;packet.window=window;DWORD owner=0;packet.thread=GetWindowThreadProcessId(window,&owner);
    if(!packet.thread||owner!=g_pid||!WndBindingRawAllowed(window))return 4;
    const HWND selected=g_hwnd;packet.selected=WndBindingFind(selected);if(selected&&!packet.selected)return 4;
    packet.stamp=(ULONG_PTR)GetPropW(window,kRawBufferWindowProperty);
    if(!packet.stamp){packet.stamp=(ULONG_PTR)++g_rawRingPos;if(!packet.stamp)packet.stamp=(ULONG_PTR)++g_rawRingPos;if(!SetPropW(window,kRawBufferWindowProperty,(HANDLE)packet.stamp))return 4;}
    const auto now=HousekeepingNow();packet.expires=now+10000;
    if(!RawBufferCurrent(packet,now))return 4;
    std::lock_guard<std::mutex> lock(g_rawQMx);RawBufferPruneLocked(now);if(g_rawQ.size()>=1024)return 6;g_rawQ.push_back(std::move(packet));return 2;
}
