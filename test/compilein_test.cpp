// compilein_test.cpp - stands in for a host DLL (your own tool or mod) that (1) links its OWN MinHook and hooks Present like
// a typical overlay does, and (2) compiles vinput in behind VINPUT_COMPILE_IN.
// Proves: no duplicate symbols at link time, both MinHook instances coexist, both Present hooks chain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include "MinHook.h"   // the host's own, un-renamed MinHook
#include "vinput.h"
#pragma comment(lib, "d3d11.lib")

static volatile LONG g_hostPresents = 0;
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
static PFN_Present o_Present;
static HRESULT STDMETHODCALLTYPE HostPresent(IDXGISwapChain* s, UINT a, UINT b) { InterlockedIncrement(&g_hostPresents); return o_Present(s, a, b); }
extern "C" __declspec(dllexport) LONG host_present_count() { return g_hostPresents; }

static DWORD WINAPI Init(LPVOID) {
    VINPUT_INIT();  // compiled in only when VINPUT_COMPILE_IN is defined
    Sleep(1500);    // let vinput hook Present FIRST, so the host's MinHook has to hook vinput's jmp (worst case)
    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"host_dummy";
    RegisterClassW(&wc);
    HWND h = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC d = {}; d.BufferCount = 1; d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow = h; d.SampleDesc.Count = 1; d.Windowed = TRUE;
    IDXGISwapChain* sc; ID3D11Device* dev; ID3D11DeviceContext* ctx;
    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx))) {
        void* present = (*(void***)sc)[8];
        MH_Initialize();
        MH_CreateHook(present, (void*)&HostPresent, (void**)&o_Present);
        MH_EnableHook(present);
        ctx->Release(); dev->Release(); sc->Release();
    }
    DestroyWindow(h);
    // publish the host-side Present count so a test can see the host's own hook still fires
    wchar_t path[MAX_PATH]; HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Init, &self);
    GetModuleFileNameW(self, path, MAX_PATH); wcscat_s(path, L".presents.txt");
    for (;;) { FILE* f = nullptr; _wfopen_s(&f, path, L"w"); if (f) { fprintf(f, "%ld\n", g_hostPresents); fclose(f); } Sleep(500); }
}
BOOL APIENTRY DllMain(HMODULE m, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(m); CloseHandle(CreateThread(nullptr, 0, Init, nullptr, 0, nullptr)); }
    return TRUE;
}
