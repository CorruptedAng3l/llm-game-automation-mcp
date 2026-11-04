#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* messageLog=nullptr;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(messageLog&&(m==WM_KEYDOWN||m==WM_KEYUP)){std::fprintf(messageLog,"KEY message=%u vk=%llu\n",m,(unsigned long long)w);std::fflush(messageLog);}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv||argc<2)return 2;FILE* out=_wfsopen(argv[1],L"w",_SH_DENYNO);const unsigned delay=argc>2?wcstoul(argv[2],nullptr,10):0;LocalFree(argv);if(!out)return 3;
 messageLog=out;std::fprintf(out,"START pid=%lu\n",GetCurrentProcessId());std::fflush(out);if(delay)Sleep(delay);
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"vi_keysequenceconsumer";RegisterClassW(&cls);
 HWND h=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Sequence observer",WS_OVERLAPPEDWINDOW,-28000,600,420,260,nullptr,nullptr,instance,nullptr);if(!h)return 4;ShowWindow(h,SW_SHOWNOACTIVATE);
 std::fprintf(out,"READY hwnd=0x%llX\n",(unsigned long long)(ULONG_PTR)h);std::fflush(out);int previous=-1;const auto began=GetTickCount64();
 while(GetTickCount64()-began<30000){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){std::fclose(out);return 0;}TranslateMessage(&msg);DispatchMessageW(&msg);}
  int bits=0;const int keys[]={'W',VK_SPACE,'D',VK_CONTROL,VK_INSERT,'A'};for(int i=0;i<6;++i)if(GetAsyncKeyState(keys[i])&0x8000)bits|=1<<i;
  if(bits!=previous){std::fprintf(out,"%llu OBS bits=%d\n",GetTickCount64(),bits);std::fflush(out);previous=bits;}Sleep(5);
 }
 std::fclose(out);return 0;
}
