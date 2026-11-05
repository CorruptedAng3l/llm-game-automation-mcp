// All API/module/hook callbacks are local fabricated C++; no Windows headers,
// controller creation, device discovery or desktop APIs are called by this model.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#define WINAPI __stdcall
#define STDMETHODCALLTYPE __stdcall
using BYTE=unsigned char;using WORD=std::uint16_t;using SHORT=std::int16_t;using DWORD=std::uint32_t;using UINT=std::uint32_t;using PUINT=UINT*;using BOOL=int;using HRESULT=std::int32_t;using ULONGLONG=std::uint64_t;using HMODULE=void*;using HWND=void*;using HRAWINPUT=void*;using LPVOID=void*;
constexpr DWORD ERROR_SUCCESS=0,ERROR_BAD_ARGUMENTS=87,ERROR_DEVICE_NOT_CONNECTED=1167,ERROR_NOT_READY=21,ERROR_ACCESS_DENIED=5;
constexpr UINT RID_INPUT=0x10000003,RID_HEADER=0x10000005,RIM_TYPEKEYBOARD=1,RIM_TYPEHID=2,RIDEV_REMOVE=1,RIDEV_EXCLUDE=0x10,RI_KEY_BREAK=1;
constexpr HRESULT NativeFailure=-1;constexpr BYTE DI8DEVTYPE_KEYBOARD=0x13,DI8DEVTYPE_MOUSE=0x12;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define GET_DIDEVICE_TYPE(x) static_cast<BYTE>((x)&255)
#define MAKEINTRESOURCEA(x) reinterpret_cast<const char*>(static_cast<std::uintptr_t>(x))
#include "vi_pad_api_census.h"
struct XGP{WORD b;BYTE lt,rt;SHORT lx,ly,rx,ry;};struct XST{DWORD pkt;XGP gp;};using PFN_XGS=DWORD(WINAPI*)(DWORD,XST*);
PFN_XGS o_XGS[8]{};std::atomic<bool> g_padActive{false};std::mutex g_padStateMx;std::atomic<int> g_padSetMask{0};std::atomic<WORD>g_padButtons{0};std::atomic<int>g_padLT{0},g_padRT{0},g_padLX{0},g_padLY{0},g_padRX{0},g_padRY{0};std::atomic<DWORD>g_padPacket{7};
bool isolated=true,storeAllowed=true;ULONGLONG tick=100;ULONGLONG GetTickCount64(){return tick++;}bool Isolating(){return isolated;}
extern DWORD fakeError;bool rawHeaderReadable=true;bool ReaderBufferLoad(const void* in,void* out,size_t n){fakeError=9999;if(!rawHeaderReadable)return false;std::memcpy(out,in,n);return true;}
bool ReaderBufferStore(void* out,const void* in,size_t n){if(!storeAllowed||!out)return false;std::memcpy(out,in,n);return true;}
struct Counters{unsigned xinput=0,rid=0,rid_magic=0,raw_real_denied=0,reg_raw=0,reg_raw_ok=0,reg_raw_removed=0,reg_raw_failed=0,di_data_kbd=0,di_data_mouse=0;} C;
#include "actual_pad_state.inl"
#include "actual_xgs.inl"
unsigned passes=0,failures=0;void Check(bool ok,const char* text){std::printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++passes:++failures;}
DWORD fakeError=0;DWORD GetLastError(){return fakeError;}void SetLastError(DWORD e){fakeError=e;}
unsigned stateOriginalCalls=0;DWORD stateResult=0,lastUser=0;
DWORD WINAPI FakeState(DWORD user,XST* out){++stateOriginalCalls;lastUser=user;fakeError=444;if(out){out->pkt=91;out->gp={2,3,4,5,6,7,8};}return out?stateResult:ERROR_BAD_ARGUMENTS;}
struct XINPUT_CAPABILITIES{BYTE opaque[20]{};};struct JOYINFOEX{DWORD field[13]{};};using LPJOYINFOEX=JOYINFOEX*;using MMRESULT=std::uint32_t;constexpr MMRESULT MMSYSERR_NODRIVER=6;
unsigned capCalls=0,joyCalls=0;DWORD lastFlags=0,capResult=0;void* lastOutput=nullptr;MMRESULT joyResult=0;
DWORD WINAPI FakeCapabilities(DWORD user,DWORD flags,XINPUT_CAPABILITIES* output){++capCalls;lastUser=user;lastFlags=flags;lastOutput=output;fakeError=555;if(output)output->opaque[0]=0xA5;return output?capResult:ERROR_BAD_ARGUMENTS;}
DWORD WINAPI FakeCapabilitiesB(DWORD u,DWORD f,XINPUT_CAPABILITIES* p){lastFlags=f^1;return FakeCapabilities(u,f,p);}
MMRESULT WINAPI FakeJoy(UINT id,LPJOYINFOEX out){++joyCalls;lastUser=id;lastOutput=out;fakeError=666;if(out)out->field[3]=0x12345678;return out?joyResult:MMSYSERR_NODRIVER;}
const wchar_t* kXinputDlls[4]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll",L"xinputuap.dll"};int modules[5]{};
HMODULE GetModuleHandleW(const wchar_t* name){for(unsigned i=0;i<4;i++)if(std::wcscmp(name,kXinputDlls[i])==0)return &modules[i];return std::wcscmp(name,L"winmm.dll")==0?(void*)&modules[4]:nullptr;}
void* GetProcAddress(HMODULE module,const char* name){unsigned slot=0;for(;slot<5;slot++)if(module==&modules[slot])break;if(slot==5)return nullptr;if(slot<4){if(reinterpret_cast<std::uintptr_t>(name)==100||std::strcmp(name,"XInputGetState")==0)return (void*)&FakeState;if(std::strcmp(name,"XInputGetCapabilities")==0)return slot==3?(void*)&FakeState:slot==2?(void*)&FakeCapabilitiesB:(void*)&FakeCapabilities;}else if(std::strcmp(name,"joyGetPosEx")==0)return (void*)&FakeJoy;return nullptr;}
struct Hook{void* detour;bool enabled=false;};std::map<void*,Hook> hooks;unsigned hookCreates=0,hookEnables=0;
using MH_STATUS=int;constexpr MH_STATUS MH_OK=0,MH_ERROR_ALREADY_CREATED=1,MH_ERROR_ENABLED=2;
MH_STATUS VIMH_CreateHook(void* target,void* detour,void** original){++hookCreates;if(target==(void*)&FakeCapabilitiesB||hooks.count(target))return MH_ERROR_ALREADY_CREATED;*original=target;hooks[target]={detour,false};return MH_OK;}
MH_STATUS VIMH_EnableHook(void* target){++hookEnables;auto& entry=hooks[target];if(entry.enabled)return MH_ERROR_ENABLED;entry.enabled=true;return MH_OK;}
#include "actual_exports.inl"
DWORD CallCapabilities(unsigned slot,DWORD user,DWORD flags,XINPUT_CAPABILITIES* output){auto target=GetProcAddress(&modules[slot],"XInputGetCapabilities");auto i=hooks.find(target);auto function=i!=hooks.end()&&i->second.enabled?i->second.detour:target;return ((DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*))function)(user,flags,output);}
MMRESULT CallJoy(UINT id,LPJOYINFOEX output){auto i=hooks.find((void*)&FakeJoy);auto function=i!=hooks.end()&&i->second.enabled?i->second.detour:(void*)&FakeJoy;return ((MMRESULT(WINAPI*)(UINT,LPJOYINFOEX))function)(id,output);}
struct RAWINPUTHEADER{DWORD dwType=0,dwSize=0;void* hDevice=nullptr;std::uintptr_t wParam=0;};struct RAWKEYBOARD{WORD MakeCode=0,Flags=0,Reserved=0,VKey=0;UINT Message=0;DWORD ExtraInformation=0;};struct RAWINPUT{RAWINPUTHEADER header;struct{RAWKEYBOARD keyboard;}data;};
struct RAWINPUTDEVICE{WORD usUsagePage=0,usUsage=0;DWORD dwFlags=0;HWND hwndTarget=nullptr;};using PCRAWINPUTDEVICE=const RAWINPUTDEVICE*;
struct RawTrace{void record(int,bool){}};struct Trace{RawTrace raw_data;};Trace g_keyTrace[256];bool rawDenied=false,rawRevoked=false;unsigned rawOriginalCalls=0;UINT rawType=RIM_TYPEHID,rawResult=sizeof(RAWINPUTHEADER);
bool ReaderMask(int){return rawDenied;}constexpr int ReaderRawData=1,ReaderDiState=2,ReaderDiBuffer=3;
bool SelfRawReadRevoked(HRAWINPUT){return rawRevoked;}void WndBindingRecordInputRefusal(){}bool SelfRawReadInternal(HRAWINPUT,UINT,LPVOID,PUINT,UINT,UINT&){return false;}bool SelfRawTrustedRead(HRAWINPUT){return false;}
UINT WINAPI FakeRawData(HRAWINPUT,UINT,LPVOID data,PUINT,UINT){++rawOriginalCalls;fakeError=777;if(data&&rawResult!=(UINT)-1&&rawResult>=sizeof(RAWINPUTHEADER)){RAWINPUTHEADER header;header.dwType=rawType;header.dwSize=rawResult;std::memcpy(data,&header,sizeof(header));}return rawResult;}
auto o_GetRawInputData=&FakeRawData;
#include "actual_raw_data.inl"
bool registerResult=true;unsigned registerCalls=0;PCRAWINPUTDEVICE lastDevices=nullptr;UINT lastCount=0;
BOOL WINAPI FakeRegister(PCRAWINPUTDEVICE devices,UINT count,UINT){++registerCalls;lastDevices=devices;lastCount=count;fakeError=888;return registerResult;}
auto o_RegisterRawInputDevices=&FakeRegister;std::atomic<unsigned long long>g_rawLastRegistrationResult{0};void Log(const char*,...){fakeError=999;}
#include "actual_registration.inl"
struct DIDEVCAPS{DWORD dwSize=0,dwFlags=0,dwDevType=0,reserved[8]{};};using LPDIDEVCAPS=DIDEVCAPS*;using PFN_DICAPS=HRESULT(STDMETHODCALLTYPE*)(void*,LPDIDEVCAPS);
struct Device{void** table=nullptr;DWORD type=0x14;bool fail=false;unsigned caps=0;};
HRESULT STDMETHODCALLTYPE FakeCaps(void* self,LPDIDEVCAPS out){auto& d=*(Device*)self;++d.caps;if(d.fail)return NativeFailure;out->dwDevType=d.type;return 0;}
#include "actual_devtype.inl"
using PFN_DIGDS=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,LPVOID);struct DIDEVICEOBJECTDATA{DWORD fields[5]{};};using LPDIDEVICEOBJECTDATA=DIDEVICEOBJECTDATA*;using LPDWORD=DWORD*;using PFN_DIGDD=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
unsigned modelOutOfScope=0,diCalls=0;HRESULT STDMETHODCALLTYPE FakeDiState(void*,DWORD cb,LPVOID out){++diCalls;if(out&&cb)((BYTE*)out)[0]=0x66;return 0;}HRESULT STDMETHODCALLTYPE FakeDiBuffer(void*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD count,DWORD){++diCalls;if(count)*count=0;return 0;}
#include "actual_di_state_prefix.inl"
#include "actual_di_buffer_prefix.inl"
#include "actual_json.inl"
#include "vi_pad_api_census_json.inl"
int main(){
 auto empty=PadApiCensusJson();Check(empty.find("\"coverage\":\"unmeasured\"")!=std::string::npos&&empty.find("\"wgi_gamepad_current_reading\":{\"coverage\":\"unmeasured\",\"calls\":null")!=std::string::npos,"unobserved/unhooked APIs are null, never fake zero discovery");
 for(auto& fn:o_XGS)fn=&FakeState;
 XST state{};stateResult=0;Check(XGSCommon(0,0,&state)==0&&state.pkt==91&&stateOriginalCalls==1&&fakeError==444,"inactive standard path preserves one native call, output and error state");Check(g_padApiState[0][0].entries==1&&g_padApiState[0][0].ok==1,"standard user0 entries/returns recorded");
 g_padActive=true;g_padButtons=0x1000;g_padSetMask=4;g_padLX=123;const auto reads=stateOriginalCalls;Check(XGSCommon(4,0,&state)==0&&state.gp.b==0x1000&&state.gp.lx==123&&stateOriginalCalls==reads,"active isolated Ex path keeps exact virtual fast result without native read");Check(g_padApiState[4][0].entries==1&&g_padApiState[4][0].ok==1&&g_padReturn.extended==1,"Ex completed API counter separate from virtual publication fence");
 const auto returns=g_padReturn.count;storeAllowed=false;Check(XGSCommon(4,0,&state)==ERROR_BAD_ARGUMENTS&&g_padReturn.count==returns,"failed caller store never becomes a virtual return witness");Check(g_padApiState[4][0].error==1,"failed store records API error return only");storeAllowed=true;
 stateResult=ERROR_DEVICE_NOT_CONNECTED;Check(XGSCommon(0,2,&state)==stateResult&&lastUser==2,"other user retains native error and no virtual merge");Check(g_padApiState[0][2].entries==1&&g_padApiState[0][2].error==1,"other user error counts remain separate");Check(XGSCommon(0,0,nullptr)==ERROR_BAD_ARGUMENTS,"null caller output retains original error");
 TryHookPadApiCensus();Check(g_padApiExportState[0]==5&&g_padApiExportState[1]==5&&g_padApiExportState[4]==5,"optional flat observers explicitly disabled/unmeasured");Check(hookCreates==0&&hookEnables==0,"optional observers never mutate safe/preexisting/cross-role entries");
 const auto installed=hookCreates;TryHookPadApiCensus();Check(hookCreates==installed,"passive installer neither probes controller nor repeats known attempts");
 XINPUT_CAPABILITIES caps{};capResult=0;const auto capBefore=capCalls;Check(CallCapabilities(1,3,0xDEADBEEF,&caps)==0&&capCalls==capBefore+1&&lastUser==3&&lastFlags==0xDEADBEEF&&lastOutput==&caps&&caps.opaque[0]==0xA5&&fakeError==555,"capabilities alias forwards exact user/flags/buffer/result once");Check(g_padApiCapabilities[0][3].entries==0&&g_padApiCapabilities[1][3].entries==0,"native capabilities passthrough creates no claimed hook observation");capResult=87;Check(CallCapabilities(0,0,0,&caps)==87&&fakeError==555,"capabilities native error remains error");Check(g_padApiCapabilities[0][0].error==0,"unobserved native capability errors remain unmeasured");
 JOYINFOEX joy{};joyResult=0;Check(CallJoy(15,&joy)==0&&joyCalls==1&&lastUser==15&&joy.field[3]==0x12345678&&fakeError==666,"joyGetPosEx forwards exact fabricated caller and output");joyResult=165;Check(CallJoy(0,&joy)==165&&joyCalls==2&&fakeError==666,"joyGetPosEx native error unchanged");Check(g_padApiJoy.entries==0,"unhooked joy calls preserve behavior without invented observations");
 RAWINPUT packet{};UINT bytes=sizeof(packet);rawResult=sizeof(RAWINPUTHEADER);Check(h_GetRawInputData(nullptr,RID_HEADER,&packet,&bytes,sizeof(RAWINPUTHEADER))==rawResult&&fakeError==777,"raw header query original result/error unchanged");Check(g_padApiHidHeaderReturns==1&&g_padApiHidInputReturns==0,"HID header query is not input reading");Check(h_GetRawInputData(nullptr,RID_INPUT,&packet,&bytes,sizeof(RAWINPUTHEADER))==rawResult,"permitted fabricated HID input header returned");Check(g_padApiHidInputReturns==1,"permitted HID input return counted separately");const auto rawBefore=rawOriginalCalls;rawDenied=true;Check(h_GetRawInputData(nullptr,RID_INPUT,&packet,&bytes,sizeof(RAWINPUTHEADER))==(UINT)-1&&rawOriginalCalls==rawBefore&&fakeError==ERROR_ACCESS_DENIED,"isolation denied raw never triggers extra classification read");Check(g_padApiHidInputReturns==1,"denied raw type stays unknown, does not add a HID return");rawDenied=false;
 rawHeaderReadable=false;const auto hidBefore=g_padApiHidInputReturns.load();Check(h_GetRawInputData(nullptr,RID_INPUT,&packet,&bytes,sizeof(RAWINPUTHEADER))==rawResult&&fakeError==777&&g_padApiHidInputReturns==hidBefore,"optional unreadable header observation preserves native result/error and adds no HID type");Check(g_padApiHidHeaderUnknown==1,"failed optional header metadata is explicit unknown");rawHeaderReadable=true;
 RAWINPUTDEVICE registrations[4]={{1,5,0,nullptr},{1,4,0,nullptr},{1,5,RIDEV_REMOVE,nullptr},{1,4,RIDEV_EXCLUDE,nullptr}};Check(h_RegisterRawInputDevices(registrations,4,sizeof(RAWINPUTDEVICE))&&registerCalls==1&&lastDevices==registrations&&lastCount==4&&fakeError==888,"registration forwards once and restores native last error after logging");Check(g_padApiHidRegistration[1].registered==1&&g_padApiHidRegistration[0].registered==1&&g_padApiHidRegistration[1].removed==1&&g_padApiHidRegistration[0].excluded==1,"registration/removal/exclusion entries are distinct from data returns");registerResult=false;Check(!h_RegisterRawInputDevices(registrations,4,sizeof(RAWINPUTDEVICE))&&fakeError==888,"registration native failure preserved");Check(g_padApiHidRegistration[1].registered==1,"failed registration does not pretend successful subscription");
 void* table[4]{};table[3]=(void*)&FakeCaps;Device device{table};BYTE data[1]{};DWORD count=0;
 Check(DIGetDeviceStateImpl(&FakeDiState,&device,1,data)==0&&data[0]==0x66&&device.caps==1,"joystick classification reuses one existing caps query and native state");Check(g_padApiDiState[0]==1,"known joystick state entry observed without new probe");device.type=0x15;Check(DIGetDeviceDataImpl(&FakeDiBuffer,&device,sizeof(DIDEVICEOBJECTDATA),nullptr,&count,0)==0&&device.caps==2,"gamepad buffered entry preserves native result/query count");Check(g_padApiDiBuffer[1]==1,"known gamepad buffer entry is distinct from joystick");device.type=0x16;Check(DIGetDeviceStateImpl(&FakeDiState,&device,1,data)==0&&g_padApiDiState[2]==1,"other nonkeyboard/mouse type is not mislabeled joystick");
 g_padApiLastDiType=0x14;device.fail=true;Check(DevType(&device)==-1&&g_padApiLastDiType==~0u,"failed outer caps query cannot retain a stale numeric controller type");Check(modelOutOfScope==0,"exact DI proven-other prefix covers every modeled DI read");
 const auto observed=PadApiCensusJson();Check(observed.size()<65536&&observed.find("\"gameinput_current_reading\":{\"coverage\":\"unmeasured\",\"calls\":null")!=std::string::npos&&observed.find("\"hid_buffer_reads\":null")!=std::string::npos&&observed.find("disabled_unproved_future_abi_alias")!=std::string::npos,"unimplemented/disabled optional paths remain explicitly unmeasured");Check(observed.find("\"joystick\":{\"coverage\":\"partial_observed_on_intercepted_implementation\"")!=std::string::npos,"status names partial DI observation without exclusive backend inference");
 std::printf("{\"passed\":%u,\"failed\":%u,\"completed\":true,\"scope\":\"actual counter paths with finite fabricated callbacks; no controller/device/desktop API\"}\n",passes,failures);return failures?1:0;
}
