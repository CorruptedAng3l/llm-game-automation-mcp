// Pure memory: exact frozen production policy, writer/clip and ClaimGate bodies.
// No Windows headers or native input/window/DI libraries are linked.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#define WINAPI __stdcall
using BOOL=int;using UINT=unsigned;using DWORD=std::uint32_t;using LONG=std::int32_t;using LONGLONG=std::int64_t;using ULONGLONG=std::uint64_t;using ULONG_PTR=std::uintptr_t;using HWND=void*;
struct RECT {LONG left,top,right,bottom;};using LPRECT=RECT*;
struct POINT {LONG x,y;};struct INPUT {DWORD type;};using LPINPUT=INPUT*;
constexpr BOOL TRUE=1,FALSE=0;
constexpr DWORD ERROR_SUCCESS=0,ERROR_ACCESS_DENIED=5,ERROR_NOACCESS=998,ERROR_PROC_NOT_FOUND=127,ERROR_BUSY=170,ERROR_INVALID_PARAMETER=87,ERROR_NOT_READY=21,ERROR_RETRY=1237;
constexpr int GA_ROOT=2,SM_XVIRTUALSCREEN=76,SM_YVIRTUALSCREEN=77,SM_CXVIRTUALSCREEN=78,SM_CYVIRTUALSCREEN=79;
thread_local DWORD g_error=0;
void SetLastError(DWORD value){g_error=value;}DWORD GetLastError(){return g_error;}
std::atomic<ULONGLONG> g_now{1000};ULONGLONG HousekeepingNow(){return g_now.load();}ULONGLONG GetTickCount64(){return g_now.load();}
std::atomic<unsigned long long> g_jobClaimEpoch{1};std::atomic<ULONGLONG> g_jobClaimExpiry{0};
thread_local unsigned long long g_admittedJobEpoch=0;thread_local bool g_admittedJobClaimed=false;
#include "vi_claim_native_fence.inl"
#include "vi_cursor_guard.h"
std::recursive_mutex g_inputMx;std::atomic<unsigned> g_jobRetargetDepth{0},g_wndConfigCommitDepth{0};
HWND g_hwnd=reinterpret_cast<HWND>(1);DWORD g_pid=101;
std::atomic<unsigned long long> g_inputReadyGeneration{1},g_captureGeometryEpoch{1};
struct Counts {std::atomic<unsigned> scur{0},clip{0};} C;
vi_cursor_guard::Counts g_cursorCounts[vi_cursor_guard::ApiCount];
bool g_virtualClipEnabled=false;RECT g_virtualClipRect{};
vi_cursor_guard::Owner g_modelOwner;
template<class T>auto SetClaimField(T& value,bool claimed,int)->decltype(value.agentClaimLive=claimed,void()){value.agentClaimLive=claimed;}
template<class T>void SetClaimField(T&,bool,...){/* Old production Owner has no live-claim member. */}
vi_cursor_guard::Owner CursorOwnerSnapshot(){auto value=g_modelOwner;SetClaimField(value,AgentClaimLive(),0);return value;}
bool CursorNativeAllowed(){return vi_cursor_guard::NativeWriteAllowed(CursorOwnerSnapshot());}
bool CursorProtectionReady(){return true;}
bool NativeClipWindowProof(HWND window,DWORD& thread,ULONG_PTR& cookie){thread=17;cookie=31;return window==g_hwnd;}
bool IsWindowVisible(HWND window){return window==g_hwnd;}bool IsIconic(HWND){return false;}
HWND GetAncestor(HWND window,int){return window;}
DWORD GetWindowThreadProcessId(HWND window,DWORD* pid){if(pid)*pid=window==g_hwnd?g_pid:0;return window==g_hwnd?17u:0u;}
BOOL GetClientRect(HWND window,RECT* value){if(window!=g_hwnd||!value)return FALSE;*value={0,0,640,480};return TRUE;}
BOOL GetWindowRect(HWND window,RECT* value){if(window!=g_hwnd||!value)return FALSE;*value={100,100,740,580};return TRUE;}
BOOL ClientToScreen(HWND window,POINT* value){if(window!=g_hwnd||!value)return FALSE;value->x+=100;value->y+=100;return TRUE;}
BOOL EqualRect(const RECT* a,const RECT* b){return a&&b&&std::memcmp(a,b,sizeof(RECT))==0;}
int GetSystemMetrics(int value){return value==SM_XVIRTUALSCREEN||value==SM_YVIRTUALSCREEN?-30000:60000;}
bool CursorReadBytes(const void* input,void* output,std::size_t size){if(!input||!output)return false;std::memcpy(output,input,size);return true;}
bool CursorBufferStore(void* output,const void* input,std::size_t size){if(!output||!input)return false;std::memcpy(output,input,size);return true;}
std::atomic<int> g_virtualX{0},g_virtualY{0};
bool CursorVirtualSet(unsigned api,int x,int y){++g_cursorCounts[api].blocked;++g_cursorCounts[api].virtualized;g_virtualX=x;g_virtualY=y;return true;}

std::atomic<unsigned> g_positions{0},g_physicalPositions{0},g_sends{0},g_mouseEvents{0},g_sets{0},g_clears{0},g_queries{0};
std::atomic<bool> g_queryFail{false},g_clearFail{false};std::mutex g_lowerMx;
RECT g_privateClip{-30000,-30000,30000,30000};
std::function<void()> g_onPosition,g_onClip,g_onEpoch;
BOOL WINAPI LowerSet(int,int){++g_positions;if(g_onPosition)g_onPosition();return TRUE;}
BOOL WINAPI LowerPhysical(int,int){++g_physicalPositions;if(g_onPosition)g_onPosition();return TRUE;}
UINT WINAPI LowerSend(UINT count,LPINPUT,int){++g_sends;return count;}
void WINAPI LowerMouse(DWORD,DWORD,DWORD,DWORD,ULONG_PTR){++g_mouseEvents;}
BOOL WINAPI LowerClip(const RECT* rectangle){
 if(rectangle){++g_sets;{std::lock_guard<std::mutex> lock(g_lowerMx);g_privateClip=*rectangle;}if(g_onClip)g_onClip();return TRUE;}
 ++g_clears;if(g_clearFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 {std::lock_guard<std::mutex> lock(g_lowerMx);g_privateClip={-30000,-30000,30000,30000};}return TRUE;
}
BOOL WINAPI LowerGet(RECT* rectangle){++g_queries;if(g_queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}std::lock_guard<std::mutex> lock(g_lowerMx);*rectangle=g_privateClip;return TRUE;}
BOOL(WINAPI*o_SetCursorPos)(int,int)=LowerSet;BOOL(WINAPI*o_SetPhysicalCursorPos)(int,int)=LowerPhysical;
UINT(WINAPI*o_SendInput)(UINT,LPINPUT,int)=LowerSend;void(WINAPI*o_mouse_event)(DWORD,DWORD,DWORD,DWORD,ULONG_PTR)=LowerMouse;
BOOL(WINAPI*o_ClipCursor)(const RECT*)=LowerClip;BOOL(WINAPI*o_GetClipCursor)(RECT*)=LowerGet;
#include "vi_cursor_write_hooks.inl"

using JObj=std::map<std::string,std::string>;
std::string JStr(const JObj& j,const char* name){auto i=j.find(name);return i==j.end()?std::string():i->second;}
double JNum(const JObj& j,const char* name,double def){auto i=j.find(name);return i==j.end()?def:std::stod(i->second);}
bool JBool(const JObj& j,const char* name,bool def){auto i=j.find(name);return i==j.end()?def:i->second=="true";}
std::string ErrJ(const std::string& why){return "{\"ok\":false,\"error\":\""+why+"\"}";}
void Log(const char*,...){}
std::mutex g_claimMx;std::string g_claimOwner;ULONGLONG g_claimExpiry=0,g_claimSince=0;DWORD g_claimTtlMs=0;std::atomic<long> g_claimRefused{0};
bool ClaimActiveLocked(ULONGLONG now){return !g_claimOwner.empty()&&now<g_claimExpiry;}
std::function<void()> g_onOpen;
bool IsOpenCommand(const std::string& cmd){if(g_onOpen)g_onOpen();return cmd=="status"||cmd=="readiness";}
bool PadIsOff(std::string names){for(auto& ch:names)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));return names=="off";}
std::string ClaimRefusal(const std::string& why){return ErrJ(why);}
void JobsEpochChange(){++g_jobClaimEpoch;if(g_onEpoch)g_onEpoch();}
#include "claim_gate.inl"

unsigned g_pass=0,g_fail=0;
void Check(bool good,const char* label){std::printf("%s %s\n",good?"PASS":"FAIL",label);if(good)++g_pass;else ++g_fail;}
std::string Claim(const char* owner="agent",bool force=false,const char* ttl="60"){return ClaimGate("claim",{{"owner",owner},{"ttl",ttl},{"force_claim",force?"true":"false"}});}
void Reset(){
 g_onPosition={};g_onClip={};g_onEpoch={};g_onOpen={};g_queryFail=false;g_clearFail=false;g_now=1000;
 g_jobClaimExpiry=0;g_claimExpiry=0;g_claimSince=0;g_claimTtlMs=0;g_claimOwner.clear();g_nativeClip={};g_virtualClipEnabled=false;g_virtualClipRect={};
 g_positions=0;g_physicalPositions=0;g_sends=0;g_mouseEvents=0;g_sets=0;g_clears=0;g_queries=0;g_virtualX=0;g_virtualY=0;
 g_privateClip={-30000,-30000,30000,30000};g_modelOwner={};g_modelOwner.foregroundVerified=true;g_modelOwner.foregroundOwned=true;g_modelOwner.windowReady=true;g_modelOwner.lease=true;g_modelOwner.foregroundRoot=reinterpret_cast<std::uintptr_t>(g_hwnd);
 const auto fence=g_claimNativeFence.Snapshot();Check(fence.active==0&&!fence.transition,"previous case leaves no native reservation/transition");
}
void CleanupTicks(){for(int n=0;n<4;++n){g_now.fetch_add(100);NativeClipTick();}}
struct Barrier {
 std::mutex mutex;std::condition_variable cv;bool entered=false,released=false,timedOut=false;
 void Run(){std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_all();if(!cv.wait_for(lock,std::chrono::seconds(2),[&]{return released;}))timedOut=true;}
 bool Entered(){std::unique_lock<std::mutex> lock(mutex);return cv.wait_for(lock,std::chrono::seconds(2),[&]{return entered;});}
 void Release(){std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all();}
};

int main(){
 std::puts("PURE_MEMORY_NO_USER32_DINPUT_IMPORTS; exact production bodies; all original pointers terminate in private callbacks");
 const RECT contained{110,120,300,330};INPUT input{};
 Reset();Check(h_SetCursorPos(120,140)&&h_SetPhysicalCursorPos(130,150)&&g_positions==1&&g_physicalPositions==1,"unclaimed leased native setters unchanged");
 Check(h_SendInput(1,&input,static_cast<int>(sizeof(input)))==1&&g_sends==1,"unclaimed native SendInput unchanged");h_mouse_event(1,1,2,0,0);Check(g_mouseEvents==1,"unclaimed native mouse_event unchanged");
 Check(h_ClipCursor(&contained)&&g_sets==1&&g_nativeClip.pending,"unclaimed contained native clip unchanged");
 Check(h_ClipCursor(nullptr)&&g_clears==1&&!g_nativeClip.pending,"unclaimed tracked NULL cleanup unchanged");

 Reset();Check(Claim().empty(),"ordinary claim accepted without native operation");g_modelOwner.humanFresh=true;
 Check(h_SetCursorPos(201,202)&&g_positions==0&&g_virtualX==201&&g_virtualY==202,"claimed SetCursorPos publishes virtual position only");
 Check(h_SetPhysicalCursorPos(203,204)&&g_physicalPositions==0&&g_virtualX==203,"claimed physical setter publishes virtual position only");
 Check(h_SendInput(1,&input,static_cast<int>(sizeof(input)))==0&&g_sends==0&&GetLastError()==ERROR_ACCESS_DENIED,"claimed SendInput refused before lower boundary");
 h_mouse_event(1,2,3,0,0);Check(g_mouseEvents==0&&GetLastError()==ERROR_ACCESS_DENIED,"claimed mouse_event refused before lower boundary");
 Check(h_ClipCursor(&contained)&&g_sets==0&&g_virtualClipEnabled&&EqualRect(&g_virtualClipRect,&contained),"claimed clip remains requested shadow only");
 Check(h_ClipCursor(nullptr)&&g_clears==0&&!g_virtualClipEnabled,"claimed unowned NULL makes no native clear");
 g_now=62000;Check(h_SetCursorPos(1,2)&&g_positions==1,"expired claim restores native permission");

 Reset();h_ClipCursor(&contained);Check(Claim().empty(),"claim can commit with tracked clip pending cleanup");CleanupTicks();
 Check(g_sets==1&&g_clears==1&&!g_nativeClip.pending,"claim transition conditionally releases tracked native clip");
 Reset();h_ClipCursor(&contained);{std::lock_guard<std::mutex> lock(g_lowerMx);g_privateClip={400,400,500,500};}Claim();CleanupTicks();
 Check(g_clears==0&&!g_nativeClip.pending&&g_nativeClip.superseded==1,"claim cleanup never clears a replaced rectangle");
 Reset();h_ClipCursor(&contained);g_queryFail=true;Claim();CleanupTicks();
 Check(g_queries==3&&g_clears==0&&g_nativeClip.pending&&g_nativeClip.cleanupExhausted,"claim query failures remain explicit bounded pending state");
 Reset();h_ClipCursor(&contained);g_clearFail=true;Claim();CleanupTicks();
 Check(g_clears==3&&g_nativeClip.pending&&g_nativeClip.cleanupExhausted,"claim clear failures remain explicit bounded pending state");

 Reset();std::string nested;g_onPosition=[&]{nested=Claim();};h_SetCursorPos(1,2);g_onPosition={};
 Check(!nested.empty()&&g_claimOwner.empty()&&g_jobClaimExpiry==0,"claim inside lower setter callback refuses immediately without mutation");
 Check(Claim().empty(),"later explicit claim succeeds after lower setter returns");

 Reset();Barrier positionBarrier;g_onPosition=[&]{positionBarrier.Run();};
 auto writer=std::async(std::launch::async,[]{return h_SetCursorPos(1,2);});const bool entered=positionBarrier.Entered();
 auto concurrentClaim=std::async(std::launch::async,[]{return Claim();});const bool fast=concurrentClaim.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
 std::string claimResult;if(fast)claimResult=concurrentClaim.get();
 Check(entered&&fast&&!claimResult.empty()&&g_jobClaimExpiry==0,"inflight setter claim refusal is bounded and leaves no live claim");
 positionBarrier.Release();writer.get();if(!fast)concurrentClaim.get();g_onPosition={};Check(!positionBarrier.timedOut,"lower setter barrier exits finitely");
 Check(Claim().empty(),"retry after inflight setter completes can publish claim");

 Reset();Barrier clipBarrier;g_onClip=[&]{clipBarrier.Run();};auto clipping=std::async(std::launch::async,[&]{return h_ClipCursor(&contained);});const bool clipEntered=clipBarrier.Entered();
 auto clipClaim=std::async(std::launch::async,[]{return Claim();});const bool clipFast=clipClaim.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
 std::string clipResult;if(clipFast)clipResult=clipClaim.get();
 Check(clipEntered&&clipFast&&!clipResult.empty()&&g_jobClaimExpiry==0,"inflight native clip claim refuses without holding a callback lock");
 clipBarrier.Release();clipping.get();if(!clipFast)clipClaim.get();g_onClip={};Check(!clipBarrier.timedOut,"lower clip barrier exits finitely");
 Claim();CleanupTicks();Check(!g_nativeClip.pending&&g_clears==1,"clip claim retry drains exact completed receipt");

 Reset();g_onEpoch=[]{h_SetCursorPos(1,2);};Check(Claim().empty(),"claim commit completes after modeled legacy epoch callback");g_onEpoch={};
 Check(g_positions==0,"claim transition denies new native setter before expiry publication");
 const auto firstExpiry=g_jobClaimExpiry.load();Claim();Check(g_jobClaimExpiry>=firstExpiry,"same-owner renewal preserves active claim");
 Check(Claim("newer",true).empty(),"force owner transition keeps existing ownership semantics");
 Check(!ClaimGate("unclaim",{{"owner","agent"}}).empty()&&g_claimOwner=="newer","old owner cannot unclaim newer epoch");
 const auto beforeNewer=g_positions.load();h_SetCursorPos(1,2);Check(g_positions==beforeNewer,"old-owner refusal cannot unprotect a newer live claim");
 Check(ClaimGate("unclaim",{{"owner","newer"}}).empty(),"current owner unclaim remains available");h_SetCursorPos(1,2);Check(g_positions==beforeNewer+1,"unclaim restores exact native path");

 Reset();g_onEpoch=[]{g_now.fetch_add(2000);};Claim("agent",false,"1");g_onEpoch={};
 Check(g_jobClaimExpiry>HousekeepingNow(),"claim TTL starts at commit after a slow modeled epoch callback");
 h_SetCursorPos(1,2);Check(g_positions==0,"slow claim commit does not ACK an already-expired protection window");

 // Exact ClaimGate samples active=true, then the modeled open-command query
 // crosses expiry and admits an original setter before implicit renewal.
 for(const char* command:{"key","release","up","mouseup","job-cancel","pad"}){
  Reset();g_claimOwner="agent";g_claimTtlMs=60000;g_claimExpiry=1100;g_jobClaimExpiry=1100;g_now=1099;
  Barrier renewalBarrier;std::future<BOOL> renewingWriter;bool renewalEntered=false;
  g_onPosition=[&]{renewalBarrier.Run();};
  g_onOpen=[&]{g_now=1200;renewingWriter=std::async(std::launch::async,[]{return h_SetCursorPos(1,2);});renewalEntered=renewalBarrier.Entered();};
  const auto skipped=g_claimCleanupRenewalSkipped.load();const std::string outcome=ClaimGate(command,{{"owner","agent"},{"buttons","OFF"}});g_onOpen={};
  const bool cleanup=std::string(command)!="key";
  Check(renewalEntered&&(cleanup?outcome.empty():!outcome.empty())&&g_jobClaimExpiry==1100&&g_claimExpiry==1100,
    cleanup?"cleanup remains available without renewing over an in-flight native call":"implicit matching-owner renewal refuses to revive expired claim over native call");
  if(cleanup)Check(g_claimCleanupRenewalSkipped==skipped+1,"skipped cleanup renewal has an explicit counter");
  renewalBarrier.Release();if(renewingWriter.valid())renewingWriter.get();g_onPosition={};
  Check(!renewalBarrier.timedOut,"implicit-renewal lower barrier exits finitely");
 }
 Reset();g_claimOwner="agent";g_claimTtlMs=60000;g_claimExpiry=1100;g_jobClaimExpiry=1100;g_now=1200;
 Barrier unclaimBarrier;g_onPosition=[&]{unclaimBarrier.Run();};auto unclaimWriter=std::async(std::launch::async,[]{return h_SetCursorPos(1,2);});const bool unclaimEntered=unclaimBarrier.Entered();
 Check(unclaimEntered&&ClaimGate("unclaim",{{"owner","agent"}}).empty()&&g_jobClaimExpiry==0,"unclaim remains available with an admitted native operation");
 unclaimBarrier.Release();unclaimWriter.get();g_onPosition={};Check(!unclaimBarrier.timedOut,"unclaim lower barrier exits finitely");

 Reset();bool readerFinished=false;std::atomic<bool> readerDone{false};std::thread reader;
 g_onClip=[&]{reader=std::thread([&]{RECT value{};h_GetClipCursor(&value);readerDone=true;});const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);while(!readerDone&&std::chrono::steady_clock::now()<end)std::this_thread::yield();readerFinished=readerDone;};
 h_ClipCursor(&contained);g_onClip={};if(reader.joinable())reader.join();Check(readerFinished,"lower native clip can complete cross-thread public reader before return");
 std::printf("RESULT %u PASS %u FAIL\n",g_pass,g_fail);return g_fail?1:0;
}
