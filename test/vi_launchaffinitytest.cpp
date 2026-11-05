#define wmain ViAffinityCliEntry
#include <vinput_cli.cpp>
#undef wmain
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include <vi_launch_affinity.h>
static unsigned passed=0,failed=0;
static void Check(bool good,const char* text){printf("%s %s\n",good?"PASS":"FAIL",text);good?++passed:++failed;}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
int wmain(int argc,wchar_t** argv){
    if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
    if(argc==3&&!wcscmp(argv[1],L"--actor")){
        DWORD_PTR process=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&process,&system)||!vi_launch_ack::ArmNoActivation())return 111;
        FILE* out=nullptr;_wfopen_s(&out,argv[2],L"w");if(!out)return 111;fprintf(out,"%llu\n",(unsigned long long)process);fclose(out);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.lpszClassName=L"ViAffinityOwned";if(!RegisterClassW(&cls))return 111;
        HWND h=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Owned affinity test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,400,250,nullptr,nullptr,cls.hInstance,nullptr);if(!h)return 111;
        const auto until=GetTickCount64()+4500;while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}DestroyWindow(h);return 0;
    }
    if(argc==2&&!wcscmp(argv[1],L"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
    std::uint64_t mask=0;
    for(const auto& invalid:{"","0x","0x0","15","-0x1","0xG","0x100000000","0xF suffix"})Check(!ParseLaunchAffinity(invalid,0xffffffffu,mask),"strict nonzero bounded hexadecimal affinity parsing");
    Check(ParseLaunchAffinity("0xF",0xffffffffu,mask)&&mask==15,"four-core helper mask parses exactly");
    Check(ParseLaunchAffinity("0XAb",0xffffffffu,mask)&&mask==171,"uppercase and mixed hexadecimal digits parse exactly");
    DWORD_PTR current=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&current,&system))return 111;
    const DWORD_PTR wanted=current&(~current+1);
    Check(wanted&&wanted!=current,"host permits a discriminating narrower affinity control");if(!wanted||wanted==current)return 2;
    wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);const auto receipt=ExeDir()+L"\\startup_affinity.txt";
    char hex[32];sprintf_s(hex,"0x%llX",(unsigned long long)wanted);
    Args options;options.opt={{"exe",W2U(own)},{"new-instance","true"},{"args",W2U(L"--actor \""+receipt+L"\"")},{"cpu-affinity",hex},{"no-inject","true"},{"timeout","8"},{"settle","0"},{"fg-guard","0"}};
    const auto rc=Launch(options);FILE* input=nullptr;_wfopen_s(&input,receipt.c_str(),L"r");unsigned long long observed=0;if(input){fscanf_s(input,"%llu",&observed);fclose(input);}
    Check(rc==0&&observed==wanted,"actual child observes requested affinity at startup");
    DWORD_PTR after=0;GetProcessAffinityMask(GetCurrentProcess(),&after,&system);Check(after==current,"parent affinity is unchanged");
    Sleep(4800);Check(vi_launch_terminal::Verify(),"desktop writer boundary retained; child self-exits");
    printf("launch affinity: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
