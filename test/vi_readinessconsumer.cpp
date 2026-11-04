#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output=nullptr;
static HWND secondWindow=nullptr;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_KEYDOWN||m==WM_KEYUP){std::fprintf(output,"MSG key=%llu down=%d\n",(unsigned long long)w,m==WM_KEYDOWN);std::fflush(output);}
 if(m==WM_APP+1){std::fprintf(output,"PAUSED\n");std::fflush(output);Sleep((DWORD)w);std::fprintf(output,"RESUMED\n");std::fflush(output);return 0;}
 if(m==WM_APP+2){
  if(!secondWindow||!IsWindow(secondWindow)){
   auto instance=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);
   secondWindow=CreateWindowExW(WS_EX_NOACTIVATE,L"vi_readinessconsumer",L"Retarget readiness observer",WS_OVERLAPPEDWINDOW,-27500,600,600,420,nullptr,nullptr,instance,nullptr);
   if(secondWindow)ShowWindow(secondWindow,SW_SHOWNOACTIVATE);
  }
  if(secondWindow){RECT client{};GetClientRect(secondWindow,&client);std::fprintf(output,"RETARGET_READY hwnd=0x%llX pid=%lu client_w=%ld client_h=%ld\n",(unsigned long long)(ULONG_PTR)secondWindow,GetCurrentProcessId(),client.right,client.bottom);}
  else std::fprintf(output,"RETARGET_FAILED error=%lu\n",GetLastError());
  std::fflush(output);return 0;
 }
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
 int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args||count<2)return 2;
 output=_wfsopen(args[1],L"w",_SH_DENYNO);const DWORD delay=count>2?wcstoul(args[2],nullptr,10):0;LocalFree(args);if(!output)return 3;
 std::fprintf(output,"START pid=%lu\n",GetCurrentProcessId());std::fflush(output);if(delay)Sleep(delay);
 WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"vi_readinessconsumer";RegisterClassW(&cls);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Input readiness observer",WS_OVERLAPPEDWINDOW,-28000,600,400,300,nullptr,nullptr,instance,nullptr);if(!window)return 4;ShowWindow(window,SW_SHOWNOACTIVATE);
 std::fprintf(output,"READY hwnd=0x%llX\n",(unsigned long long)(ULONG_PTR)window);std::fflush(output);
 int previous=-1;MSG message{};const auto began=GetTickCount64();
 while(GetTickCount64()-began<60000) {
  while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){std::fclose(output);return 0;}TranslateMessage(&message);DispatchMessageW(&message);}
  const int down=(GetAsyncKeyState(VK_INSERT)&0x8000)!=0;if(down!=previous){std::fprintf(output,"OBS insert=%d\n",down);std::fflush(output);previous=down;}Sleep(5);
 }
 std::fclose(output);return 0;
}
