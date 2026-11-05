// Exact production discovery/lifetime/format/readers with finite COM endpoints.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dinput8.lib") // SDK data-format constants; no DirectInput function is imported.
using PFN_DIGDS=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,LPVOID);using PFN_DIGDD=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
#include <vi_di_format_state.h>
static std::mutex g_diMx;static std::recursive_mutex g_inputMx;extern std::mutex g_diDiscoveryMx;
struct DiQueuedEvent{DWORD first,second;unsigned long long generation=0;DWORD timestamp=0,sequence=0;};static std::deque<DiQueuedEvent> g_diKbdQ,g_diMouseQ;
struct Channel{unsigned reads=0,downs=0;void record(int value){++reads;if(value&0x80)++downs;}};struct Trace{Channel di_state,di_buffer;};static Trace g_keyTrace[256];static std::atomic<BYTE> g_vk[256]{};
struct Counters{unsigned di_kbd=0,di_mouse=0,di_fake=0,di_data_kbd=0,di_data_mouse=0;}C;
static bool fake=true,mask=true,callbacksUnlocked=true;enum{ReaderDiState,ReaderDiBuffer};static bool Faking(){return fake;}static bool ReaderMask(int){return mask;}static constexpr size_t kReaderScratchLimit=1024*1024;
static bool ReaderBufferWritable(const void* p,size_t n){return (p||!n)&&(std::uintptr_t)p>16;}
static const void* rejectedStore=nullptr;
static bool ReaderBufferLoad(const void* p,void* out,size_t n){if(!p||(std::uintptr_t)p<=16)return false;memcpy(out,p,n);return true;}static bool ReaderBufferStore(void* p,const void* in,size_t n){if(p==rejectedStore||!ReaderBufferWritable(p,n))return false;memcpy(p,in,n);return true;}
static unsigned g_padApiLastDiType=0,g_readerDiUnknownDenied=0;static void PadApiRecordDiRead(unsigned,bool){}
static std::atomic<LONG> g_diDX{0},g_diDY{0},g_diDZ{0};static LONG ReadDiDelta(std::atomic<LONG>& value,LONG native){return native+value.exchange(0);}
static std::atomic<unsigned> g_diMouseMovesSincePoll{0};static std::atomic<ULONGLONG> g_diMouseBufferPollAt{0};static const int kDiMouseVk[5]={1,2,4,5,6};
struct MouseObservation{std::atomic<unsigned long long> press{0},release{0},state_poll_at{0};};static MouseObservation g_diMouseObservation[5];static void ObserveDiMouseDown(int,unsigned long long,bool){}static void ObserveDiMouseUp(int,unsigned long long,bool){}
static BYTE scanMap[256]{};static const BYTE* DiScanMap(){return scanMap;}static UINT WINAPI ModelMap(UINT scan,UINT){return scan==DIK_W?'W':scan==DIK_A?'A':0;}
struct Object;struct Control{unsigned refs=0,bytes=256,stateCalls=0,bufferCalls=0,propertyCalls=0,formatCalls=0,actionCalls=0;int type=1;Object* identity=nullptr;bool property=true,metadata=true;std::map<DWORD,DWORD> mouseObjects;};struct Object{void** table;Control* control;};
static void* ownerTable[40]{};static void* deviceTable[40]{};static void* aliasTable[40]{};static void* unknownTable[3]{};static Control ownerControl,deviceControl,oldControl,foreignControl,mouseControl;static Object owner{ownerTable,&ownerControl},device{deviceTable,&deviceControl},alias{aliasTable,&deviceControl},unknown{unknownTable,&deviceControl},oldDevice{deviceTable,&oldControl},foreignDevice{deviceTable,&foreignControl},mouseDevice{deviceTable,&mouseControl};static Object* nextDevice=&device;
static bool failFormat=false,wrongIdentity=false,stateMutation=false,propertyMutation=false,propertyDrain=false;static LPCDIDATAFORMAT mutationFormat=nullptr;
static unsigned nativeRights=0;static HRESULT nativeState=DIERR_NOTACQUIRED,nativeBuffer=DIERR_NOTACQUIRED;
enum{MH_OK,MH_ERROR_ENABLED,MH_ERROR_ALREADY_CREATED};struct Hook{void* detour=nullptr;bool enabled=false;};static std::map<void*,Hook> hooks;
static int VIMH_CreateHook(void* target,void* detour,void** original){if(hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=target;hooks[target]={detour,false};return MH_OK;}static int VIMH_EnableHook(void* target){hooks[target].enabled=true;return MH_OK;}
static bool CursorEntryReaches(void* target,void* detour){auto it=hooks.find(target);return it!=hooks.end()&&it->second.enabled&&it->second.detour==detour;}
template<class F>static F Entry(void* target){auto it=hooks.find(target);return (F)(it!=hooks.end()&&it->second.enabled?it->second.detour:target);}
static void CheckUnlocked(){std::unique_lock<std::mutex> a(g_diMx,std::try_to_lock),b(g_diFormatMx,std::try_to_lock),c(g_diDiscoveryMx,std::try_to_lock);callbacksUnlocked=callbacksUnlocked&&a.owns_lock()&&b.owns_lock()&&c.owns_lock();}
static HRESULT STDMETHODCALLTYPE Query(void* self,REFIID iid,void** out){CheckUnlocked();auto* object=(Object*)self;if(IsEqualIID(iid,IID_IUnknown))*out=object->control->identity;else if(IsEqualIID(iid,IID_IDirectInputDevice8W))*out=object->control->type==2?self:wrongIdentity?(void*)&foreignDevice:(void*)&alias;else *out=&unknown;++((Object*)*out)->control->refs;return S_OK;}
static ULONG STDMETHODCALLTYPE Release(void* self){CheckUnlocked();auto& refs=((Object*)self)->control->refs;return refs?--refs:0;}
static HRESULT STDMETHODCALLTYPE SetFormat(void* self,LPCDIDATAFORMAT format){CheckUnlocked();auto& control=*((Object*)self)->control;++control.formatCalls;if(failFormat||!format||(std::uintptr_t)format<=16)return DIERR_INVALIDPARAM;control.bytes=format->dwDataSize;if(control.type==2){control.mouseObjects.clear();DWORD button=0;for(DWORD n=0;n<format->dwNumObjs;++n){const auto& object=format->rgodf[n];DWORD source=0;if(object.pguid&&IsEqualGUID(*object.pguid,GUID_XAxis))source=DIMOFS_X;else if(object.pguid&&IsEqualGUID(*object.pguid,GUID_YAxis))source=DIMOFS_Y;else if(object.pguid&&IsEqualGUID(*object.pguid,GUID_ZAxis))source=DIMOFS_Z;else source=DIMOFS_BUTTON0+button++;control.mouseObjects[object.dwOfs]=source;}}return S_OK;}
static HRESULT STDMETHODCALLTYPE DeviceInfo(void* self,DIDEVICEINSTANCEW* out){CheckUnlocked();if(!((Object*)self)->control->metadata)return DIERR_NOTINITIALIZED;out->guidInstance=GUID_SysMouse;return DI_OK;}
static HRESULT STDMETHODCALLTYPE ObjectInfo(void* self,DIDEVICEOBJECTINSTANCEW* out,DWORD offset,DWORD how){CheckUnlocked();auto& control=*((Object*)self)->control;auto found=control.mouseObjects.find(offset);if(!control.metadata||how!=DIPH_BYOFFSET||found==control.mouseObjects.end())return DIERR_INVALIDPARAM;out->dwOfs=found->second;if(out->dwOfs<DIMOFS_BUTTON0){out->guidType=out->dwOfs==DIMOFS_X?GUID_XAxis:out->dwOfs==DIMOFS_Y?GUID_YAxis:GUID_ZAxis;out->dwType=DIDFT_RELAXIS|DIDFT_MAKEINSTANCE(out->dwOfs/4);}else{out->guidType=GUID_Button;out->dwType=DIDFT_PSHBUTTON|DIDFT_MAKEINSTANCE(out->dwOfs-DIMOFS_BUTTON0+3);}return DI_OK;}
static HRESULT STDMETHODCALLTYPE Action(void* self,void*,const void*,DWORD){CheckUnlocked();++((Object*)self)->control->actionCalls;return S_OK;}
static HRESULT STDMETHODCALLTYPE State(void* self,DWORD bytes,void* out){CheckUnlocked();auto& control=*((Object*)self)->control;++control.stateCalls;if(bytes!=control.bytes||!out)return DIERR_INVALIDPARAM;if(stateMutation){stateMutation=false;Entry<DiSetFormatFn>(deviceTable[11])(self,mutationFormat);}if(SUCCEEDED(nativeState))memset(out,0x55,bytes);return nativeState;}
static HRESULT STDMETHODCALLTYPE Buffer(void* self,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD count,DWORD){CheckUnlocked();++((Object*)self)->control->bufferCalls;if(!count)return DIERR_INVALIDPARAM;*count=0;return nativeBuffer;}
static HRESULT STDMETHODCALLTYPE Property(void* self,REFGUID,LPDIPROPHEADER header){CheckUnlocked();auto& control=*((Object*)self)->control;++control.propertyCalls;if(!control.property)return DIERR_UNSUPPORTED;((DIPROPPOINTER*)header)->uData=0x1000+header->dwObj;
 if(propertyMutation){propertyMutation=false;Entry<DiSetFormatFn>(deviceTable[11])(self,mutationFormat);}if(propertyDrain){propertyDrain=false;std::lock_guard<std::mutex> lock(g_diMx);g_diKbdQ.clear();}return S_OK;}
static HRESULT STDMETHODCALLTYPE Acquire(void*){++nativeRights;return S_OK;}static HRESULT STDMETHODCALLTYPE Coop(void*,HWND,DWORD){++nativeRights;return S_OK;}
static HRESULT STDMETHODCALLTYPE Create(void*,REFGUID,void** out,LPUNKNOWN){CheckUnlocked();*out=nextDevice;++nextDevice->control->refs;return S_OK;}
static HRESULT WINAPI Factory(HINSTANCE,DWORD,REFIID,void** out,LPUNKNOWN){CheckUnlocked();*out=&owner;++ownerControl.refs;return S_OK;}
static int DevType(void* self){const int type=((Object*)self)->control->type;g_padApiLastDiType=type==1?DI8DEVTYPE_KEYBOARD:type==2?DI8DEVTYPE_MOUSE:0;return type;}
static std::mutex g_claimDiEntryMx;struct Claim{void* target=nullptr;};static Claim g_claimDiAcquireEntries[16],g_claimDiCoopEntries[16];static constexpr unsigned kDiPollSlots=8;struct Poll{bool enabled=false;};static Poll g_diStateEntries[kDiPollSlots],g_diDataEntries[kDiPollSlots];
static bool DiPollEntryKnown(void*){return false;}static bool ClaimDiInstallEntry(void*,bool){return true;}static bool DiPollInstall(void*,bool){return true;}
static void ClaimDiReleaseBorrowed(void* p){if(p)Entry<DiReleaseFn>((*(void***)p)[2])(p);}
static HMODULE WINAPI Module(LPCWSTR){return (HMODULE)(ULONG_PTR)1;}static BOOL WINAPI Retain(DWORD,LPCWSTR,HMODULE* out){*out=Module(nullptr);return TRUE;}static BOOL WINAPI Free(HMODULE){return TRUE;}static FARPROC WINAPI Export(HMODULE,LPCSTR){return (FARPROC)&Factory;}
#define GetModuleHandleW Module
#define GetModuleHandleExW Retain
#define GetProcAddress Export
#define FreeLibrary Free
#define MapVirtualKeyW ModelMap
#include "actual_discovery.inl"
#include "actual_format_hooks.inl"
#include "actual_state_reader.inl"
#include "actual_buffer_reader.inl"
static unsigned passed=0,failed=0;static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
static HRESULT Set(const DIDATAFORMAT& format){return Entry<DiSetFormatFn>(deviceTable[11])(&device,&format);}
static HRESULT ReadState(BYTE* out,DWORD bytes){return DIGetDeviceStateImpl(&State,&device,bytes,out);}
static HRESULT ReadEvents(DIDEVICEOBJECTDATA* out,DWORD& count,DWORD flags=0){return DIGetDeviceDataImpl(&Buffer,&device,sizeof(DIDEVICEOBJECTDATA),out,&count,flags);}
static DIDATAFORMAT Format(std::vector<DIOBJECTDATAFORMAT>& objects,DWORD bytes){return {sizeof(DIDATAFORMAT),sizeof(DIOBJECTDATAFORMAT),DIDF_RELAXIS,bytes,(DWORD)objects.size(),objects.data()};}
static void Queue(unsigned scan,unsigned value,DWORD sequence){g_diKbdQ.push_back({scan,value,0,123,sequence});}
int main(){
 ownerControl.identity=&owner;deviceControl.identity=&device;oldControl.identity=&oldDevice;foreignControl.identity=&foreignDevice;mouseControl.identity=&mouseDevice;mouseControl.type=2;oldControl.refs=foreignControl.refs=1;
 for(void** table:{ownerTable,deviceTable,aliasTable}){table[0]=(void*)&Query;table[2]=(void*)&Release;table[3]=(void*)&Create;table[5]=(void*)&Property;table[7]=(void*)&Acquire;table[9]=(void*)&State;table[10]=(void*)&Buffer;table[11]=(void*)&SetFormat;table[13]=(void*)&Coop;table[14]=(void*)&ObjectInfo;table[15]=(void*)&DeviceInfo;table[30]=(void*)&Action;}unknownTable[0]=(void*)&Query;unknownTable[2]=(void*)&Release;
 scanMap['W']=DIK_W;scanMap['A']=DIK_A;g_vk['W']=0x80;TryDi8Factory();void* actualOwner=nullptr;using FactoryFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);Entry<FactoryFn>((void*)&Factory)(nullptr,DIRECTINPUT_VERSION,IID_IDirectInput8A,&actualOwner,nullptr);void* actualDevice=nullptr;Entry<DiCreateFn>(ownerTable[3])(actualOwner,GUID_SysKeyboard,&actualDevice,nullptr);
 std::vector<DIOBJECTDATAFORMAT> fields{{&GUID_Key,5,DIDFT_BUTTON|DIDFT_MAKEINSTANCE(DIK_W),0},{&GUID_Key,1,DIDFT_BUTTON|DIDFT_MAKEINSTANCE(DIK_A),0}};auto compactFmt=Format(fields,8);Check(Set(compactFmt)==S_OK,"native custom keyboard format result preserved");
 BYTE data[256];memset(data,0xcc,sizeof(data));Check(ReadState(data,8)==DI_OK&&data[5]==0x80&&data[1]==0&&data[7]==0&&data[8]==0xcc,"small custom keyboard offsets receive only requested virtual fields");Check(g_keyTrace['W'].di_state.downs>0&&g_keyTrace['A'].di_state.downs==0,"custom state trace counts mapped virtual API values");
 auto permuted=Format(fields,256);Set(permuted);memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DI_OK&&data[5]==0x80&&data[DIK_W]==0,"256-byte explicit custom format never falls back to scan-index guessing");
 failFormat=true;Check(Set(compactFmt)==DIERR_INVALIDPARAM,"failed SetDataFormat preserves original error");failFormat=false;memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DI_OK&&data[5]==0x80,"failed setter preserves prior successful descriptor");Set(compactFmt);
 void* wide=nullptr;Entry<DiQueryFn>(deviceTable[0])(&device,IID_IDirectInputDevice8W,&wide);memset(data,0xcc,sizeof(data));Check(DIGetDeviceStateImpl(&State,wide,8,data)==DI_OK&&data[5]==0x80,"verified A/W alias shares exact descriptor lifetime");
 const IID other={0x90909090,0,0,{}};void* opaque=nullptr;Entry<DiQueryFn>(deviceTable[0])(&device,other,&opaque);Check(opaque==&unknown&&DiFormatFind(opaque)==DiFormatFind(&device),"unknown queried interface uses universal slots and verified controlling identity only");
 wrongIdentity=true;void* wrong=nullptr;const auto wrongResult=Entry<DiQueryFn>(deviceTable[0])(&device,IID_IDirectInputDevice8W,&wrong);Check(wrongResult==DIERR_NOTINITIALIZED&&wrong==nullptr,"mismatched controlling identity cannot inherit descriptor");wrongIdentity=false;
 g_diKbdQ.clear();Queue(DIK_W,0x80,1);DIDEVICEOBJECTDATA event{};DWORD count=1;Check(ReadEvents(&event,count)==DI_OK&&count==1&&event.dwOfs==5&&event.dwData==0x80&&event.uAppData==0x1005,"buffered custom offset and native per-object appdata are preserved");Check(g_keyTrace['W'].di_buffer.downs>0&&g_diKbdQ.empty(),"custom buffer trace follows returned scan and consumed token");
 Queue(DIK_A,0x80,2);Queue(DIK_W,0x80,3);std::vector<DIOBJECTDATAFORMAT> onlyFields{fields[0]};auto onlyW=Format(onlyFields,8);Set(onlyW);count=1;event={};ReadEvents(&event,count);Check(count==1&&event.dwOfs==5&&g_diKbdQ.size()==1&&g_diKbdQ.front().first==DIK_A,"unmapped buffered key stays available for another device");
 g_diKbdQ.clear();Queue(DIK_W,0x80,4);count=1;const auto beforePeek=g_keyTrace['W'].di_buffer.downs;ReadEvents(&event,count,DIGDD_PEEK);Check(count==1&&g_diKbdQ.size()==1&&g_keyTrace['W'].di_buffer.downs==beforePeek,"peek returns mapped event without consuming or claiming drain");
 deviceControl.property=false;count=1;event.dwOfs=999;Check(ReadEvents(&event,count)==DIERR_NOTINITIALIZED&&g_diKbdQ.size()==1&&event.dwOfs==999,"unavailable appdata refuses untouched output and retains event");deviceControl.property=true;
 propertyDrain=true;count=1;event.dwOfs=999;Check(ReadEvents(&event,count)==DI_OK&&count==0&&event.dwOfs==999,"competing drain during unlocked metadata callback never replays stale event");
 Queue(DIK_W,0x80,5);propertyMutation=true;mutationFormat=&permuted;count=1;event.dwOfs=999;Check(ReadEvents(&event,count)==DIERR_NOTINITIALIZED&&event.dwOfs==999&&!g_diKbdQ.empty(),"format revision during property callback refuses virtual append");Set(permuted);stateMutation=true;mutationFormat=&onlyW;memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DIERR_NOTINITIALIZED&&data[0]==0xcc&&data[5]==0xcc,"format revision during native state callback leaves caller output untouched");
 std::vector<DIOBJECTDATAFORMAT> axes{{&GUID_XAxis,0,DIDFT_AXIS|DIDFT_MAKEINSTANCE(0),0}};auto unsupported=Format(axes,256);Set(unsupported);memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DIERR_UNSUPPORTED&&data[0]==0xcc&&data[DIK_W]==0xcc,"unsupported custom axes never receive guessed standard keyboard bytes");
 mask=false;fake=false;nativeState=nativeBuffer=S_OK;const auto nativeStateTrace=g_keyTrace['W'].di_state.reads;memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==S_OK&&data[0]==0x55&&data[DIK_W]==0x55&&g_keyTrace['W'].di_state.reads==nativeStateTrace,"explicit opt-out unsupported state passes only native bytes without guessed virtual merge");
 g_diKbdQ.clear();Queue(DIK_W,0x80,6);const auto nativeBufferTrace=g_keyTrace['W'].di_buffer.reads;count=1;event.dwOfs=999;Check(ReadEvents(&event,count)==S_OK&&count==0&&event.dwOfs==999&&g_diKbdQ.size()==1&&g_keyTrace['W'].di_buffer.reads==nativeBufferTrace,"explicit opt-out unsupported buffer preserves native result and virtual queue without guessed offsets");
 Set(compactFmt);memset(data,0xcc,sizeof(data));Check(ReadState(data,8)==S_OK&&data[5]==0x80&&data[1]==0x55,"explicit opt-out proven compact layout still merges virtual key at verified offset");mask=true;fake=true;nativeState=nativeBuffer=DIERR_NOTACQUIRED;g_diKbdQ.clear();
 Set(permuted);Entry<DiActionMapFn>(deviceTable[30])(&device,nullptr,nullptr,0);memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DIERR_UNSUPPORTED&&data[5]==0xcc,"action map changes are explicitly unsupported without a guessed layout");
 oldControl.bytes=256;memset(data,0xcc,sizeof(data));Check(DIGetDeviceStateImpl(&State,&oldDevice,256,data)==DI_OK&&data[DIK_W]==0x80,"implicit pre-existing standard keyboard fallback remains available");
 nextDevice=&oldDevice;{DiDiscoveryProbeScope probeScope;void* cached=nullptr;Entry<DiCreateFn>(ownerTable[3])(actualOwner,GUID_SysKeyboard,&cached,nullptr);if(cached)Entry<DiReleaseFn>(deviceTable[2])(cached);}nextDevice=&device;Check(!DiFormatFind(&oldDevice),"internal probe returning cached pre-existing identity does not invent unknown format metadata");memset(data,0xcc,sizeof(data));Check(DIGetDeviceStateImpl(&State,&oldDevice,256,data)==DI_OK&&data[DIK_W]==0x80,"cached internal probe preserves standard implicit fallback");
 nextDevice=&mouseDevice;void* actualMouse=nullptr;Entry<DiCreateFn>(ownerTable[3])(actualOwner,GUID_SysMouse,&actualMouse,nullptr);nextDevice=&device;
 std::vector<DIOBJECTDATAFORMAT> mouseFields(c_dfDIMouse.rgodf,c_dfDIMouse.rgodf+c_dfDIMouse.dwNumObjs);auto mouseFormat=c_dfDIMouse;mouseFormat.rgodf=mouseFields.data();Entry<DiSetFormatFn>(deviceTable[11])(&mouseDevice,&mouseFormat);DIMOUSESTATE mouseState{};g_diDX=3;g_vk[VK_LBUTTON]=0x80;Check(DIGetDeviceStateImpl(&State,&mouseDevice,sizeof(mouseState),&mouseState)==DI_OK&&mouseState.lX==3&&mouseState.rgbButtons[0]==0x80,"explicit canonical mouse descriptor preserves existing relative mouse synthesis");
 const DWORD mouseOffsets[]={8,0,16,5,4,6,7};for(unsigned n=0;n<mouseFields.size();++n)mouseFields[n].dwOfs=mouseOffsets[n];mouseFormat.dwDataSize=24;
 auto setMouse=[&](const DIDATAFORMAT& format){return Entry<DiSetFormatFn>(deviceTable[11])(&mouseDevice,&format);};
 Check(setMouse(mouseFormat)==S_OK,"native custom relative mouse format result preserved");g_diDX=31;g_diDY=-17;g_diDZ=120;memset(data,0xcc,sizeof(data));LONG mouseX=0,mouseY=0,mouseZ=0;
 auto mouseResult=DIGetDeviceStateImpl(&State,&mouseDevice,24,data);memcpy(&mouseX,data+8,4);memcpy(&mouseY,data,4);memcpy(&mouseZ,data+16,4);
 Check(mouseResult==DI_OK&&mouseX==31&&mouseY==-17&&mouseZ==120&&data[5]==0x80&&data[4]==0&&data[24]==0xcc,"permuted custom mouse descriptor maps axes/button without guessed storage");
 mouseResult=DIGetDeviceStateImpl(&State,&mouseDevice,24,data);memcpy(&mouseX,data+8,4);Check(mouseResult==DI_OK&&mouseX==0,"custom relative mouse state consumes deltas once");
 g_diDX=41;g_diDY=-23;g_diDZ=120;const auto failedMouseReads=g_keyTrace[VK_LBUTTON].di_state.reads;memset(data,0xcc,sizeof(data));rejectedStore=data;
 Check(DIGetDeviceStateImpl(&State,&mouseDevice,24,data)==DIERR_INVALIDPARAM&&g_diDX==41&&g_diDY==-23&&g_diDZ==120&&data[5]==0xcc&&g_keyTrace[VK_LBUTTON].di_state.reads==failedMouseReads,"failed custom state copy preserves every pending delta and read receipt");rejectedStore=nullptr;
 stateMutation=true;mutationFormat=&mouseFormat;memset(data,0xcc,sizeof(data));
 Check(DIGetDeviceStateImpl(&State,&mouseDevice,24,data)==DIERR_NOTINITIALIZED&&g_diDX==41&&data[5]==0xcc&&g_keyTrace[VK_LBUTTON].di_state.reads==failedMouseReads,"format revision inside native mouse state callback refuses before delta consumption or receipt");
 mouseControl.metadata=false;setMouse(mouseFormat);memset(data,0xcc,sizeof(data));Check(DIGetDeviceStateImpl(&State,&mouseDevice,24,data)==DIERR_UNSUPPORTED&&g_diDX==41&&data[5]==0xcc,"missing native system-mouse metadata refuses custom mapping without inferred identities");mouseControl.metadata=true;setMouse(mouseFormat);
 auto mouseEvents=[&](DIDEVICEOBJECTDATA* output,DWORD& number,DWORD flags=0){return DIGetDeviceDataImpl(&Buffer,&mouseDevice,sizeof(DIDEVICEOBJECTDATA),output,&number,flags);};
 g_diMouseQ.clear();g_diMouseQ.push_back({DIMOFS_X,13,0,123,10});g_diMouseQ.push_back({DIMOFS_BUTTON0,0x80,1,124,11});count=1;event={};
 Check(mouseEvents(&event,count,DIGDD_PEEK)==DI_OK&&count==1&&event.dwOfs==8&&event.dwData==13&&event.uAppData==0x1008&&g_diMouseQ.size()==2,"custom mouse PEEK preserves exact offset appdata and queue");
 count=1;Check(mouseEvents(&event,count)==DI_OK&&count==1&&event.dwOfs==8&&g_diMouseQ.size()==1,"custom buffered mouse consumes exact first token");count=1;const auto mouseReads=g_keyTrace[VK_LBUTTON].di_buffer.downs;
 Check(mouseEvents(&event,count)==DI_OK&&count==1&&event.dwOfs==5&&event.dwData==0x80&&g_diMouseQ.empty()&&g_keyTrace[VK_LBUTTON].di_buffer.downs==mouseReads+1,"custom buffered mouse button receipt follows mapped returned token");
 g_diMouseQ.push_back({DIMOFS_X,7,0,123,12});mouseControl.property=false;count=1;event.dwOfs=999;Check(mouseEvents(&event,count)==DIERR_NOTINITIALIZED&&event.dwOfs==999&&g_diMouseQ.size()==1,"custom mouse unavailable appdata refuses untouched output and retains token");mouseControl.property=true;g_diMouseQ.clear();
 auto mouseBad=mouseFormat;mouseBad.dwFlags=DIDF_ABSAXIS;setMouse(mouseBad);memset(data,0xcc,sizeof(data));g_diDX=19;
 Check(DIGetDeviceStateImpl(&State,&mouseDevice,24,data)==DIERR_UNSUPPORTED&&data[8]==0xcc&&g_diDX==19,"absolute custom mouse state is refused without consuming relative deltas");
 setMouse(mouseFormat);const auto mouseProof=DiFormatProof(&mouseDevice);Entry<DiActionMapFn>(deviceTable[30])(&mouseDevice,nullptr,nullptr,0);Check(!DiFormatProofCurrent(&mouseDevice,mouseProof),"mouse action map invalidates exact descriptor proof");
 auto readMouse=[](const void* p,void* out,size_t n){return ReaderBufferLoad(p,out,n);};
 auto malformedMouse=mouseFields;malformedMouse[0].dwOfs=23;auto malformedMouseFormat=mouseFormat;malformedMouseFormat.rgodf=malformedMouse.data();Check(!vi_di_mouse_format::Copy(&malformedMouseFormat,readMouse).parsed,"four-byte mouse axis extent must fit custom state");
 malformedMouse=mouseFields;malformedMouse[3].dwOfs=8;Check(!vi_di_mouse_format::Copy(&malformedMouseFormat,readMouse).parsed,"mouse button cannot overlap axis storage");
 malformedMouse=mouseFields;malformedMouse[3].dwType=DIDFT_BUTTON|DIDFT_ANYINSTANCE;const auto unresolvedMouse=vi_di_mouse_format::Copy(&malformedMouseFormat,readMouse);Check(unresolvedMouse.parsed&&!unresolvedMouse.supported,"parsed ANYINSTANCE mouse buttons require native identity resolution before support");
 malformedMouse=mouseFields;malformedMouse[1].pguid=&GUID_XAxis;Check(!vi_di_mouse_format::Copy(&malformedMouseFormat,readMouse).parsed,"duplicate custom mouse semantic identity is refused");
 malformedMouseFormat=mouseFormat;malformedMouseFormat.dwNumObjs=33;Check(!vi_di_mouse_format::Copy(&malformedMouseFormat,readMouse).parsed,"custom mouse descriptor count is bounded before copying");
 g_vk[VK_LBUTTON]=0;Entry<DiReleaseFn>(deviceTable[2])(&mouseDevice);
 Check(callbacksUnlocked&&nativeRights==0,"all native COM callbacks stay unlocked and request zero device rights");
 const auto retiring=DiFormatFind(&device);Entry<DiReleaseFn>(deviceTable[2])(&device);Entry<DiReleaseFn>(aliasTable[2])(&alias);Entry<DiReleaseFn>(unknownTable[2])(&unknown);Check(retiring&&!DiFormatFind(&device)&&!DiFormatFind(&alias)&&!DiFormatFind(&unknown),"terminal Release invalidates every verified alias");
 memset(data,0xcc,sizeof(data));Check(ReadState(data,256)==DIERR_NOTINITIALIZED&&data[DIK_W]==0xcc,"retired observed pointer cannot re-enter implicit standard fallback");
 actualDevice=nullptr;Entry<DiCreateFn>(ownerTable[3])(actualOwner,GUID_SysKeyboard,&actualDevice,nullptr);Check(!DiFormatFind(&device)||!DiFormatFind(&device)->format,"recycled COM address cannot inherit retired custom format");
 auto read=[](const void* p,void* out,size_t n){return ReaderBufferLoad(p,out,n);};DIDATAFORMAT malformed=compactFmt;malformed.dwNumObjs=0xffffffff;Check(!vi_di_format::Copy(&malformed,read).supported,"descriptor count bound rejects oversized copy");malformed=compactFmt;malformed.rgodf=(DIOBJECTDATAFORMAT*)(std::numeric_limits<std::uintptr_t>::max)();Check(!vi_di_format::Copy(&malformed,read).supported,"descriptor address overflow rejected before pointer read");
 auto badFields=fields;badFields[0].pguid=(const GUID*)(ULONG_PTR)1;auto bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"unreadable GUID pointer cannot prove key mapping");badFields=fields;badFields[0].dwOfs=8;bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"one-byte key extent must fit declared output");
 Check(vi_di_format::Copy(&c_dfDIKeyboard,read).supported&&vi_di_format::Copy(&c_dfDIKeyboard,read).standard,"actual linked SDK keyboard descriptor including optional reserved slot is supported");
 badFields=fields;badFields[0].dwType=DIDFT_BUTTON|DIDFT_ANYINSTANCE;bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"ANYINSTANCE cannot invent a keyboard scan identity");badFields=fields;badFields[1].dwOfs=badFields[0].dwOfs;bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"different keys cannot share ambiguous output storage");badFields=fields;badFields[1].dwType=badFields[0].dwType;bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"one key cannot invent multiple buffered event offsets");
 malformed=compactFmt;malformed.dwObjSize+=4;Check(!vi_di_format::Copy(&malformed,read).supported,"unknown object stride is explicitly unsupported");malformed=compactFmt;malformed.dwDataSize=65537;Check(!vi_di_format::Copy(&malformed,read).supported,"custom keyboard state allocation is bounded");badFields=fields;badFields[0].dwType|=DIDFT_OUTPUT;bad=Format(badFields,8);Check(!vi_di_format::Copy(&bad,read).supported,"unrecognized keyboard output type flags are refused");
 printf("di format: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
