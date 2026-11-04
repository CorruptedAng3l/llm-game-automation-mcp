// compilein_test.cpp - stands in for a host DLL (your own tool or mod) that (1) links its OWN MinHook and hooks Present like
// a typical overlay does, and (2) compiles vinput in behind VINPUT_COMPILE_IN.
// Proves: no duplicate symbols at link time, both MinHook instances coexist, both Present hooks chain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <string>
#include <share.h>
#include "MinHook.h"   // the host's own, un-renamed MinHook
#include "vinput.h"
#pragma comment(lib, "d3d11.lib")

static volatile LONG g_hostPresents = 0;
static volatile LONG g_hostReturns = 0;
static std::atomic<ULONGLONG> g_lastEnter{0},g_lastReturn{0},g_lastDuration{0};
static std::atomic<LONG> g_lastHresult{E_PENDING};
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
static PFN_Present o_Present;
static HRESULT STDMETHODCALLTYPE HostPresent(IDXGISwapChain* s, UINT a, UINT b) {
    const ULONGLONG began=GetTickCount64();g_lastEnter=began;InterlockedIncrement(&g_hostPresents);
    const HRESULT result=o_Present?o_Present(s,a,b):E_UNEXPECTED;
    g_lastHresult=result;g_lastDuration=GetTickCount64()-began;g_lastReturn=GetTickCount64();InterlockedIncrement(&g_hostReturns);
    return result;
}
extern "C" __declspec(dllexport) LONG host_present_count() { return g_hostPresents; }

static std::wstring g_journalPath,g_metricsPath,g_compatibilityPath;
static const char* g_phase="starting";
static bool g_installed=false,g_failed=false;
static int g_initialize=-1,g_create=-1,g_enable=-1;
static ULONGLONG g_pipeWaitMs=0;
static void Journal(const char* format,...) {
    FILE* file=_wfsopen(g_journalPath.c_str(),L"a",_SH_DENYNO);if(!file)return;
    fprintf(file,"%llu ",GetTickCount64());va_list arguments;va_start(arguments,format);vfprintf(file,format,arguments);va_end(arguments);
    fputc('\n',file);fclose(file);
}
static void Publish() {
    const std::wstring temporary=g_metricsPath+L".tmp";
    FILE* file=_wfsopen(temporary.c_str(),L"w",_SH_DENYNO);
    if(file) {
        const LONG returned=InterlockedCompareExchange(&g_hostReturns,0,0),entered=InterlockedCompareExchange(&g_hostPresents,0,0);
        fprintf(file,"{\"schema\":1,\"pid\":%lu,\"sample_ms\":%llu,\"phase\":\"%s\",\"installed\":%s,\"failed\":%s,\"pipe_wait_ms\":%llu,\"mh_initialize\":%d,\"mh_create\":%d,\"mh_enable\":%d,\"entered\":%ld,\"returned\":%ld,\"in_flight\":%ld,\"last_enter_ms\":%llu,\"last_return_ms\":%llu,\"last_present_ms\":%llu,\"last_hresult\":\"0x%08lX\"}\n",
            GetCurrentProcessId(),GetTickCount64(),g_phase,g_installed?"true":"false",g_failed?"true":"false",g_pipeWaitMs,g_initialize,g_create,g_enable,
            entered,returned,entered-returned,g_lastEnter.load(),g_lastReturn.load(),g_lastDuration.load(),(unsigned long)g_lastHresult.load());
        fclose(file);
        MoveFileExW(temporary.c_str(),g_metricsPath.c_str(),MOVEFILE_REPLACE_EXISTING);
    }
    // Kept for older harnesses; the per-PID journal/metrics above are authoritative.
    const std::wstring compatibilityTemporary=g_metricsPath+L".count.tmp";
    file=_wfsopen(compatibilityTemporary.c_str(),L"w",_SH_DENYNO);if(file){fprintf(file,"%ld\n",InterlockedCompareExchange(&g_hostPresents,0,0));fclose(file);MoveFileExW(compatibilityTemporary.c_str(),g_compatibilityPath.c_str(),MOVEFILE_REPLACE_EXISTING);}
}
static bool WaitForVinputPipe() {
    wchar_t pipe[80];swprintf_s(pipe,L"\\\\.\\pipe\\vinput_%lu",GetCurrentProcessId());
    const ULONGLONG began=GetTickCount64();
    while(GetTickCount64()-began<12000) {
        if(WaitNamedPipeW(pipe,50)||GetLastError()==ERROR_SEM_TIMEOUT) {
            g_pipeWaitMs=GetTickCount64()-began;Journal("PIPE_READY waited_ms=%llu",g_pipeWaitMs);return true;
        }
        Sleep(10);
    }
    g_pipeWaitMs=GetTickCount64()-began;Journal("PIPE_TIMEOUT waited_ms=%llu error=%lu",g_pipeWaitMs,GetLastError());return false;
}
static void Failed(const char* phase) {g_phase=phase;g_failed=true;Journal("INSTALL_FAILED phase=%s",phase);Publish();}
static DWORD WINAPI Init(LPVOID) {
    wchar_t modulePath[MAX_PATH]={};HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&Init,&self);
    if(!GetModuleFileNameW(self,modulePath,MAX_PATH))return 1;
    g_compatibilityPath=std::wstring(modulePath)+L".presents.txt";
    const std::wstring prefix=std::wstring(modulePath)+L".host."+std::to_wstring(GetCurrentProcessId());
    g_journalPath=prefix+L".log";g_metricsPath=prefix+L".json";
    Journal("INIT_START pid=%lu",GetCurrentProcessId());Publish();
    VINPUT_INIT();  // compiled in only when VINPUT_COMPILE_IN is defined
    // The pipe is created only after vinput's real DXGI-hook initialization finishes.
    // A fixed sleep cannot establish that ordering on a busy graphics driver.
    g_phase="waiting_for_vinput_pipe";Publish();
    if(!WaitForVinputPipe()){Failed("vinput_pipe_timeout");for(;;){Publish();Sleep(100);} }
    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"host_dummy";
    RegisterClassW(&wc);
    HWND h = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    if(!h){Failed("dummy_window");for(;;){Publish();Sleep(100);} }
    DXGI_SWAP_CHAIN_DESC d = {}; d.BufferCount = 1; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow = h; d.SampleDesc.Count = 1; d.Windowed = TRUE;
    IDXGISwapChain* sc=nullptr; ID3D11Device* dev=nullptr; ID3D11DeviceContext* ctx=nullptr;
    g_phase="creating_dummy_device";Journal("DUMMY_CREATE_BEGIN");Publish();
    const HRESULT created=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&sc,&dev,nullptr,&ctx);
    Journal("DUMMY_CREATE_END hr=0x%08lX",(unsigned long)created);
    if (SUCCEEDED(created)&&sc&&dev&&ctx) {
        void* present = (*(void***)sc)[8];
        g_phase="installing_host_hook";Publish();
        g_initialize=(int)MH_Initialize();Journal("MH_INITIALIZE result=%d",g_initialize);
        if(g_initialize!=MH_OK&&g_initialize!=MH_ERROR_ALREADY_INITIALIZED)Failed("minhook_initialize");
        else {
            g_create=(int)MH_CreateHook(present,(void*)&HostPresent,(void**)&o_Present);
            Journal("MH_CREATE result=%d present=%p original=%p",g_create,present,(void*)o_Present);
            if(g_create!=MH_OK||!o_Present)Failed("minhook_create");
            else {
                g_enable=(int)MH_EnableHook(present);Journal("MH_ENABLE result=%d",g_enable);
                if(g_enable!=MH_OK)Failed("minhook_enable");
                else {g_installed=true;g_phase="installed";Journal("INSTALL_READY");Publish();}
            }
        }
    }else Failed("dummy_device");
    if(ctx)ctx->Release();if(dev)dev->Release();if(sc)sc->Release();
    DestroyWindow(h);
    for (;;) { Publish(); Sleep(100); }
}
BOOL APIENTRY DllMain(HMODULE m, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(m); CloseHandle(CreateThread(nullptr, 0, Init, nullptr, 0, nullptr)); }
    return TRUE;
}
