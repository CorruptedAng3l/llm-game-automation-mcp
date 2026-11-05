// Actual launch entry plus real owned process trees. Every desktop writer is
// absorbed before entry and windows have a CBT activation veto + NOACTIVATE.
#define wmain ViFollowCliEntry
#include <vinput_cli.cpp>
#undef wmain
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#include "../src/vi_launch_follow.h"
static int pass=0,fail=0;
static void Check(bool b,const char* n){printf("%s %s\n",b?"PASS":"FAIL",n);b?++pass:++fail;}
static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
static DWORD WINAPI Deadline(void*){Sleep(55000);ExitProcess(119);}
static int Actor(int argc,wchar_t** argv){
    if(argc<4)return 2;
    const std::wstring role=argv[1],mode=argv[2],receipt=argv[3];
    if(role==L"--window"){
        if(!vi_launch_ack::ArmNoActivation())return 111;
        WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=WindowProc;c.lpszClassName=L"ViFollowOwned";
        if(!RegisterClassW(&c))return 111;
        HWND h=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"Owned follow fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,-28000,600,400,250,nullptr,nullptr,c.hInstance,nullptr);
        if(!h)return 111;
        FILE* out=nullptr;_wfopen_s(&out,receipt.c_str(),L"w");if(!out)return 111;
        fprintf(out,"%lu\n",GetCurrentProcessId());fclose(out);
        const auto until=GetTickCount64()+3500;
        while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}
        DestroyWindow(h);return 0;
    }
    wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);std::wstring image=own;
    if(mode==L"java")image=ExeDir()+L"\\javaw.exe";
    const auto next=role==L"--stub"&&mode==L"chain"?L"--relay":L"--window";
    std::wstring command=L"\""+image+L"\" "+next+L" "+mode+L" \""+receipt+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return 111;
    // Keep the intermediate alive long enough for a snapshot, then exit before
    // the final window is selected. No PID-based kill or external target access.
    Sleep(180);CloseHandle(child.hThread);CloseHandle(child.hProcess);return 0;
}
static void Model(){
    using namespace vi_launch_follow;
    Process root{10,1,100,150,L"NeoForge.exe",false,false};
    Tree tree(root);
    std::vector<Process> rows{{20,10,120,0,L"NeoForge.exe",true,true},{30,10,160,0,L"wrong.exe",true,true},{40,1,110,0,L"NeoForge.exe",true,true}};
    tree.Observe(rows);auto chosen=tree.Candidates(rows,L"");
    Check(chosen.size()==1&&chosen[0].pid==20,"same-name child survives parent exit; reused parent PID excluded");
    rows.push_back({21,20,130,0,L"javaw.exe",true,true});tree.Observe(rows);chosen=tree.Candidates(rows,L"");
    Check(chosen.size()==1&&chosen[0].pid==21,"deep Java child wins over visible ancestor");
    rows.push_back({22,20,131,0,L"javaw.exe",true,true});tree.Observe(rows);
    Check(tree.Candidates(rows,L"javaw.exe").size()==2,"ambiguous sibling windows never arbitrarily selected");
    Check(Launcher(L"SkyrimSE.exe")==L"SkyrimSELauncher.exe"&&Launcher(L"Oblivion.exe")==L"OblivionLauncher.exe"&&Launcher(L"other.exe").empty(),"only explicit interactive launcher mappings");
    rows={{20,10,200,0,L"NeoForge.exe",true,true}};
    Check(!tree.Contains(rows[0]),"PID reuse is a different process identity");
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HANDLE watchdog=CreateThread(nullptr,0,Deadline,nullptr,0,nullptr);if(!watchdog)return 111;CloseHandle(watchdog);
    if(!vi_launch_terminal::Install()||!vi_launch_terminal::Verify())return 111;
    if(argc==2&&!wcscmp(argv[1],L"--await-start")){char line[8]{};if(!fgets(line,sizeof(line),stdin)||strcmp(line,"RUN\n"))return 111;}
    else if(argc>1)return Actor(argc,argv);
    Model();wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);
    for(const wchar_t* mode:{L"same",L"java",L"chain",L"interactive"}){
        const auto receipt=ExeDir()+L"\\"+mode+L".pid";
        const bool interactive=!wcscmp(mode,L"interactive");
        Args a;a.opt={{"exe",W2U(interactive?ExeDir()+L"\\SkyrimSELauncher.exe":own)},{"args",W2U((interactive?L"--window ":L"--stub ")+std::wstring(mode)+L" \""+receipt+L"\"")},{"no-inject","true"},{"settle","0"},{"fg-guard","0"},{"timeout","8"}};
        if(interactive)a.opt.push_back({"wait-exe","SkyrimSE.exe"});
        const auto started=GetTickCount64();const int result=Launch(a);const auto elapsed=GetTickCount64()-started;
        FILE* input=nullptr;unsigned long expected=0;const auto publicationDeadline=GetTickCount64()+6000;
        do{_wfopen_s(&input,receipt.c_str(),L"r");if(input){fscanf_s(input,"%lu",&expected);fclose(input);}if(!expected)Sleep(20);}while(!expected&&GetTickCount64()<publicationDeadline);
        Check(expected!=0,"owned final child published its real PID");
        Check(result==0&&g_launchPid==expected&&(interactive?g_initialLaunchPid==expected:g_initialLaunchPid!=expected),interactive?"interactive launcher returns before Play":"actual launch follows window-owning child");
        Check(elapsed<8500,"process discovery respects single timeout");
        Sleep(3700); // finite child self-exit; never act on a reopened PID
    }
    Check(vi_launch_terminal::Verify(),"terminal writer boundary remains installed");
    printf("launch follow: %d PASS / %d FAIL\n",pass,fail);return fail?1:0;
}
