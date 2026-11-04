// Every write API is absorbed below the product before a probe can run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include "vi_cursor_spies.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output=nullptr;static HWND primary=nullptr,proxy=nullptr;static HINSTANCE instance=nullptr;static unsigned sequence=0;static std::wstring latePath;static bool safe=false;
static void Stats(const char* event,const unsigned long long* before=nullptr){
 fprintf(output,"%s seq=%u pid=%lu primary=%llu proxy=%llu mode=%d set=%llu physical=%llu clip=%llu send=%llu mouse=%llu total=%llu clip_query_matched=%d",event,sequence,GetCurrentProcessId(),(unsigned long long)(uintptr_t)primary,(unsigned long long)(uintptr_t)proxy,vi_cursor_spy::foregroundMode.load(),vi_cursor_spy::calls[0].load(),vi_cursor_spy::calls[1].load(),vi_cursor_spy::calls[2].load(),vi_cursor_spy::calls[3].load(),vi_cursor_spy::calls[4].load(),vi_cursor_spy::Total(),vi_cursor_spy::clipQueryMatched?1:0);
 fprintf(output," neutralized_foreground=%llu neutralized_queue=%llu neutralized_capture=%llu neutralized_permission=%llu",vi_cursor_spy::foregroundEffects.load(),vi_cursor_spy::queueEffects.load(),vi_cursor_spy::captureEffects.load(),vi_cursor_spy::permissionEffects.load());if(before){unsigned long long delta=0;const char* names[]={"set","physical","clip","send","mouse"};for(int i=0;i<5;i++){const auto change=vi_cursor_spy::calls[i]-before[i];delta+=change;fprintf(output," d_%s=%llu",names[i],change);}fprintf(output," delta=%llu",delta);}fputc('\n',output);fflush(output);
}
static void Probe(bool late){
 ++sequence;if(!safe){fprintf(output,"REFUSED seq=%u reason=absorbing_spies_not_verified\n",sequence);fflush(output);return;}
 unsigned long long before[5];for(int i=0;i<5;i++)before[i]=vi_cursor_spy::calls[i];
 if(late){HMODULE module=latePath.empty()?nullptr:LoadLibraryW(latePath.c_str());using Run=BOOL(WINAPI*)();auto run=module?(Run)GetProcAddress(module,"ViCursorLateProbe"):nullptr;if(module&&!run)run=(Run)GetProcAddress(module,"_ViCursorLateProbe@0");if(!run||!run()){fprintf(output,"LATE_FAILED seq=%u\n",sequence);fflush(output);}Stats("LATE",before);}
 else{vi_cursor_spy::Probe();Stats("PROBE",before);}
}
static LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_APP+1){Probe(false);return 1;}
 if(message==WM_APP+2){if(w>vi_cursor_spy::ModeledExternal)return 0;vi_cursor_spy::SetForegroundMode((vi_cursor_spy::ForegroundMode)w,proxy?proxy:primary);Stats("MODE");return 1;}
 if(message==WM_APP+3){if(!proxy){WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=instance;wc.lpszClassName=L"D3DProxyWindow";RegisterClassW(&wc);proxy=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"owned synthetic D3D proxy",WS_OVERLAPPEDWINDOW,-28000,700,480,320,nullptr,nullptr,instance,nullptr);if(proxy)ShowWindow(proxy,SW_SHOWNOACTIVATE);}Stats("PROXY");return proxy?1:0;}
 if(message==WM_APP+4){Probe(true);return 1;}
 if(message==WM_APP+5){const bool changed=vi_cursor_spy::ReplaceProductEntryWithAbsorber(0);fprintf(output,"ABSORBER_REPLACED ok=%d\n",changed?1:0);fflush(output);return changed?1:0;}
 if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){
 instance=hi;int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<3)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(!output)return 3;
 DWORD delay=0;for(int i=3;i<argc;i++){if(!wcscmp(argv[i],L"--delay")&&i+1<argc)delay=wcstoul(argv[++i],nullptr,10);else if(!wcscmp(argv[i],L"--late")&&i+1<argc)latePath=argv[++i];}
 safe=vi_cursor_spy::Install()&&vi_cursor_spy::VerifyBeforeProduct();fprintf(output,"START pid=%lu guard_version=1 safe_spies=%d no_native_write_trampoline_calls=1\n",GetCurrentProcessId(),safe?1:0);fflush(output);if(!safe)return 4;
 const std::wstring prefix=argv[2];HANDLE request=CreateEventW(nullptr,FALSE,FALSE,(prefix+L".request").c_str());if(!request)return 5;
 const auto began=GetTickCount64();for(;;){if(!primary&&GetTickCount64()-began>=delay){WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=hi;wc.lpszClassName=L"GNW95 Class";RegisterClassW(&wc);primary=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"owned synthetic legacy window",WS_OVERLAPPEDWINDOW,-28000,500,320,240,nullptr,nullptr,hi,nullptr);if(!primary)return 6;ShowWindow(primary,SW_SHOWNOACTIVATE);Stats("READY");}
  MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){CloseHandle(request);fclose(output);return 0;}TranslateMessage(&message);DispatchMessageW(&message);}
  if(WaitForSingleObject(request,0)==WAIT_OBJECT_0)Probe(false);Sleep(2);
 }
}
