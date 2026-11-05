// Actual pointer command branch with terminal writers and no window/product Init.
#include "vinput_core.cpp"
#include "vi_launch_terminal.h"
bool mutatePress=false;
bool ModelScopeAdmit(const std::string& cmd,const JObj& j,CaptureScope& scope,std::string& error){if(mutatePress)++g_diMouseObservation[0].press;return PointerScopeAdmit(cmd,j,scope,error);}
std::string Command(const JObj& j){const auto cmd=JStr(j,"cmd");
#define PointerScopeAdmit ModelScopeAdmit
#include "actual_pointer_branch.inl"
#undef PointerScopeAdmit
 return ErrJ("unsupported model command");
}
void Seed(bool held=true){g_hwnd=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x1234));g_cfg.msgmode=2;g_cfg.rawmode=3;g_cfg.autoactivate=false;g_vcurX=100;g_vcurY=200;memset((void*)g_vk,0,sizeof g_vk);g_vk[VK_LBUTTON]=held?0x80:0;g_vk['W']=0x80;g_diMouseObservation[0].press=101;mutatePress=false;}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;g_pid=GetCurrentProcessId();unsigned passed=0,failed=0;auto check=[&](bool b,const char* n){printf("%s %s\n",b?"PASS":"FAIL",n);b?++passed:++failed;};
 JObj j;j["cmd"]={1,"mouseup"};j["button"]={1,"left"};j["expected_capture_scope"]={1,"{}"};j["x"]={2,{},500};j["y"]={2,{},300};
 Seed();auto r=Command(j);check(!g_vk[VK_LBUTTON]&&g_vk['W']&&g_vcurX==100&&g_vcurY==200&&r.find("\"cleanup_attempted\":true")!=std::string::npos&&r.find("\"button_released\":true")!=std::string::npos&&r.find("no input was injected")==std::string::npos,"rejected scoped mouseup reports original-generation cleanup without a false no-input claim");
 Seed();mutatePress=true;r=Command(j);check(g_vk[VK_LBUTTON]&&g_vk['W']&&g_vcurX==100&&r.find("\"button_released\":false")!=std::string::npos,"rejected cleanup cannot release a newer button generation");
 Seed();auto bad=j;bad.erase("y");r=Command(bad);check(!g_vk[VK_LBUTTON]&&g_vk['W']&&g_vcurX==100,"malformed positioned up still performs only original-generation cleanup");
 Seed();g_hwnd=nullptr;r=Command(j);check(!g_vk[VK_LBUTTON]&&g_vk['W']&&g_vcurX==100&&r.find("\"button_released\":true")!=std::string::npos,"no-window scoped mouseup reports actual cleanup result");
 Seed(false);j["cmd"]={1,"mousedown"};r=Command(j);check(!g_vk[VK_LBUTTON]&&g_vk['W']&&g_vcurX==100&&r.find("\"input_injected\":false")!=std::string::npos,"rejected scoped down never hovers or presses");
 check(vi_launch_terminal::Verify(),"all terminal desktop writer boundaries remain installed");
 printf("pointer release memory: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
