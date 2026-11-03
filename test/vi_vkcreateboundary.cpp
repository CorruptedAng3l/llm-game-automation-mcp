// A fake loader exercises the actual injected hook's native-call count/error
// propagation. It is an ABI/error-policy control, not Vulkan pixel evidence.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <atomic>
#include <share.h>
#include <string>
#define VK_NO_PROTOTYPES
#include "../third_party/vulkan/vulkan_core.h"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
#ifdef VI_FAKE_VK_LOADER
static std::atomic<unsigned> mode{0},calls{0},sourceCalls{0},oldCalls{0};
extern "C" __declspec(dllexport) void WINAPI FixtureMode(unsigned next){mode=next;calls=sourceCalls=oldCalls=0;}
extern "C" __declspec(dllexport) void WINAPI FixtureCounts(unsigned* values){values[0]=calls;values[1]=sourceCalls;values[2]=oldCalls;}
extern "C" __declspec(dllexport) VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks*,VkSwapchainKHR* out){++calls;bool source=(info->imageUsage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;if(source)++sourceCalls;if(info->oldSwapchain)++oldCalls;VkResult result=VK_SUCCESS;
 if(mode==5)result=VK_ERROR_OUT_OF_DATE_KHR;
 else if(source)switch(mode.load()){case 0:result=VK_ERROR_OUT_OF_DATE_KHR;break;case 1:result=VK_ERROR_SURFACE_LOST_KHR;break;case 2:result=VK_ERROR_DEVICE_LOST;break;case 3:result=VK_ERROR_FORMAT_NOT_SUPPORTED;break;default:break;}
 if(result==VK_SUCCESS&&out)*out=(VkSwapchainKHR)(uintptr_t)0x1234;return result;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
#else
static bool go=false,quit=false;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_APP+1){go=true;return 0;}if(m==WM_DESTROY){quit=true;PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
static void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT){quit=true;return;}TranslateMessage(&m);DispatchMessageW(&m);}}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){int argc=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<3)return 2;FILE* file=_wfsopen(args[1],L"w",_SH_DENYNO);std::wstring loader=args[2];LocalFree(args);if(!file)return 3;WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=Proc;c.lpszClassName=L"vi_vkcreateboundary";RegisterClassW(&c);HWND window=CreateWindowExW(WS_EX_NOACTIVATE,c.lpszClassName,L"Vulkan boundary ABI control",WS_OVERLAPPEDWINDOW,-28000,600,320,240,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);fprintf(file,"READY\n");fflush(file);while(!go&&!quit){Pump();Sleep(1);}
 HMODULE module=LoadLibraryW(loader.c_str());if(!module){fprintf(file,"FAIL LOAD\n");fclose(file);return 4;}using Set=void(WINAPI*)(unsigned);using Counts=void(WINAPI*)(unsigned*);auto set=(Set)GetProcAddress(module,"FixtureMode");auto counts=(Counts)GetProcAddress(module,"FixtureCounts");auto create=(PFN_vkCreateSwapchainKHR)GetProcAddress(module,"vkCreateSwapchainKHR");if(!set||!counts||!create){fprintf(file,"FAIL EXPORTS\n");fclose(file);return 5;}
 struct Case{unsigned mode;bool old,source;};Case cases[]={{0,true,false},{1,true,false},{2,true,false},{3,false,false},{4,true,false},{5,true,true},{0,false,false}};unsigned index=0;
 for(auto test:cases){set(test.mode);VkSwapchainCreateInfoKHR info{};info.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|(test.source?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);info.oldSwapchain=test.old?(VkSwapchainKHR)(uintptr_t)0x999:VK_NULL_HANDLE;VkSwapchainKHR created=VK_NULL_HANDLE;VkResult result=create((VkDevice)(uintptr_t)1,&info,nullptr,&created);unsigned values[3]{};counts(values);fprintf(file,"CASE index=%u mode=%u old=%d provided_source=%d calls=%u source_calls=%u old_calls=%u result=%d input_usage=%u\n",index++,test.mode,(int)test.old,(int)test.source,values[0],values[1],values[2],(int)result,(unsigned)info.imageUsage);fflush(file);}
 fprintf(file,"DONE\n");fflush(file);while(!quit){Pump();Sleep(1);}DestroyWindow(window);fclose(file);return 0;
}
#endif
