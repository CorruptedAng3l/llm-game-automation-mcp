// Read-only foreground witness independent of an upstream fake-GetForegroundWindow chain.
std::mutex g_cursorOwnershipMx;vi_cursor_guard::Arbitration g_cursorArbitration;std::atomic<unsigned long long> g_cursorOwnershipClock{0};
HWND g_cursorHumanWindow=nullptr,g_cursorAgentWindow=nullptr;unsigned long long g_cursorHumanGeneration=0,g_cursorAgentGeneration=0;ULONGLONG g_cursorHumanAt=0;long g_cursorSeenDecision=-1,g_cursorRevokedDecision=-1;DWORD g_cursorHardwareTick=0;
thread_local unsigned long long g_cursorCommandToken=0;thread_local bool g_cursorAdmissionPending=false,g_cursorMoveAdmission=false;
struct CursorCommandScope {unsigned long long oldToken;bool oldPending,oldMove;explicit CursorCommandScope(const std::string& command):oldToken(g_cursorCommandToken),oldPending(g_cursorAdmissionPending),oldMove(g_cursorMoveAdmission){g_cursorMoveAdmission=command=="mouse"||command=="move"||command=="click"||command=="mousedown";g_cursorAdmissionPending=command=="mouse"||command=="move"||command=="click"||command=="mousedown"||command=="drag"||command=="placement-drag";g_cursorCommandToken=g_cursorAdmissionPending?++g_cursorOwnershipClock:0;}~CursorCommandScope(){g_cursorCommandToken=oldToken;g_cursorAdmissionPending=oldPending;g_cursorMoveAdmission=oldMove;}};
void CursorAdmitCommand(bool move=false){if(!g_cursorAdmissionPending||(move&&!g_cursorMoveAdmission))return;g_cursorAdmissionPending=false;std::lock_guard<std::mutex> lock(g_cursorOwnershipMx);if(g_cursorArbitration.AdmitAgent(g_cursorCommandToken,true)){g_cursorAgentWindow=g_hwnd;g_cursorAgentGeneration=g_inputReadyGeneration;}}
void CursorPublishHuman(HWND window,unsigned long long generation,bool newActivity,unsigned long long observed=0){
 const DWORD proof=(DWORD)(ULONG_PTR)GetPropW(window,vi_user_intent::kProof);std::lock_guard<std::mutex> lock(g_cursorOwnershipMx);if(window!=g_hwnd||generation!=g_inputReadyGeneration)return;
 const long decision=g_userIntent.Count();const bool same=g_cursorHumanWindow==window&&g_cursorHumanGeneration==generation;
 if(!newActivity){if(decision==g_cursorRevokedDecision)return;if(same&&g_cursorSeenDecision==decision)return;if(same&&g_cursorHardwareTick&&proof&&vi_user_intent::BeforeOrEqual(proof,g_cursorHardwareTick)){g_cursorSeenDecision=decision;return;}}
 const auto token=observed?observed:++g_cursorOwnershipClock;if(token<=g_cursorArbitration.takeover)return;
 g_cursorArbitration.Human(token);g_cursorSeenDecision=decision;g_cursorRevokedDecision=-1;g_cursorHumanWindow=window;g_cursorHumanGeneration=generation;g_cursorHumanAt=GetTickCount64();g_cursorHardwareTick=newActivity?GetTickCount():0;
}
void CursorValidateHumanScope(HWND window,unsigned long long generation,bool foregroundOwned,unsigned long long observed){std::lock_guard<std::mutex> lock(g_cursorOwnershipMx);if(window!=g_hwnd||generation!=g_inputReadyGeneration||g_cursorArbitration.takeover>observed)return;if(foregroundOwned&&g_cursorHumanWindow==window&&g_cursorHumanGeneration==generation)return;g_cursorHumanWindow=nullptr;g_cursorHumanGeneration=0;g_cursorHumanAt=0;g_cursorHardwareTick=0;g_cursorRevokedDecision=g_userIntent.Count();}
vi_cursor_guard::Counts g_cursorCounts[vi_cursor_guard::ApiCount];
std::atomic<bool> g_cursorInstallFinished{false};
void* g_cursorEntries[vi_cursor_guard::ApiCount]={};void* g_cursorDetours[vi_cursor_guard::ApiCount]={};
bool g_cursorInstalled[vi_cursor_guard::ApiCount]={};
bool g_virtualClipEnabled=false;RECT g_virtualClipRect{};
bool CursorReadBytes(const void* address,void* bytes,SIZE_T size){SIZE_T read=0;return address&&ReadProcessMemory(GetCurrentProcess(),address,bytes,size,&read)&&read==size;}
// Do not make a caller's read-only page writable as WriteProcessMemory may do.
bool CursorBufferWritable(const void* pointer,size_t bytes){
 if(!bytes)return true;ULONG_PTR at=(ULONG_PTR)pointer;if(!at||bytes>(size_t)(~(ULONG_PTR)0-at))return false;const ULONG_PTR end=at+bytes;
 while(at<end){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery((const void*)at,&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const DWORD access=info.Protect&0xff;if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return false;const ULONG_PTR base=(ULONG_PTR)info.BaseAddress;if(info.RegionSize>(SIZE_T)(~(ULONG_PTR)0-base))return false;const ULONG_PTR next=base+info.RegionSize;if(next<=at)return false;at=next<end?next:end;}return true;
}
// No RAII objects in this leaf: guard the protection-query/copy race with SEH.
bool CursorBufferStore(void* output,const void* input,size_t bytes){
 if(!CursorBufferWritable(output,bytes))return false;__try{if(bytes)memcpy(output,input,bytes);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CursorEntryReaches(void* entry,void* expected){
 auto current=(std::uintptr_t)entry;for(int count=0;count<6&&current;count++){
  if(current==(std::uintptr_t)expected)return true;unsigned char bytes[6]{};if(!CursorReadBytes((void*)current,bytes,sizeof(bytes)))return false;
  if(bytes[0]==0xE9){std::int32_t delta=0;memcpy(&delta,bytes+1,4);current=(std::uintptr_t)((std::intptr_t)current+5+delta);continue;}
  if(bytes[0]==0xEB){current=(std::uintptr_t)((std::intptr_t)current+2+(std::int8_t)bytes[1]);continue;}
  if(bytes[0]==0xFF&&bytes[1]==0x25){std::int32_t displacement=0;memcpy(&displacement,bytes+2,4);
#ifdef _WIN64
   const auto slot=(std::uintptr_t)((std::intptr_t)current+6+displacement);
#else
   const auto slot=(std::uintptr_t)(std::uint32_t)displacement;
#endif
   if(!CursorReadBytes((void*)slot,&current,sizeof(current)))return false;continue;}
  return false;
 }return false;
}
bool CursorProtectionReady(){
 if(!g_cursorInstallFinished.load())return false;
 for(unsigned i=0;i<vi_cursor_guard::ApiCount;i++)if(!g_cursorInstalled[i]||!CursorEntryReaches(g_cursorEntries[i],g_cursorDetours[i]))return false;
 return true;
}
vi_cursor_guard::Owner CursorOwnerSnapshot(){
 vi_cursor_guard::Owner owner;const HWND upstream=RealForeground();GUITHREADINFO gui{sizeof(gui)};
 const bool queried=GetGUIThreadInfo(0,&gui)!=FALSE;const HWND independent=queried?gui.hwndActive:nullptr;const HWND after=RealForeground();
 const HWND a=upstream?GetAncestor(upstream,GA_ROOT):nullptr,b=independent?GetAncestor(independent,GA_ROOT):nullptr,c=after?GetAncestor(after,GA_ROOT):nullptr;
 owner.foregroundVerified=upstream&&independent&&after&&a&&b&&c&&a==b&&b==c;
 owner.foregroundOwned=owner.foregroundVerified&&ForegroundBelongsToTarget(independent);
 HWND target=g_hwnd;DWORD pid=0;owner.windowReady=target&&IsWindow(target)&&GetWindowThreadProcessId(target,&pid)&&pid==g_pid;
 const auto now=GetTickCount64();{std::lock_guard<std::mutex> lock(g_cursorOwnershipMx);const bool same=owner.windowReady&&g_cursorHumanWindow==target&&g_cursorHumanGeneration==g_inputReadyGeneration;
 owner.humanHolding=vi_cursor_guard::ReaderAllowed(owner)&&same&&g_cursorArbitration.Holding();owner.humanFresh=owner.humanHolding&&g_cursorHumanAt&&now>=g_cursorHumanAt&&now-g_cursorHumanAt<=300;
 owner.agentPosition=owner.windowReady&&g_cursorAgentWindow==target&&g_cursorAgentGeneration==g_inputReadyGeneration&&g_cursorArbitration.Automation();owner.humanEpoch=g_cursorArbitration.takeover;owner.agentEpoch=g_cursorArbitration.automation;}
 owner.lease=now<g_fgLeaseUntil.load();owner.forcedVirtual=g_cfg.cursor==1;return owner;
}
bool PhysicalForegroundOwned(){return vi_cursor_guard::ReaderAllowed(CursorOwnerSnapshot());}
bool CursorNativeAllowed(){return vi_cursor_guard::NativeWriteAllowed(CursorOwnerSnapshot());}
void CursorObserveHardwareActivity(){
 // SDK IMO_HARDWARE includes UIAccess injection; it is not a cryptographic human proof.
 // Reject our posted/self-raw messages at call sites; native hardware-class
 // activity must additionally reach a freshly verified owned foreground.
 if(!PhysicalForegroundOwned())return;INPUT_MESSAGE_SOURCE source{};if(!GetCurrentInputMessageSource(&source)||source.originId!=IMO_HARDWARE)return;
 HWND window=g_hwnd;const auto generation=g_inputReadyGeneration.load();if(window)CursorPublishHuman(window,generation,true);
}
bool CursorVirtualSet(unsigned api,int x,int y){
 auto& counters=g_cursorCounts[api];++counters.blocked;std::lock_guard<std::recursive_mutex> lock(g_inputMx);
 HWND target=g_hwnd;DWORD owner=0;if(!target||!IsWindow(target)||!GetWindowThreadProcessId(target,&owner)||owner!=g_pid){++counters.failedVirtual;SetLastError(ERROR_NOT_READY);return false;}
 if(g_virtualClipEnabled){const int right=g_virtualClipRect.right>g_virtualClipRect.left?(int)g_virtualClipRect.right-1:(int)g_virtualClipRect.left,bottom=g_virtualClipRect.bottom>g_virtualClipRect.top?(int)g_virtualClipRect.bottom-1:(int)g_virtualClipRect.top;x=(std::max)((int)g_virtualClipRect.left,(std::min)(x,right));y=(std::max)((int)g_virtualClipRect.top,(std::min)(y,bottom));}
 POINT point{x,y};if(!ScreenToClient(target,&point)){++counters.failedVirtual;return false;}
 if(point.x!=g_vcurX||point.y!=g_vcurY){JobsOverrideCursor("game_cursor_drift");g_vcurX=point.x;g_vcurY=point.y;++g_pointerIssued;}
 ++counters.virtualized;return true;
}
bool CursorVirtualGet(LPPOINT output,int reader){
 if(!output){SetLastError(ERROR_INVALID_PARAMETER);return false;}HWND target=g_hwnd;DWORD pid=0;
 if(!target||!IsWindow(target)||!GetWindowThreadProcessId(target,&pid)||pid!=g_pid){POINT zero{};if(!CursorBufferStore(output,&zero,sizeof(zero))){SetLastError(ERROR_NOACCESS);return false;}SetLastError(ERROR_NOT_READY);return false;}
 POINT point{g_vcurX,g_vcurY};if(!ClientToScreen(target,&point))return false;if(!CursorBufferStore(output,&point,sizeof(point))){SetLastError(ERROR_NOACCESS);return false;}g_pointerSeen=g_pointerIssued.load();g_pointerReader=reader;return true;
}
