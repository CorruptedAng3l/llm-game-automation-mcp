#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <share.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"xinput.lib")
static FILE* output;
static bool measuring=false;
static ULONGLONG began=0,lastAt=0,onMs=0;static long long lxArea=0;
static bool lastD=false;static int lastLx=0,lastLy=0;
static unsigned sampleDelayMs=0;static ULONGLONG sampleDelayUntil=0;
static void Sample(){ULONGLONG now=GetTickCount64();if(measuring){ULONGLONG dt=now-lastAt;if(lastD)onMs+=dt;lxArea+=(long long)dt*lastLx;}lastAt=now;
 bool d=(GetAsyncKeyState('D')&0x8000)!=0;XINPUT_STATE state{};XInputGetState(0,&state);int lx=state.Gamepad.sThumbLX,ly=state.Gamepad.sThumbLY;
 if(ly!=1234)sampleDelayUntil=0;
 else if(lastLy!=1234&&sampleDelayMs&&!sampleDelayUntil){sampleDelayUntil=now+sampleDelayMs;fprintf(output,"%llu DELAY_PAD_SAMPLE ms=%u\n",now,sampleDelayMs);fflush(output);}
 if(now<sampleDelayUntil)return;
 if(d!=lastD||lx!=lastLx||ly!=lastLy){fprintf(output,"%llu OBS d=%d lx=%d ly=%d\n",now,(int)d,lx,ly);fflush(output);}lastD=d;lastLx=lx;lastLy=ly;
}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(m==WM_APP+1){Sample();onMs=0;lxArea=0;began=lastAt;measuring=true;fprintf(output,"%llu BEGIN token=%llu\n",began,(unsigned long long)w);fflush(output);return 0;}
 if(m==WM_APP+2){Sample();measuring=false;fprintf(output,"%llu MEASURE token=%llu elapsed=%llu on_ms=%llu lx_area=%lld\n",lastAt,(unsigned long long)w,lastAt-began,onMs,lxArea);fflush(output);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 2;output=_wfsopen(argv[1],L"w",_SH_DENYNO);if(argc>2)sampleDelayMs=(unsigned)_wtoi(argv[2]);LocalFree(argv);if(!output)return 3;
 WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"vi_steeringmeasure";RegisterClassW(&c);HWND h=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"steering integral",WS_OVERLAPPEDWINDOW,-28000,600,320,240,nullptr,nullptr,instance,nullptr);ShowWindow(h,SW_SHOWNOACTIVATE);fprintf(output,"READY\n");fflush(output);lastAt=GetTickCount64();bool run=true;
 while(run){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){run=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}Sample();Sleep(1);}fclose(output);return 0;
}
