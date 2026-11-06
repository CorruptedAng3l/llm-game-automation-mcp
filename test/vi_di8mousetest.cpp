// Real pre-existing DI8 A/W mouse objects, called through their actual vtables.
// Every native Acquire ends at a lower absorber; no physical device is acquired.
#include <vinput_core.cpp>
#include "vi_launch_terminal.h"
#include <set>
#undef MH_Initialize
#undef MH_CreateHook
#undef MH_EnableHook
extern "C" MH_STATUS WINAPI MH_Initialize();
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*);
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID);
#pragma comment(lib,"dinput8.lib")
static unsigned passed=0,failed=0;
static std::atomic<unsigned> lowerAcquireCalls{0};
static HRESULT STDMETHODCALLTYPE AbsorbAcquire(void*){++lowerAcquireCalls;return DIERR_OTHERAPPHASPRIO;}
static void Check(bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);ok?++passed:++failed;}
int main(int argc,char** argv){
    if(argc==2&&!strcmp(argv[1],"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
    if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
    g_pid=GetCurrentProcessId();g_cfg.fakefocus=true;g_cfg.isolate=true;
    IDirectInput8A* ansi=nullptr;IDirectInput8W* wide=nullptr;
    IDirectInputDevice8A* a=nullptr;IDirectInputDevice8W* w=nullptr;
    const auto instance=GetModuleHandleW(nullptr);
    const bool created=SUCCEEDED(DirectInput8Create(instance,DIRECTINPUT_VERSION,IID_IDirectInput8A,(void**)&ansi,nullptr))&&
        SUCCEEDED(DirectInput8Create(instance,DIRECTINPUT_VERSION,IID_IDirectInput8W,(void**)&wide,nullptr))&&
        SUCCEEDED(ansi->CreateDevice(GUID_SysMouse,&a,nullptr))&&SUCCEEDED(wide->CreateDevice(GUID_SysMouse,&w,nullptr));
    Check(created,"real A/W mouse objects created before hook installation");if(!created)return 2;
    Check(SUCCEEDED(a->SetDataFormat(&c_dfDIMouse2))&&SUCCEEDED(w->SetDataFormat(&c_dfDIMouse2)),"native standard mouse formats accepted");
    DIPROPDWORD property{};property.diph.dwSize=sizeof(property);property.diph.dwHeaderSize=sizeof(property.diph);property.diph.dwHow=DIPH_DEVICE;property.dwData=32;
    Check(SUCCEEDED(a->SetProperty(DIPROP_BUFFERSIZE,&property.diph))&&SUCCEEDED(w->SetProperty(DIPROP_BUFFERSIZE,&property.diph)),"native mouse buffer properties accepted without acquisition");
    BYTE scratch{};const HRESULT invalidNative[]={a->GetDeviceState(1,&scratch),w->GetDeviceState(1,&scratch)};
    printf("native DI8 A state=%p data=%p W state=%p data=%p\n",(*(void***)a)[9],(*(void***)a)[10],(*(void***)w)[9],(*(void***)w)[10]);
    bool lower=MH_Initialize()==MH_OK;std::set<void*> entries;
    for(void* device:{(void*)a,(void*)w}){void* entry=(*(void***)device)[7];if(entries.insert(entry).second){void* unused=nullptr;lower=lower&&MH_CreateHook(entry,(void*)&AbsorbAcquire,&unused)==MH_OK&&unused&&MH_EnableHook(entry)==MH_OK;}}
    Check(lower,"lower native Acquire absorber installed before production hooks");if(!lower)return 111;
    TryHookDInput();
#ifdef VI_DI_POLL_REGISTRY
    Check(g_di8ProbeRoles==15,"all keyboard/mouse A/W implementation roles hooked");
    Check(!DiPollInstall((*(void***)a)[9],true),"state entry cannot be reinterpreted as buffered ABI");
    Check(!ClaimDiInstallEntry((*(void***)a)[9],false),"poll entry cannot be reinterpreted as Acquire ABI");
    g_diMouseMovesSincePoll=3;
    {const auto status=StatusJson();Check(status.find("\"mouse_poll_unobserved\":true")!=std::string::npos&&
        (status.find("DirectInput mouse consumer has not polled")!=std::string::npos||status.find("no DirectInput mouse consumer was observed")!=std::string::npos),"three unpolled moves produce structured status plus the applicable consumer warning or note");}
#else
    Check(false,"all keyboard/mouse A/W implementation roles hooked");Check(false,"state entry cannot be reinterpreted as buffered ABI");Check(false,"poll entry cannot be reinterpreted as Acquire ABI");Check(false,"three unpolled moves produce structured status plus the applicable consumer warning or note");
#endif
    for(unsigned i=0;i<2;++i){
        void* device=i?(void*)w:(void*)a;const auto state=(PFN_DIGDS)(*(void***)device)[9];const auto buffer=(PFN_DIGDD)(*(void***)device)[10];
        g_jobClaimExpiry=HousekeepingNow()+60000;
        const auto acquired=((PFN_DIACQ)(*(void***)device)[7])(device);
        Check(acquired==DI_OK&&lowerAcquireCalls==0,"claimed real DI8 mouse gets virtual Acquire success with no native acquisition");
        g_diDX=123;g_diDY=-45;g_vk[VK_LBUTTON]=1;
        const auto before=C.di_mouse.load();DIMOUSESTATE2 output{};auto hr=state(device,sizeof(output),&output);
        Check(SUCCEEDED(hr)&&output.lX==123&&output.lY==-45&&(output.rgbButtons[0]&0x80)&&C.di_mouse==before+1,"real pre-existing mouse state consumes injected axes/button");
        output={};hr=state(device,sizeof(output),&output);
        Check(SUCCEEDED(hr)&&output.lX==0&&output.lY==0,"mouse deltas drain exactly once");
        Check(state(device,1,&output)==invalidNative[i],"invalid native state format retains measured native error");
        DIDEVICEOBJECTDATA events[8]{};DWORD count=8;buffer(device,sizeof(events[0]),events,&count,0); // learn the real buffered poll channel
        const auto beforeBuffer=C.di_data_mouse.load();
        Check(DiPush(false,DIMOFS_X,17)==1&&DiPush(false,DIMOFS_BUTTON0,0x80)==1,"observed buffered reader accepts mouse events");
        count=8;hr=buffer(device,sizeof(events[0]),events,&count,DIGDD_PEEK);
        Check(SUCCEEDED(hr)&&count==2&&events[0].dwOfs==DIMOFS_X&&events[0].dwData==17&&g_diMouseQ.size()==2,"buffered PEEK returns exact motion/button without consuming");
        count=8;hr=buffer(device,sizeof(events[0]),events,&count,0);
        Check(SUCCEEDED(hr)&&count==2&&g_diMouseQ.empty()&&C.di_data_mouse>=beforeBuffer+2,"buffered mouse data drains and reports polling");
        g_cfg.rawmode=3;MouseWheel(-240,false);output={};hr=state(device,sizeof(output),&output);
        Check(SUCCEEDED(hr)&&output.lZ==-240,"production vertical wheel reaches real DI8 mouse state lZ with its sign");
        output={};hr=state(device,sizeof(output),&output);Check(SUCCEEDED(hr)&&output.lZ==0,"DI8 state wheel delta drains exactly once");
        count=8;hr=buffer(device,sizeof(events[0]),events,&count,DIGDD_PEEK);
        Check(SUCCEEDED(hr)&&count==1&&events[0].dwOfs==DIMOFS_Z&&(LONG)events[0].dwData==-240&&g_diMouseQ.size()==1,"production wheel reaches DI8 buffered Z and PEEK preserves it");
        count=8;hr=buffer(device,sizeof(events[0]),events,&count,0);Check(SUCCEEDED(hr)&&count==1&&g_diMouseQ.empty(),"DI8 buffered wheel drains exactly once");
        g_vk[VK_LBUTTON]=0;
    }
#ifdef VI_DI_POLL_REGISTRY
    {const auto status=StatusJson();Check(status.find("state_and_buffer")!=std::string::npos&&status.find("\"mouse_poll_unobserved\":false")!=std::string::npos&&g_diMouseMovesSincePoll==0,"status names actual mouse polling channels and clears warning");}
    g_diMouseMovesSincePoll=3;{const auto status=StatusJson();Check(status.find("\"mouse_poll_unobserved\":true")!=std::string::npos&&status.find("previously observed DirectInput mouse consumer has not polled")!=std::string::npos,"known consumer gap preserves the structured flag and warning branch");}g_diMouseMovesSincePoll=0;
#else
    Check(false,"status names actual mouse polling channels and clears warning");
    Check(false,"known consumer gap preserves the structured flag and warning branch");
#endif
    // Menu engines may select a compact/permuted mouse format independently of
    // their in-game standard DIMOUSESTATE reader. Prove the exact descriptor.
    IDirectInputDevice8A* custom=nullptr;
    Check(SUCCEEDED(ansi->CreateDevice(GUID_SysMouse,&custom,nullptr))&&custom,"new real mouse object is discovered after production installation");
    if(custom){
        std::vector<DIOBJECTDATAFORMAT> fields(c_dfDIMouse.rgodf,c_dfDIMouse.rgodf+c_dfDIMouse.dwNumObjs);
        const DWORD offsets[]={8,0,16,5,4,6,7};for(unsigned n=0;n<fields.size();++n)fields[n].dwOfs=offsets[n];
        DIDATAFORMAT format{sizeof(DIDATAFORMAT),sizeof(DIOBJECTDATAFORMAT),DIDF_RELAXIS,24,(DWORD)fields.size(),fields.data()};
        const auto formatResult=custom->SetDataFormat(&format);printf("custom format hr=0x%08X observed=%d failures=%llu refused=%llu reason=%s\n",(unsigned)formatResult,DiFormatFind(custom)!=nullptr,g_diDiscoveryFailures.load(),g_diFormatRefused.load(),g_diFormatLastUnsupported.c_str());
        Check(SUCCEEDED(formatResult),"real permuted relative mouse format accepted by native DirectInput");
        Check(SUCCEEDED(custom->SetProperty(DIPROP_BUFFERSIZE,&property.diph)),"real custom mouse buffering configured without native acquisition");
        BYTE output[28];memset(output,0xcc,sizeof(output));g_diDX=123;g_diDY=-45;g_diDZ=240;g_vk[VK_LBUTTON]=1;
        const auto before=C.di_mouse.load();auto hr=custom->GetDeviceState(24,output);LONG x=0,y=0,z=0;memcpy(&x,output+8,4);memcpy(&y,output,4);memcpy(&z,output+16,4);
        Check(SUCCEEDED(hr)&&x==123&&y==-45&&z==240&&output[5]==0x80&&output[4]==0&&output[24]==0xcc&&C.di_mouse==before+1,"real custom mouse state maps axes/button at descriptor offsets and counts polling");
        memset(output,0xcc,sizeof(output));hr=custom->GetDeviceState(24,output);memcpy(&x,output+8,4);memcpy(&y,output,4);memcpy(&z,output+16,4);
        Check(SUCCEEDED(hr)&&x==0&&y==0&&z==0,"custom relative mouse axes drain exactly once");
        DIDEVICEOBJECTDATA events[8]{};DWORD count=8;custom->GetDeviceData(sizeof(events[0]),events,&count,0);
        {std::lock_guard<std::mutex> lock(g_diMx);g_diMouseQ.clear();}
        Check(DiPush(false,DIMOFS_X,17)==1&&DiPush(false,DIMOFS_BUTTON0,0x80)==1,"custom reader enables production buffered mouse queue");
        count=8;hr=custom->GetDeviceData(sizeof(events[0]),events,&count,DIGDD_PEEK);printf("custom PEEK hr=0x%08X count=%u queue=%zu offsets=%u/%u\n",(unsigned)hr,count,g_diMouseQ.size(),events[0].dwOfs,events[1].dwOfs);
        Check(SUCCEEDED(hr)&&count==2&&events[0].dwOfs==8&&events[0].dwData==17&&events[1].dwOfs==5&&g_diMouseQ.size()==2,"custom buffered PEEK maps each descriptor offset and preserves tokens");
        count=8;hr=custom->GetDeviceData(sizeof(events[0]),events,&count,0);
        Check(SUCCEEDED(hr)&&count==2&&g_diMouseQ.empty(),"custom buffered mouse tokens drain exactly once");
        g_vk[VK_LBUTTON]=0;custom->Release();
    }
    void* stateEntry=(*(void***)a)[9];
    Check(VIMH_DisableHook(stateEntry)==MH_OK,"owned polling hook can be removed for the changed-entry reject control");
    Check(FAILED(((PFN_DIACQ)(*(void***)a)[7])(a))&&lowerAcquireCalls==0,"changed polling entry refuses virtual Acquire without native acquisition");
    Check(VIMH_EnableHook(stateEntry)==MH_OK,"owned polling hook restored after reject control");
    a->Release();w->Release();ansi->Release();wide->Release();
    Check(vi_launch_terminal::Verify()&&lowerAcquireCalls==0,"terminal desktop writers remain guarded; native Acquire never called");
    printf("DI8 mouse: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
