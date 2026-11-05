// Real pre-existing DI8 A/W mouse objects, called through their actual vtables.
// No Acquire, focus change, OS input, or renderer/window creation.
#include <vinput_core.cpp>
#include "vi_launch_terminal.h"
#pragma comment(lib,"dinput8.lib")
static unsigned passed=0,failed=0;
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
    TryHookDInput();
#ifdef VI_DI_POLL_REGISTRY
    Check(g_di8ProbeRoles==15,"all keyboard/mouse A/W implementation roles hooked");
    Check(!DiPollInstall((*(void***)a)[9],true),"state entry cannot be reinterpreted as buffered ABI");
    Check(!ClaimDiInstallEntry((*(void***)a)[9],false),"poll entry cannot be reinterpreted as Acquire ABI");
    g_diMouseMovesSincePoll=3;
    Check(StatusJson().find("mouse has not polled DirectInput")!=std::string::npos,"three unpolled moves produce actionable status warning");
#else
    Check(false,"all keyboard/mouse A/W implementation roles hooked");Check(false,"state entry cannot be reinterpreted as buffered ABI");Check(false,"poll entry cannot be reinterpreted as Acquire ABI");Check(false,"three unpolled moves produce actionable status warning");
#endif
    for(unsigned i=0;i<2;++i){
        void* device=i?(void*)w:(void*)a;const auto state=(PFN_DIGDS)(*(void***)device)[9];const auto buffer=(PFN_DIGDD)(*(void***)device)[10];
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
        g_vk[VK_LBUTTON]=0;
    }
#ifdef VI_DI_POLL_REGISTRY
    Check(StatusJson().find("state_and_buffer")!=std::string::npos&&g_diMouseMovesSincePoll==0,"status names actual mouse polling channels and clears warning");
#else
    Check(false,"status names actual mouse polling channels and clears warning");
#endif
    a->Release();w->Release();ansi->Release();wide->Release();
    Check(vi_launch_terminal::Verify(),"terminal desktop writers remain guarded; native Acquire never called");
    printf("DI8 mouse: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
