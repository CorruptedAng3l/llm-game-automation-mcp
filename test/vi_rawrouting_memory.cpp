#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdio>
static DWORD threadId=10,g_pid=42;static ULONGLONG tick=1000;static unsigned propertyReads=0;static HWND a=(HWND)(ULONG_PTR)1,b=(HWND)(ULONG_PTR)2,g_hwnd=a;
struct Window{DWORD thread=0;bool alive=true;ULONG_PTR property=0;};static std::map<HWND,Window> windows;
struct WndProcBinding{HWND window=nullptr;bool live=true;};static std::map<HWND,std::shared_ptr<WndProcBinding>> bindings;
static std::map<HANDLE,DWORD> handles;static std::map<DWORD,bool> aliveThreads;static unsigned nextHandle=100,closed=0;
static DWORD WINAPI Thread(){return threadId;}static DWORD WINAPI ThreadOf(HWND window,LPDWORD pid){auto i=windows.find(window);if(i==windows.end()||!i->second.alive){if(pid)*pid=0;return 0;}if(pid)*pid=g_pid;return i->second.thread;}
static HANDLE WINAPI Property(HWND window,LPCWSTR){++propertyReads;return (HANDLE)windows[window].property;}static BOOL WINAPI SetProperty(HWND window,LPCWSTR,HANDLE value){windows[window].property=(ULONG_PTR)value;return TRUE;}
static HANDLE WINAPI Process(){return (HANDLE)(ULONG_PTR)1;}static HANDLE WINAPI CurrentThread(){return (HANDLE)(ULONG_PTR)threadId;}
static BOOL WINAPI Duplicate(HANDLE,HANDLE,HANDLE,HANDLE* out,DWORD,BOOL,DWORD){*out=(HANDLE)(ULONG_PTR)++nextHandle;handles[*out]=threadId;return TRUE;}
static BOOL WINAPI Close(HANDLE h){handles.erase(h);++closed;return TRUE;}static DWORD WINAPI Wait(HANDLE h,DWORD){auto i=handles.find(h);return i!=handles.end()&&aliveThreads[i->second]?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static DWORD WINAPI Tick(){return (DWORD)tick;}static ULONGLONG HousekeepingNow(){return tick;}
static bool RawOwnTarget(HWND h){return windows.count(h)&&windows[h].alive;}
static std::shared_ptr<WndProcBinding> WndBindingFind(HWND h){auto i=bindings.find(h);return i==bindings.end()?nullptr:i->second;}
static bool WndBindingLive(const std::shared_ptr<WndProcBinding>& p){return p&&p->live;}
static bool WndBindingRawAllowed(HWND h){auto p=WndBindingFind(h);return RawOwnTarget(h)&&(!p||p->live);}
static std::atomic<unsigned> g_rawRingPos{0};static std::atomic<int> g_rawLastBackend{0};static constexpr ULONG_PTR kMagicBase=0x12340000;
struct Config{int rawmode=0;}g_cfg;
struct Counters{unsigned raw_target_rejected=0,raw_queued=0,raw_queue_dropped=0,raw_posted=0,raw_post_failed=0,rib=0,rib_injected=0;}C;
static unsigned posted=0;static HWND postedTo=nullptr;static BOOL SelfRawPostRecord(HWND h,const RAWINPUT&,LPARAM){++posted;postedTo=h;return TRUE;}
enum{ReaderRawBuffer};static bool ReaderMask(int){return true;}static constexpr UINT kReaderScratchLimit=1024*1024;
static bool ReaderBufferWritable(const void* p,size_t bytes){return p||!bytes;}static bool ReaderBufferLoad(const void* p,void* q,size_t n){if(!p)return false;memcpy(q,p,n);return true;}static bool ReaderBufferStore(void* p,const void* q,size_t n){if(!p)return false;memcpy(p,q,n);return true;}
struct Channel{unsigned reads=0;void record(int,bool){++reads;}};struct Trace{Channel raw_buffer;};static Trace g_keyTrace[256];
static UINT WINAPI NativeBuffer(PRAWINPUT,PUINT pcb,UINT header){if(!pcb||header!=sizeof(RAWINPUTHEADER)){SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;}*pcb=0;return 0;}
static auto o_GetRawInputBuffer=&NativeBuffer;
#define GetCurrentThreadId Thread
#define GetWindowThreadProcessId ThreadOf
#define GetPropW Property
#define SetPropW SetProperty
#define GetCurrentProcess Process
#define GetCurrentThread CurrentThread
#define DuplicateHandle Duplicate
#define CloseHandle Close
#define WaitForSingleObject Wait
#define GetTickCount Tick
#include "actual_raw_state.inl"
#include "actual_raw_routing.inl"
#include "actual_emit.inl"
#include "actual_reader.inl"
static unsigned passed=0,failed=0;static void Check(bool good,const char* what){printf("%s %s\n",good?"PASS":"FAIL",what);good?++passed:++failed;}
static RAWINPUT Key(){RAWINPUT r{};r.header.dwType=RIM_TYPEKEYBOARD;r.header.dwSize=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD);r.data.keyboard.VKey='W';return r;}
static UINT Read(){alignas(8) BYTE out[256]{};UINT bytes=sizeof(out);return h_GetRawInputBuffer((PRAWINPUT)out,&bytes,sizeof(RAWINPUTHEADER));}
static void Reset(){g_rawQ.clear();
#ifdef VI_RAW_THREAD_ROUTING
g_rawBufferReaders.clear();
#endif
 windows.clear();bindings.clear();windows[a]={10,true,0};windows[b]={20,true,0};bindings[a]=std::make_shared<WndProcBinding>();bindings[a]->window=a;bindings[b]=std::make_shared<WndProcBinding>();bindings[b]->window=b;g_hwnd=a;aliveThreads[10]=aliveThreads[20]=aliveThreads[30]=true;tick=1000;threadId=10;g_lastRIB=0;g_cfg.rawmode=0;posted=0;postedTo=nullptr;for(auto& key:g_keyTrace)key.raw_buffer.reads=0;}
int main(){
 Reset();threadId=20;Read();threadId=10;const auto key=Key();const int route=EmitRaw(key,a);Check(route==1&&posted==1&&postedTo==a,"unrelated buffer reader never diverts destination keyboard from posted route");
#ifdef _WIN64
 Reset();g_cfg.rawmode=2;propertyReads=0;unsigned accepted=0,rejected=0;
 for(unsigned i=0;i<1500;i++){const int result=EmitRaw(key,a);accepted+=result==2;rejected+=result==6;}
 Check(accepted==1024&&rejected==476,"1500-packet burst retains exactly capacity and refuses the remaining 476");
 printf("burst property reads: %u\n",propertyReads);
 Check(propertyReads<=6000,"burst lifetime validation uses bounded window queries per admission instead of rescanning every packet through USER32");
 unsigned drained=0;for(unsigned i=0;i<1025;i++){const auto count=Read();if(!count||count==(UINT)-1)break;drained+=count;}
 Check(drained==1024&&g_rawQ.empty(),"capacity burst drains all 1024 retained packets exactly once");
 Reset();g_cfg.rawmode=2;EmitRaw(key,a);tick+=9999;EmitRaw(key,a);tick+=2;
 Check(Read()==1,"expiry is checked per packet even when packets share a window generation");
 Reset();g_cfg.rawmode=2;EmitRaw(key,a);EmitRaw(key,b);windows[b].property=0;
 Check(Read()==1&&g_rawQ.empty(),"one revoked destination cannot borrow another destination's valid generation");
 Reset();threadId=10;Read();EmitRaw(key,a);Check(Read()==1,"positive control destination buffer reader receives one synthetic packet");
 Reset();threadId=10;Read();EmitRaw(key,a);threadId=20;Check(Read()==0&&g_keyTrace['W'].raw_buffer.reads==0,"foreign thread cannot drain or claim destination virtual key");threadId=10;Check(Read()==1&&g_keyTrace['W'].raw_buffer.reads==1,"registered destination thread receives its own buffered key");
 Reset();threadId=10;Read();EmitRaw(key,a);windows[a].property=0;Check(Read()==0,"window-generation revocation discards queued virtual key");
 Reset();threadId=10;Read();EmitRaw(key,a);bindings[a]->live=false;Check(Read()==0,"selected binding quarantine revokes queued key");
 Reset();threadId=10;Read();EmitRaw(key,a);g_hwnd=b;Check(Read()==0,"retarget cannot replay a queued key into a different selected scope");
 Reset();threadId=10;Read();EmitRaw(key,a);tick+=10001;Check(Read()==0,"virtual buffered key has finite lifetime");
 Reset();threadId=10;Read();aliveThreads[10]=false;Check(EmitRaw(key,a)==1,"dead retained reader witness cannot select buffer mode");
 Reset();g_cfg.rawmode=2;Check(EmitRaw(key,(HWND)(ULONG_PTR)999)==4,"explicit buffer mode rejects an unowned destination before queueing");
 Reset();g_cfg.rawmode=2;EmitRaw(key,a);EmitRaw(key,b);threadId=20;Check(Read()==1,"mixed queue yields only the current registered thread's packet");threadId=10;Check(Read()==1,"another thread's FIFO packet remains available");
#else
 Reset();g_cfg.rawmode=2;Check(EmitRaw(key,a)==1&&Read()==0,"x86 retains explicit unsupported buffered-path behavior without queue synthesis");
#endif
 printf("raw routing: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
