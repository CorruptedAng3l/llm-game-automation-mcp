// Actual DI8 keyboard factories, descriptors/properties and readers. No windows,
// enumeration or native acquisition/cooperative rights are delegated by the fixture.
// Separate lower MinHook namespace absorbs every public Acquire/coop boundary.
#include <vinput_core.cpp>
#include "vi_launch_terminal.h"
#undef MH_Initialize
#undef MH_CreateHook
#undef MH_EnableHook
extern "C" MH_STATUS WINAPI MH_Initialize();
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
#pragma comment(lib,"dinput8.lib")
static unsigned passed=0,failed=0;
static std::atomic<unsigned> lowerAcquire{0},lowerCoop{0};
static HRESULT STDMETHODCALLTYPE AbsorbAcquire(void*){++lowerAcquire;return DIERR_OTHERAPPHASPRIO;}
static HRESULT STDMETHODCALLTYPE AbsorbCoop(void*,HWND,DWORD){++lowerCoop;return DIERR_OTHERAPPHASPRIO;}
static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
using NativeCreate=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,void**,LPUNKNOWN);
using NativeSetFormat=HRESULT(STDMETHODCALLTYPE*)(void*,LPCDIDATAFORMAT);
using NativeSetProperty=HRESULT(STDMETHODCALLTYPE*)(void*,REFGUID,LPCDIPROPHEADER);
static HRESULT Format(void* device,const DIDATAFORMAT& format){return ((NativeSetFormat)(*(void***)device)[11])(device,&format);}
static HRESULT Property(void* device,REFGUID kind,LPCDIPROPHEADER value){return ((NativeSetProperty)(*(void***)device)[6])(device,kind,value);}
static void ReleaseObject(void* object){if(object)((ULONG(STDMETHODCALLTYPE*)(void*))(*(void***)object)[2])(object);}
static bool LowerEntry(void* entry,void* detour){void* unused=nullptr;return MH_CreateHook(entry,detour,&unused)==MH_OK&&unused&&MH_EnableHook(entry)==MH_OK&&vi_cursor_spy::Reaches(entry,detour);}
static void KeyboardChecks(void* device,unsigned profile){
    printf("PROFILE %u\n",profile);const auto state=(PFN_DIGDS)(*(void***)device)[9];const auto buffer=(PFN_DIGDD)(*(void***)device)[10];
    const BYTE scanW=DiScanMap()['W'],scanA=DiScanMap()['A'];Check(scanW&&scanA&&scanW!=scanA,"current layout provides distinct W/A scan identities");if(!scanW||!scanA||scanW==scanA)return;
    Check(SUCCEEDED(Format(device,c_dfDIKeyboard)),"native standard keyboard format accepted without acquisition");
    g_vk['W']=1;BYTE standard[256]{};const auto before=g_keyTrace['W'].di_state.down_reads.load();auto hr=state(device,sizeof(standard),standard);
    Check(SUCCEEDED(hr)&&(standard[scanW]&0x80)&&!(standard[scanA]&0x80)&&g_keyTrace['W'].di_state.down_reads>before,"actual standard GetDeviceState returns virtual W and trace");
    const DWORD offsetW=scanW==5?6:5;
    DIOBJECTDATAFORMAT fields[]={{&GUID_Key,offsetW,(DWORD)(DIDFT_BUTTON|DIDFT_MAKEINSTANCE(scanW)),0},{&GUID_Key,1,(DWORD)(DIDFT_BUTTON|DIDFT_MAKEINSTANCE(scanA)),0}};
    DIDATAFORMAT custom{sizeof(DIDATAFORMAT),sizeof(DIOBJECTDATAFORMAT),DIDF_RELAXIS,8,2,fields};
    Check(SUCCEEDED(Format(device,custom)),"native compact custom keyboard descriptor accepted");
    BYTE compactState[9];memset(compactState,0xcc,sizeof(compactState));hr=state(device,8,compactState);
    Check(SUCCEEDED(hr)&&compactState[offsetW]==0x80&&compactState[1]==0&&compactState[7]==0&&compactState[8]==0xcc,"actual custom GetDeviceState uses declared offset and extent");
    custom.dwDataSize=256;Check(SUCCEEDED(Format(device,custom)),"native 256-byte custom keyboard descriptor accepted");memset(standard,0xcc,sizeof(standard));hr=state(device,sizeof(standard),standard);
    Check(SUCCEEDED(hr)&&standard[offsetW]==0x80&&standard[scanW]==0,"actual custom256 layout never guesses standard scan offsets");custom.dwDataSize=8;Check(SUCCEEDED(Format(device,custom)),"native compact descriptor restored");
    DIPROPDWORD capacity{};capacity.diph={sizeof(capacity),sizeof(DIPROPHEADER),0,DIPH_DEVICE};capacity.dwData=32;
    Check(SUCCEEDED(Property(device,DIPROP_BUFFERSIZE,&capacity.diph)),"native keyboard buffer capacity accepted unacquired");
    DIPROPPOINTER appdata{};appdata.diph={sizeof(appdata),sizeof(DIPROPHEADER),offsetW,DIPH_BYOFFSET};appdata.uData=0x12345;
    Check(SUCCEEDED(Property(device,DIPROP_APPDATA,&appdata.diph)),"native per-object keyboard appdata accepted");
    {std::lock_guard<std::mutex> lock(g_diMx);g_diKbdQ.clear();}
    DIDEVICEOBJECTDATA events[4]{};DWORD count=4;hr=buffer(device,sizeof(events[0]),events,&count,0);
    Check(SUCCEEDED(hr)&&count==0,"actual unacquired buffered reader primes an empty virtual queue");
    Check(DiPush(true,scanW,0x80)==1,"observed keyboard buffer accepts one software event");const auto beforeBuffer=g_keyTrace['W'].di_buffer.down_reads.load();count=4;hr=buffer(device,sizeof(events[0]),events,&count,0);
    Check(SUCCEEDED(hr)&&count==1&&events[0].dwOfs==offsetW&&events[0].dwData==0x80&&events[0].uAppData==appdata.uData&&g_keyTrace['W'].di_buffer.down_reads>beforeBuffer,"actual buffered keyboard returns custom offset and native appdata with trace");
    // Null GUID is a native valid type/instance selector, deliberately outside
    // this implementation's explicit GUID_Key proof. No native action map runs.
    DIOBJECTDATAFORMAT unproved{nullptr,0,(DWORD)(DIDFT_BUTTON|DIDFT_MAKEINSTANCE(scanW)),0};DIDATAFORMAT unsupported{sizeof(DIDATAFORMAT),sizeof(DIOBJECTDATAFORMAT),DIDF_RELAXIS,8,1,&unproved};
    const HRESULT accepted=Format(device,unsupported);printf("unsupported-native-format hr=0x%08lX\n",(unsigned long)accepted);Check(SUCCEEDED(accepted),"native unproved-GUID selector supplies an explicit unsupported control");
    BYTE untouched[8];memset(untouched,0xcc,sizeof(untouched));const auto refusedBefore=g_diFormatRefused.load();hr=state(device,sizeof(untouched),untouched);
    Check(SUCCEEDED(accepted)&&hr==DIERR_UNSUPPORTED&&untouched[0]==0xcc&&untouched[7]==0xcc&&g_diFormatRefused>refusedBefore,"unsupported explicit native layout refuses without rewriting caller bytes");
    Check(SUCCEEDED(Format(device,c_dfDIKeyboard)),"standard native layout remains recoverable after refusal");g_vk['W']=0;
    Check(lowerAcquire==0&&lowerCoop==0,"all native keyboard format/read operations requested zero acquisition/cooperative rights");
}
int main(int argc,char** argv){
    if(argc==2&&!strcmp(argv[1],"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
    g_pid=GetCurrentProcessId();g_cfg.fakefocus=true;g_cfg.isolate=true;g_jobClaimExpiry=HousekeepingNow()+60000;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))return 111;
    void* probes[2]{};void* probeOwners[2]{};bool ready=MH_Initialize()==MH_OK;std::set<void*> acquireEntries,coopEntries;
    for(unsigned wide=0;wide<2;++wide){const auto hr=DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,wide?IID_IDirectInput8W:IID_IDirectInput8A,&probeOwners[wide],nullptr);
        ready=ready&&SUCCEEDED(hr)&&probeOwners[wide]&&SUCCEEDED(((NativeCreate)(*(void***)probeOwners[wide])[3])(probeOwners[wide],GUID_SysKeyboard,&probes[wide],nullptr))&&probes[wide];
        if(probes[wide]){void** table=*(void***)probes[wide];if(acquireEntries.insert(table[7]).second)ready=ready&&LowerEntry(table[7],(void*)&AbsorbAcquire);if(coopEntries.insert(table[13]).second)ready=ready&&LowerEntry(table[13],(void*)&AbsorbCoop);}
    }
    Check(ready,"lower actual A/W keyboard Acquire/coop absorbers installed without delegation");if(!ready)return 111;
    const auto acquireControl=((PFN_DIACQ)(*(void***)probes[0])[7])(probes[0]);
    const auto coopControl=((PFN_DICOOP)(*(void***)probes[0])[13])(probes[0],nullptr,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE);
    Check(acquireControl==DIERR_OTHERAPPHASPRIO&&coopControl==DIERR_OTHERAPPHASPRIO&&lowerAcquire==1&&lowerCoop==1,"positive controls reach lower nondelegating rights absorbers only");lowerAcquire=0;lowerCoop=0;
    TryDi8Factory();Check(g_diFactoryEnabled,"actual DirectInput8Create export hook installed");
    for(unsigned wide=0;wide<2;++wide){void* owner=nullptr;void* device=nullptr;const auto hr=DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,wide?IID_IDirectInput8W:IID_IDirectInput8A,&owner,nullptr);
        const bool created=SUCCEEDED(hr)&&owner&&SUCCEEDED(((NativeCreate)(*(void***)owner)[3])(owner,GUID_SysKeyboard,&device,nullptr))&&device;
        Check(created,"real application factory and created keyboard pass production discovery");if(created)KeyboardChecks(device,wide);
        ReleaseObject(device);ReleaseObject(owner);
    }
    for(unsigned i=0;i<2;++i){ReleaseObject(probes[i]);ReleaseObject(probeOwners[i]);}
    Check(g_diFactoryReturns>=2&&g_diDeviceReturns>=2,"actual factory/device counters establish boundary observation");
    Check(lowerAcquire==0&&lowerCoop==0&&vi_launch_terminal::Verify(),"zero native rights requests and terminal desktop guards verified after cleanup");
    CoUninitialize();printf("DI keyboard: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
