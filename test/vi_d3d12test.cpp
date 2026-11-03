// vi_d3d12test.cpp - synthetic D3D12 capture target for vinput. Flip-model swapchain on a DIRECT queue; every frame
// the CPU writes vi_pattern.h (frame counter + colour bars) into an upload buffer and the GPU copies it into the
// current back buffer. Handles WM_SIZE with ResizeBuffers (all back-buffer refs released first).
//   vi_d3d12test.exe <log> <x> <y> <w> <h> [--fmt bgra|rgba|r10|f16] [--vsync 0|1]
// Never activates: WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>
#include "vi_pattern.h"
#include "vi_capturetiming.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

static FILE* g_log; static HWND g_hwnd; static volatile bool g_resize = false;
static void L(const char* f, ...) { if (!g_log) return; va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log); }
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if(vi_capture_timing::Message(m,w))return 0;
    if (m == WM_SIZE) g_resize = true;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
#define CK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { L("FAIL %s hr=0x%08X line %d", #x, (unsigned)hr_, __LINE__); return 2; } } while (0)

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_log = _wfsopen(argc > 1 ? argv[1] : L"vi_d3d12test.log", L"w", _SH_DENYNO);
    int x = argc > 5 ? _wtoi(argv[2]) : 100, y = argc > 5 ? _wtoi(argv[3]) : 100, w = argc > 5 ? _wtoi(argv[4]) : 800, hgt = argc > 5 ? _wtoi(argv[5]) : 450;
    vipat::Fmt pf = vipat::BGRA8; DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM; UINT vsync = 1;
    for (int i = 6; i + 1 < argc; i++) {
        if (!wcscmp(argv[i], L"--fmt")) {
            const wchar_t* f = argv[++i];
            if (!wcscmp(f, L"rgba")) { pf = vipat::RGBA8; fmt = DXGI_FORMAT_R8G8B8A8_UNORM; }
            else if (!wcscmp(f, L"r10")) { pf = vipat::R10G10B10A2; fmt = DXGI_FORMAT_R10G10B10A2_UNORM; }
            else if (!wcscmp(f, L"f16")) { pf = vipat::RGBA16F; fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; }
        } else if (!wcscmp(argv[i], L"--vsync")) vsync = (UINT)_wtoi(argv[++i]);
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_d3d12test"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput d3d12 capture target", WS_OVERLAPPEDWINDOW, x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    ID3D12Device* dev = nullptr; IDXGIFactory4* fac = nullptr; ID3D12CommandQueue* q = nullptr;
    CK(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&fac));
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev))) {
        IDXGIAdapter* warp = nullptr; CK(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp));
        CK(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)); warp->Release(); L("WARP");
    }
    D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CK(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q));
    const UINT N = 3;
    RECT rc; GetClientRect(g_hwnd, &rc);
    UINT cw = rc.right, ch = rc.bottom;
    DXGI_SWAP_CHAIN_DESC1 sd = {}; sd.Width = cw; sd.Height = ch; sd.Format = fmt; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = N; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc1 = nullptr; IDXGISwapChain3* sc = nullptr;
    CK(fac->CreateSwapChainForHwnd(q, g_hwnd, &sd, nullptr, nullptr, &sc1));
    CK(sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc)); sc1->Release();
    fac->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER);
    ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* cl = nullptr; ID3D12Fence* fe = nullptr; UINT64 fv = 0;
    CK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al));
    CK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl)); cl->Close();
    CK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe));
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto wait = [&]() { ++fv; q->Signal(fe, fv); if (fe->GetCompletedValue() < fv) { fe->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, 5000); } };
    ID3D12Resource* up = nullptr; UINT64 upSize = 0; BYTE* upMap = nullptr;
    L("START hwnd=%p fmt=%d client=%ux%u", g_hwnd, (int)fmt, cw, ch);
    UINT32 frame = 0; DWORD lastLog = 0; int resizes = 0;
    LARGE_INTEGER qpf, prevT{}; QueryPerformanceFrequency(&qpf); double maxMs = 0, worstMs = 0;
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) { wait(); L("EXIT frames=%u resizes=%d", frame, resizes); return 0; } TranslateMessage(&msg); DispatchMessageW(&msg); }
        if(vi_capture_timing::pendingReset){QueryPerformanceCounter(&prevT);maxMs=worstMs=0;lastLog=GetTickCount();L("CAPTURE_TIMING_START token=%llu frame=%u",(unsigned long long)vi_capture_timing::pendingReset,frame);vi_capture_timing::pendingReset=0;}
        if (g_resize) {
            g_resize = false;
            GetClientRect(g_hwnd, &rc);
            if (rc.right > 0 && rc.bottom > 0 && ((UINT)rc.right != cw || (UINT)rc.bottom != ch)) {
                wait();   // no back-buffer refs are held between frames
                HRESULT hr = sc->ResizeBuffers(0, rc.right, rc.bottom, DXGI_FORMAT_UNKNOWN, 0);
                L("RESIZE %ldx%ld hr=0x%08X", rc.right, rc.bottom, (unsigned)hr);
                if (FAILED(hr)) return 3;
                cw = rc.right; ch = rc.bottom; resizes++; prevT.QuadPart = 0;   // a resize is not a capture stall
            }
        }
        UINT idx = sc->GetCurrentBackBufferIndex();
        ID3D12Resource* bb = nullptr; CK(sc->GetBuffer(idx, __uuidof(ID3D12Resource), (void**)&bb));
        D3D12_RESOURCE_DESC rd = bb->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT rows; UINT64 rb, total;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rb, &total);
        if (total > upSize) {
            if (up) { wait(); up->Release(); }
            D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            CK(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&up));
            D3D12_RANGE none = {0, 0}; CK(up->Map(0, &none, (void**)&upMap)); upSize = total;
        }
        for (UINT yy = 0; yy < rd.Height; yy++) vipat::Row(pf, (int)yy, (int)rd.Width, frame, upMap + fp.Offset + (size_t)yy * fp.Footprint.RowPitch);
        CK(al->Reset()); CK(cl->Reset(al, nullptr));
        D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = bb; b.Transition.Subresource = 0;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; cl->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
        dst.pResource = bb; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
        src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT; cl->ResourceBarrier(1, &b);
        CK(cl->Close());
        ID3D12CommandList* lists[] = {cl}; q->ExecuteCommandLists(1, lists);
        bb->Release();
        HRESULT hr = sc->Present(vsync, 0);
        if (FAILED(hr)) { L("PRESENT_FAIL hr=0x%08X removed=0x%08X", (unsigned)hr, (unsigned)dev->GetDeviceRemovedReason()); return 4; }
        wait();   // simple: one frame in flight (the upload buffer is rewritten next frame)
        ++frame;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (prevT.QuadPart) { double ms = (now.QuadPart - prevT.QuadPart) * 1000.0 / qpf.QuadPart; if (ms > maxMs) maxMs = ms; if (ms > worstMs) worstMs = ms; }
        prevT = now;
        if(vi_capture_timing::pendingReport){L("CAPTURE_TIMING_END token=%llu maxms=%.1f",(unsigned long long)vi_capture_timing::pendingReport,worstMs);vi_capture_timing::pendingReport=0;}
        if (GetTickCount() - lastLog > 1000) { lastLog = GetTickCount(); L("FRAME %u maxms=%.1f worstms=%.1f", frame, maxMs, worstMs); maxMs = 0; }
        Sleep(4);   // bounded render rate (an offscreen flip-model window is never vsync-throttled)
    }
}
