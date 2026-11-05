// Legacy launch-test argument surface, guarded own normal window only. No GPU,
// DirectInput device/Acquire, desktop restore, input or child process creation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <string>
#include <cstdio>
#include <climits>
#include "vi_launch_terminal.h"
#include "vi_launch_ack.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return m==WM_MOUSEACTIVATE?MA_NOACTIVATE:DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);if(!vi_launch_ack::ArmDeadline())return 111;
 int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);wchar_t expected[2048]{};const auto n=GetEnvironmentVariableW(L"VI_LAUNCH_TARGET_LOG",expected,2048);if(!args||argc!=6||!n||n>=2048||_wcsicmp(args[1],expected)){if(args)LocalFree(args);return 111;}
 long values[4]{};for(int i=0;i<4;++i){wchar_t* end=nullptr;values[i]=wcstol(args[i+2],&end,10);if(!end||*end){LocalFree(args);return 111;}}LocalFree(args);
 if(values[0]!=-28000||values[1]!=600||!((values[2]==420&&values[3]==260)||(values[2]==640&&values[3]==360)))return 111;
 FILE* out=_wfsopen(expected,L"w",_SH_DENYNO);if(!out)return 111;const bool safe=vi_launch_terminal::Install()&&vi_launch_terminal::Verify();vi_launch_terminal::Record(out,"before_windows",safe);if(!safe){fclose(out);return 111;}
 FILETIME b{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u)){fclose(out);return 111;}ULARGE_INTEGER born{};born.LowPart=b.dwLowDateTime;born.HighPart=b.dwHighDateTime;fprintf(out,"{\"event\":\"owned_identity\",\"pid\":%lu,\"creation_filetime\":%llu}\n",GetCurrentProcessId(),born.QuadPart);fflush(out);
 if(!vi_launch_ack::Wait(out,L"target")||!vi_launch_ack::ArmNoActivation()){fclose(out);return 111;}
 const long long left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);if(width<1||height<1||!((long long)values[0]+values[2]<=left||values[0]>=left+width||(long long)values[1]+values[3]<=top||values[1]>=top+height)){fclose(out);return 111;}
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Proc;cls.lpszClassName=L"ViLegacyLaunchOwnedFixture";if(!RegisterClassW(&cls)){fclose(out);return 111;}
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Synthetic legacy launch target",WS_OVERLAPPEDWINDOW|WS_VISIBLE,values[0],values[1],values[2],values[3],nullptr,nullptr,instance,nullptr);if(!window){fclose(out);return 111;}
 const wchar_t* lifetime=L"vi.legacy.launch.fixture.lifetime.v1";const ULONG_PTR cookie=(ULONG_PTR)&born;const DWORD thread=GetCurrentThreadId();if(!SetPropW(window,lifetime,(HANDLE)cookie)){DestroyWindow(window);fclose(out);return 111;}
 auto own=[&](){DWORD pid=0;return IsWindow(window)&&GetWindowThreadProcessId(window,&pid)==thread&&pid==GetCurrentProcessId()&&(ULONG_PTR)GetPropW(window,lifetime)==cookie;};
 RECT rectangle{},client{};POINT origin{};if(!own()||!GetWindowRect(window,&rectangle)||!GetClientRect(window,&client)||!ClientToScreen(window,&origin)||!(rectangle.right<=left||rectangle.left>=left+width||rectangle.bottom<=top||rectangle.top>=top+height)||!((long long)origin.x+client.right<=left||origin.x>=left+width||(long long)origin.y+client.bottom<=top||origin.y>=top+height)){if(own())DestroyWindow(window);fclose(out);return 111;}
 fprintf(out,"{\"event\":\"windows_ready\",\"pid\":%lu,\"thread\":%lu,\"main\":%llu,\"main_visible\":%s}\n",GetCurrentProcessId(),GetCurrentThreadId(),(unsigned long long)(ULONG_PTR)window,IsWindowVisible(window)?"true":"false");fflush(out);
 const auto until=GetTickCount64()+90000;while(GetTickCount64()<until){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}
 if(own())DestroyWindow(window);else fprintf(out,"{\"event\":\"cleanup_refused_lifetime_changed\"}\n");vi_launch_terminal::Record(out,"target_exit",safe);fclose(out);return 0;
}
