// vi_cap12.h - native D3D12 back-buffer capture for vinput. Included INSIDE vinput_core.cpp's anonymous
// namespace (uses Log, ConvertRow, Typed, g_capPix/W/H/Err, HookAddr).
//
// Flow (game thread, inside the Present hook, only when a capture is requested):
//   queue  = a DIRECT queue seen by our ExecuteCommandLists hook on the back buffer's device, preferring the
//            presenting thread (installed by the monitor thread on the first D3D12 frame). The cheaper
//            swapchain->GetDevice(ID3D12CommandQueue) is tried first but FAILS on flip-model swapchains (MEASURED).
//   record = barrier PRESENT->COPY_SOURCE, CopyTextureRegion(back buffer[GetCurrentBackBufferIndex]) -> our
//            READBACK buffer, barrier back to PRESENT; ExecuteCommandLists on the game's queue; Signal(our fence)
//   NO wait on the game thread: the requester thread waits on the fence, maps and converts.
// The game is never stalled; it only pays for recording one copy. We never hold a back-buffer reference
// past the Present hook, so ResizeBuffers is never blocked; our objects are released on ResizeBuffers,
// on device removal and whenever the game's device changes.

std::mutex g_c12Mx;
struct Cap12 {
    ID3D12Device* dev = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceVal = 0;
    ID3D12Resource* rb = nullptr; UINT64 rbSize = 0;
    bool pending = false; UINT64 waitVal = 0;
    UINT w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    bool stale = false;          // release once the in-flight readback is done (resize / device change)
    long releases = 0, leaks = 0, submits = 0;
} g_c12;

// The swapchain does NOT hand out its queue (GetDevice(ID3D12CommandQueue) fails on a flip-model D3D12 swapchain,
// MEASURED 2026-11-02), so the queue comes from an ExecuteCommandLists hook, installed by the monitor thread as soon
// as a D3D12 frame is seen. We keep the DIRECT queues seen (AddRef'd, at most 8) with the thread and time of their
// last submission, and pick the one on the back buffer's device, preferring the presenting thread, then the latest.
using PFN_ECL = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PFN_ECL o_ExecuteCommandLists = nullptr;
struct EclQ { ID3D12CommandQueue* q; DWORD tid; DWORD tick; };
std::mutex g_eclMx;
EclQ g_eclQs[8] = {}; int g_eclN = 0;
std::atomic<bool> g_eclWanted{false}, g_eclInstalled{false};
std::atomic<int> g_c12QueueSource{0};                    // 1 = swapchain GetDevice, 2 = ExecuteCommandLists hook
thread_local bool t_ourEcl = false;
void STDMETHODCALLTYPE h_ExecuteCommandLists(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (!t_ourEcl && q) {
        DWORD tid = GetCurrentThreadId(), now = GetTickCount();
        std::unique_lock<std::mutex> lk(g_eclMx, std::try_to_lock);   // never make the game wait on us
        if (lk.owns_lock()) {
            int i = 0;
            for (; i < g_eclN; i++) if (g_eclQs[i].q == q) { g_eclQs[i].tid = tid; g_eclQs[i].tick = now; break; }
            if (i == g_eclN && g_eclN < 8) {
                D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
                if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) { q->AddRef(); g_eclQs[g_eclN++] = {q, tid, now}; }
            }
        }
    }
    o_ExecuteCommandLists(q, n, lists);
}

// Releases our objects. Caller holds g_c12Mx. If a readback is still on the GPU we wait (bounded); if the
// GPU never finishes we LEAK instead of releasing something in use (releasing it would remove the device).
void Release12Locked(const char* why) {
    if (g_c12.pending && g_c12.fence) {
        if (g_c12.fence->GetCompletedValue() < g_c12.waitVal) {
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (ev && SUCCEEDED(g_c12.fence->SetEventOnCompletion(g_c12.waitVal, ev))) WaitForSingleObject(ev, 1000);
            if (ev) CloseHandle(ev);
        }
        UINT64 done = g_c12.fence->GetCompletedValue();
        if (done != UINT64_MAX && done < g_c12.waitVal) {
            long rel = g_c12.releases, lk = g_c12.leaks + 1, sb = g_c12.submits;
            Log("d3d12 capture: release (%s) while readback still in flight -> leaking our objects instead", why);
            g_c12 = Cap12{};
            g_c12.releases = rel; g_c12.leaks = lk; g_c12.submits = sb;
            return;
        }
    }
    long rel = g_c12.releases + 1, lk = g_c12.leaks, sb = g_c12.submits;
    if (g_c12.rb) g_c12.rb->Release();
    if (g_c12.list) g_c12.list->Release();
    if (g_c12.alloc) g_c12.alloc->Release();
    if (g_c12.fence) g_c12.fence->Release();
    if (g_c12.dev) g_c12.dev->Release();
    bool had = g_c12.dev != nullptr;
    g_c12 = Cap12{};
    g_c12.releases = had ? rel : rel - 1; g_c12.leaks = lk; g_c12.submits = sb;
    if (had) Log("d3d12 capture: released our objects (%s)", why);
}
// Called from the ResizeBuffers hooks and on device removal. Never blocks the game behind a requester.
void Drop12(const char* why) {
    std::unique_lock<std::mutex> lk(g_c12Mx, std::try_to_lock);
    if (!lk.owns_lock()) { g_c12.stale = true; return; }   // a requester is mapping: it releases when done
    if (g_c12.dev) Release12Locked(why);
}

ID3D12CommandQueue* QueueFor12(IDXGISwapChain* sc, ID3D12Device* bbDev) {
    ID3D12CommandQueue* q = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D12CommandQueue), (void**)&q)) && q) { g_c12QueueSource = 1; return q; }
    g_eclWanted = true;
    if (!g_eclInstalled) return nullptr;
    DWORD tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lk(g_eclMx);
    int best = -1; int bestScore = -1; DWORD bestTick = 0;
    for (int i = 0; i < g_eclN; i++) {
        ID3D12Device* d = nullptr;
        if (FAILED(g_eclQs[i].q->GetDevice(__uuidof(ID3D12Device), (void**)&d)) || !d) continue;
        bool same = d == bbDev; d->Release();
        if (!same) continue;
        int score = g_eclQs[i].tid == tid ? 1 : 0;
        if (score > bestScore || (score == bestScore && (LONG)(g_eclQs[i].tick - bestTick) > 0)) { best = i; bestScore = score; bestTick = g_eclQs[i].tick; }
    }
    if (best < 0) return nullptr;
    q = g_eclQs[best].q; q->AddRef(); g_c12QueueSource = 2;
    return q;
}
void ReleaseEclQueues() {
    std::lock_guard<std::mutex> lk(g_eclMx);
    for (int i = 0; i < g_eclN; i++) g_eclQs[i].q->Release();
    g_eclN = 0;
}

// Game thread. Returns true when a readback was submitted (requester must call Finish12).
bool Begin12(IDXGISwapChain* sc, std::string& err) {
    IDXGISwapChain3* sc3 = nullptr;
    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) || !sc3) { err = "no IDXGISwapChain3"; return false; }
    UINT idx = sc3->GetCurrentBackBufferIndex();
    sc3->Release(); sc3 = nullptr;
    if (FAILED(sc->GetBuffer(idx, __uuidof(ID3D12Resource), (void**)&bb)) || !bb) { err = "GetBuffer(ID3D12Resource) failed"; return false; }
    ID3D12Device* dev = nullptr;
    if (FAILED(bb->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) { bb->Release(); err = "back buffer has no device"; return false; }
    ID3D12CommandQueue* q = QueueFor12(sc, dev);
    if (!q) { bb->Release(); dev->Release(); g_capRetryable = true; err = g_eclInstalled ? "no DIRECT command queue on the swapchain's device seen yet" : "command-queue hook not installed yet"; return false; }
    bool ok = false;
    std::lock_guard<std::mutex> lk(g_c12Mx);
    do {
        if (dev->GetDeviceRemovedReason() != S_OK) { if (g_c12.dev) Release12Locked("device removed"); ReleaseEclQueues(); err = "device removed"; break; }
        if (g_c12.pending) {
            UINT64 done = g_c12.fence ? g_c12.fence->GetCompletedValue() : 0;
            if (done == UINT64_MAX) { Release12Locked("device removed (fence)"); err = "device removed"; break; }
            if (done < g_c12.waitVal) { err = "previous readback still in flight"; break; }
            g_c12.pending = false;
        }
        if (g_c12.stale || (g_c12.dev && g_c12.dev != dev)) Release12Locked(g_c12.stale ? "resize" : "device changed");
        if (!g_c12.dev) {
            D3D12_COMMAND_LIST_TYPE t = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(dev->CreateCommandAllocator(t, __uuidof(ID3D12CommandAllocator), (void**)&g_c12.alloc)) ||
                FAILED(dev->CreateCommandList(0, t, g_c12.alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_c12.list)) ||
                FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_c12.fence))) {
                err = "creating allocator/list/fence failed"; g_c12.dev = dev; dev->AddRef(); Release12Locked("create failed"); break;
            }
            g_c12.list->Close();
            g_c12.dev = dev; dev->AddRef();
            Log("d3d12 capture: created allocator/list/fence on device %p (queue source %d)", dev, (int)g_c12QueueSource);
        }
        D3D12_RESOURCE_DESC rd = bb->GetDesc();
        if(!CaptureDimensionsAllowed(rd.Width,rd.Height)){err=g_capErr;break;}
        if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd.SampleDesc.Count != 1) { err = "back buffer is not a single-sample 2D texture"; break; }
        BYTE probe[16] = {0}, out[8];
        if (!ConvertRow(rd.Format, probe, out, 1)) { char b[64]; sprintf_s(b, "unsupported backbuffer format %d", (int)rd.Format); err = b; break; }
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
        dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
        if (!g_c12.rb || g_c12.rbSize < total) {
            if (g_c12.rb) { g_c12.rb->Release(); g_c12.rb = nullptr; g_c12.rbSize = 0; }
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1;
            bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&g_c12.rb))) {
                g_c12.rb = nullptr; err = "readback buffer allocation failed"; break;
            }
            g_c12.rbSize = total;
        }
        if (FAILED(g_c12.alloc->Reset()) || FAILED(g_c12.list->Reset(g_c12.alloc, nullptr))) { err = "command list reset failed"; break; }
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = bb;
        b.Transition.Subresource = 0; b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        g_c12.list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = g_c12.rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
        src.pResource = bb; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
        g_c12.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        g_c12.list->ResourceBarrier(1, &b);
        if (FAILED(g_c12.list->Close())) { err = "command list close failed"; break; }
        ID3D12CommandList* cl = g_c12.list;
        t_ourEcl = true; q->ExecuteCommandLists(1, &cl); t_ourEcl = false;
        g_c12.waitVal = ++g_c12.fenceVal;
        if (FAILED(q->Signal(g_c12.fence, g_c12.waitVal))) { err = "queue Signal failed"; break; }
        g_c12.pending = true; g_c12.submits++;
        g_c12.w = (UINT)rd.Width; g_c12.h = rd.Height; g_c12.fmt = rd.Format; g_c12.fp = fp;
        ok = true;
    } while (false);
    if (bb) bb->Release();
    if (sc3) sc3->Release();
    dev->Release(); q->Release();
    return ok;
}

// Requester thread: wait for our fence (not the game), map, convert into g_capPix.
bool Finish12(DWORD timeoutMs, std::string& err) {
    ID3D12Fence* fence = nullptr; UINT64 val = 0;
    {
        std::lock_guard<std::mutex> lk(g_c12Mx);
        if (!g_c12.pending || !g_c12.fence) { err = "no readback pending"; return false; }
        fence = g_c12.fence; fence->AddRef(); val = g_c12.waitVal;
    }
    if (fence->GetCompletedValue() < val) {
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ev && SUCCEEDED(fence->SetEventOnCompletion(val, ev))) WaitForSingleObject(ev, timeoutMs);
        if (ev) CloseHandle(ev);
    }
    UINT64 done = fence->GetCompletedValue();
    fence->Release();
    std::lock_guard<std::mutex> lk(g_c12Mx);
    if (done == UINT64_MAX) { Release12Locked("device removed (readback)"); err = "device removed"; return false; }
    if (done < val) { err = "GPU readback did not complete within timeout"; return false; }
    if (!g_c12.pending || g_c12.waitVal != val || !g_c12.rb) { err = "readback was dropped"; return false; }
    bool ok = false;
    void* p = nullptr;
    HRESULT mhr = g_c12.rb->Map(0, nullptr, &p);
    if (SUCCEEDED(mhr) && p) {
        g_capPix.resize((size_t)g_c12.w * g_c12.h * 4);
        ok = true;
        const BYTE* base = (const BYTE*)p + g_c12.fp.Offset;
        for (UINT y = 0; y < g_c12.h && ok; y++) ok = ConvertRow(g_c12.fmt, base + (size_t)y * g_c12.fp.Footprint.RowPitch, g_capPix.data() + (size_t)y * g_c12.w * 4, g_c12.w);
        D3D12_RANGE none{0, 0}; g_c12.rb->Unmap(0, &none);
        if (ok) { g_capW = g_c12.w; g_capH = g_c12.h; g_capFmt = g_c12.fmt; }
        else { g_capPix.clear(); err = "format conversion failed"; }
    } else { char b[64]; sprintf_s(b, "Map(readback) failed hr=0x%08X", (unsigned)mhr); err = b; }
    g_c12.pending = false;
    if (g_c12.stale) Release12Locked("resize (deferred)");
    return ok;
}

// Monitor thread: install the ExecuteCommandLists hook only when a D3D12 swapchain did not hand out its queue.
void TryHookEcl() {
    if (!g_eclWanted || g_eclInstalled) return;
    HMODULE h = GetModuleHandleW(L"d3d12.dll"); if (!h) return;
    using PFN_Create = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    PFN_Create create = (PFN_Create)GetProcAddress(h, "D3D12CreateDevice");
    if (!create) return;
    g_eclInstalled = true;   // one attempt
    ID3D12Device* dev = nullptr; ID3D12CommandQueue* q = nullptr;
    if (SUCCEEDED(create(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)) && dev) {
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) && q) {
            void** vt = *(void***)q;
            HookAddr("ID3D12CommandQueue::ExecuteCommandLists", vt[10], (void*)&h_ExecuteCommandLists, (void**)&o_ExecuteCommandLists);
            q->Release();
        }
        dev->Release();
    } else g_hooksFail += "ID3D12CommandQueue::ExecuteCommandLists(no device) ";
    Log("ExecuteCommandLists fallback hook %s", o_ExecuteCommandLists ? "installed" : "FAILED");
}
