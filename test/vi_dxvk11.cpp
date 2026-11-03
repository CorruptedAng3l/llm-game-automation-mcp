// Copied D3D11 -> DXVK capture fixture. No input polling or activation calls.
// The package DLLs are placed only next to a COPY of this fixture by its harness.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <share.h>
#include "vi_pattern.h"
#define VK_NO_PROTOTYPES
#include "../third_party/vulkan/vulkan.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static FILE* output=nullptr;
static void Note(const char* format,...){if(!output)return;va_list args;va_start(args,format);fprintf(output,"%llu ",GetTickCount64());vfprintf(output,format,args);fprintf(output,"\n");fflush(output);va_end(args);}
static void Module(const wchar_t* name){wchar_t path[MAX_PATH]{};HMODULE module=GetModuleHandleW(name);if(module)GetModuleFileNameW(module,path,MAX_PATH);fprintf(output,"MODULE %ls path=%ls\n",name,path);fflush(output);}
static bool ProcScope(){
 HMODULE module=GetModuleHandleW(L"vulkan-1.dll");if(!module)return true;
 auto gipa=(PFN_vkGetInstanceProcAddr)GetProcAddress(module,"vkGetInstanceProcAddr");if(!gipa)return false;
 auto enumerate=(PFN_vkEnumerateInstanceExtensionProperties)gipa(nullptr,"vkEnumerateInstanceExtensionProperties");
 uint32_t count=0;const bool pass=gipa(nullptr,"vkCreateDevice")==nullptr && gipa(nullptr,"vk_vinput_unsupported_name")==nullptr && gipa(nullptr,"vkCreateInstance")!=nullptr && enumerate && enumerate(nullptr,&count,nullptr)==VK_SUCCESS && count>0;
 Note("VULKAN_PROC_SCOPE pass=%d extension_count=%u",(int)pass,count);return pass;
}
static LRESULT CALLBACK Window(HWND h,UINT message,WPARAM w,LPARAM l){if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,message,w,l);}
#define CHECK_HR(expression) do{HRESULT result=(expression);if(FAILED(result)){Note("FAIL %s hr=0x%08X",#expression,(unsigned)result);return 2;}}while(0)
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 output=_wfsopen(argc>1?argv[1]:L"vi_dxvk11.log",L"w",_SH_DENYNO);if(!output)return 3;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Window;wc.lpszClassName=L"vi_dxvk11";RegisterClassW(&wc);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"DXVK pattern fixture",WS_OVERLAPPEDWINDOW,-28000,600,836,519,nullptr,nullptr,instance,nullptr);if(!window)return 4;
 ShowWindow(window,SW_SHOWNOACTIVATE);RECT client{};GetClientRect(window,&client);
 const UINT width=(UINT)client.right,height=(UINT)client.bottom;
 DXGI_SWAP_CHAIN_DESC description{};description.BufferCount=2;description.BufferDesc.Width=width;description.BufferDesc.Height=height;description.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.OutputWindow=window;description.SampleDesc.Count=1;description.Windowed=TRUE;description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 IDXGISwapChain* swapchain=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
 CHECK_HR(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&description,&swapchain,&device,nullptr,&context));
 ID3D11Texture2D* backbuffer=nullptr;CHECK_HR(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&backbuffer));
 D3D11_TEXTURE2D_DESC texture{};backbuffer->GetDesc(&texture);texture.BindFlags=0;texture.MiscFlags=0;texture.Usage=D3D11_USAGE_DEFAULT;texture.CPUAccessFlags=0;
 ID3D11Texture2D* source=nullptr;CHECK_HR(device->CreateTexture2D(&texture,nullptr,&source));
 Module(L"d3d11.dll");Module(L"dxgi.dll");Module(L"vulkan-1.dll");if(!ProcScope())return 5;
 Note("DEVICE_READY hwnd=0x%llX client=%ux%u ex=0x%llX",(unsigned long long)(ULONG_PTR)window,width,height,(unsigned long long)GetWindowLongPtrW(window,GWL_EXSTYLE));
 std::vector<unsigned char> pixels((size_t)width*height*4);UINT frame=0;const ULONGLONG deadline=GetTickCount64()+120000;
 while(GetTickCount64()<deadline){
  MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)goto done;TranslateMessage(&message);DispatchMessageW(&message);}
  ++frame;for(UINT y=0;y<height;++y)vipat::Row(vipat::BGRA8,(int)y,(int)width,frame,pixels.data()+(size_t)y*width*4);
  context->UpdateSubresource(source,0,nullptr,pixels.data(),width*4,0);context->CopyResource(backbuffer,source);
  CHECK_HR(swapchain->Present(0,0));if(frame%60==0)Note("FRAME %u",frame);Sleep(16);
 }
done:
 Note("EXIT frames=%u",frame);source->Release();backbuffer->Release();context->Release();device->Release();swapchain->Release();DestroyWindow(window);LocalFree(argv);fclose(output);return 0;
}
