// vi_capvk.h - native Vulkan capture for vinput. Included INSIDE vinput_core.cpp's anonymous namespace.
//
// vulkan-1.dll is never loaded by vinput and never linked; hooks go in only once the game has it loaded.
// Hooked loader exports (inline): vkCreateDevice, vkDestroyDevice, vkGetDeviceQueue, vkGetDeviceQueue2,
// vkCreateSwapchainKHR, vkDestroySwapchainKHR, vkQueuePresentKHR, vkGetDeviceProcAddr. The vkGetDeviceProcAddr
// hook hands out our wrappers for the device-level ones, so games that resolve through it (volk-style) are
// covered too. Every wrapper calls the loader trampoline, which dispatches through the handle.
//
// LIMIT (by design, see README): the device, its queues and the swapchain must be CREATED AFTER injection
// (we need the physical device, the queue family and the swapchain format/extent/images, and we add
// TRANSFER_SRC to the swapchain usage). Inject before the game creates its VkDevice (or force a swapchain
// recreation after injection for a game that resolved vkGetDeviceProcAddr late).
//
// Capture (game thread, inside vkQueuePresentKHR, only on request): one command buffer waits on the present's
// wait semaphores, transitions the presented image PRESENT_SRC -> TRANSFER_SRC, copies it to a host-visible
// buffer, transitions it back, and signals our per-image semaphore + our fence; the present is then issued
// waiting on our semaphore. The requester thread waits on the fence; the game thread never waits.

using PFN_vkVoid = PFN_vkVoidFunction;
PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkDestroyDevice o_vkDestroyDevice = nullptr;
PFN_vkGetDeviceQueue o_vkGetDeviceQueue = nullptr;
PFN_vkGetDeviceQueue2 o_vkGetDeviceQueue2 = nullptr;
PFN_vkCreateSwapchainKHR o_vkCreateSwapchainKHR = nullptr;
PFN_vkDestroySwapchainKHR o_vkDestroySwapchainKHR = nullptr;
PFN_vkQueuePresentKHR o_vkQueuePresentKHR = nullptr;
PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;
std::atomic<bool> g_vkHooked{false};
std::atomic<bool> g_vkEnabled{true};   // config vk=off: hooks stay pass-through, no capture / no usage change

struct VkLoaderFns {
#define VKF(n) PFN_##n n = nullptr;
    VKF(vkCreateCommandPool) VKF(vkDestroyCommandPool) VKF(vkAllocateCommandBuffers) VKF(vkResetCommandPool)
    VKF(vkCreateFence) VKF(vkDestroyFence) VKF(vkWaitForFences) VKF(vkResetFences) VKF(vkGetFenceStatus)
    VKF(vkBeginCommandBuffer) VKF(vkEndCommandBuffer) VKF(vkCmdPipelineBarrier) VKF(vkCmdCopyImageToBuffer)
    VKF(vkQueueSubmit) VKF(vkCreateBuffer) VKF(vkDestroyBuffer) VKF(vkGetBufferMemoryRequirements)
    VKF(vkAllocateMemory) VKF(vkFreeMemory) VKF(vkBindBufferMemory) VKF(vkMapMemory) VKF(vkUnmapMemory)
    VKF(vkInvalidateMappedMemoryRanges) VKF(vkCreateSemaphore) VKF(vkDestroySemaphore)
    VKF(vkGetSwapchainImagesKHR) VKF(vkGetPhysicalDeviceMemoryProperties)
#undef VKF
    bool ok = false;
} g_vkf;

template <class T> uint64_t VkKey(T h) {
    if constexpr (std::is_pointer<T>::value) return (uint64_t)(uintptr_t)h; else return (uint64_t)h;
}
struct VkScInfo { VkDevice dev = nullptr; VkFormat fmt = VK_FORMAT_UNDEFINED; VkExtent2D ext{}; uint32_t layers = 1; bool transferSrc = false; HWND hwnd = nullptr; std::vector<VkImage> imgs; std::vector<VkSemaphore> sems; };
std::mutex g_vkMx;
std::map<VkDevice, VkPhysicalDevice> g_vkDevs;
std::map<VkQueue, std::pair<VkDevice, uint32_t>> g_vkQueues;
std::map<uint64_t, VkScInfo> g_vkScs;
std::map<uint64_t, HWND> g_vkSurfaces;   // VkSurfaceKHR -> window (vkCreateWin32SurfaceKHR)
std::atomic<long> g_vkUsageAdded{0}, g_vkPresents{0}, g_vkPresentsOther{0};
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
struct VkCap {
    VkDevice dev = nullptr; uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE; VkCommandBuffer cb = nullptr; VkFence fence = VK_NULL_HANDLE;
    VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize size = 0; void* map = nullptr; bool coherent = true;
    bool pending = false; uint64_t seq = 0; uint32_t w = 0, h = 0; VkFormat fmt = VK_FORMAT_UNDEFINED;
} g_vkc;
uint64_t g_vkSeq = 0;
std::atomic<long> g_vkSubmits{0}, g_vkReleases{0};

bool VkResolve() {
    if (g_vkf.ok) return true;
    HMODULE h = GetModuleHandleW(L"vulkan-1.dll"); if (!h) return false;
#define VKR(n) g_vkf.n = (PFN_##n)GetProcAddress(h, #n); if (!g_vkf.n) return false;
    VKR(vkCreateCommandPool) VKR(vkDestroyCommandPool) VKR(vkAllocateCommandBuffers) VKR(vkResetCommandPool)
    VKR(vkCreateFence) VKR(vkDestroyFence) VKR(vkWaitForFences) VKR(vkResetFences) VKR(vkGetFenceStatus)
    VKR(vkBeginCommandBuffer) VKR(vkEndCommandBuffer) VKR(vkCmdPipelineBarrier) VKR(vkCmdCopyImageToBuffer)
    VKR(vkQueueSubmit) VKR(vkCreateBuffer) VKR(vkDestroyBuffer) VKR(vkGetBufferMemoryRequirements)
    VKR(vkAllocateMemory) VKR(vkFreeMemory) VKR(vkBindBufferMemory) VKR(vkMapMemory) VKR(vkUnmapMemory)
    VKR(vkInvalidateMappedMemoryRanges) VKR(vkCreateSemaphore) VKR(vkDestroySemaphore)
    VKR(vkGetSwapchainImagesKHR) VKR(vkGetPhysicalDeviceMemoryProperties)
#undef VKR
    g_vkf.ok = true;
    return true;
}
// VkFormat -> the DXGI format ConvertRow understands (+ whether R and B must be swapped afterwards)
DXGI_FORMAT VkToDxgi(VkFormat f, bool* swapRB) {
    *swapRB = false;
    switch (f) {
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: *swapRB = true; return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
uint32_t VkBpp(VkFormat f) { return f == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 4; }

// caller holds g_vkMx
void VkReleaseCapLocked(const char* why) {
    if (!g_vkc.dev) return;
    if (g_vkc.pending && g_vkc.fence) {
        VkResult r = g_vkf.vkWaitForFences(g_vkc.dev, 1, &g_vkc.fence, VK_TRUE, 1000000000ULL);
        if (r == VK_TIMEOUT) { Log("vk capture: release (%s) while copy in flight -> leaking our objects", why); g_vkc = VkCap{}; return; }
    }
    VkDevice d = g_vkc.dev;
    if (g_vkc.map) g_vkf.vkUnmapMemory(d, g_vkc.mem);
    if (g_vkc.buf) g_vkf.vkDestroyBuffer(d, g_vkc.buf, nullptr);
    if (g_vkc.mem) g_vkf.vkFreeMemory(d, g_vkc.mem, nullptr);
    if (g_vkc.fence) g_vkf.vkDestroyFence(d, g_vkc.fence, nullptr);
    if (g_vkc.pool) g_vkf.vkDestroyCommandPool(d, g_vkc.pool, nullptr);   // frees the command buffer
    g_vkc = VkCap{};
    g_vkReleases++;
    Log("vk capture: released our objects (%s)", why);
}

// Game thread, inside vkQueuePresentKHR. On success *out is the present info to use (waiting on our semaphore).
bool BeginVk(VkQueue q, const VkPresentInfoKHR* pi, VkPresentInfoKHR* out, VkSemaphore* sigOut, std::string& err) {
    if (!g_vkEnabled) { err = "vk: capture disabled (config vk=off)"; return false; }
    if (!pi || pi->swapchainCount < 1 || !pi->pSwapchains || !pi->pImageIndices) { err = "vk: empty present"; return false; }
    if (!VkResolve()) { err = "vk: loader entry points not found"; return false; }
    std::lock_guard<std::mutex> lk(g_vkMx);
    auto qi = g_vkQueues.find(q);
    if (qi == g_vkQueues.end()) { err = "vk: present queue was obtained before vinput was injected (inject before the game creates its VkDevice)"; return false; }
    VkDevice dev = qi->second.first; uint32_t fam = qi->second.second;
    auto di = g_vkDevs.find(dev);
    if (di == g_vkDevs.end()) { err = "vk: device was created before vinput was injected"; return false; }
    auto si = g_vkScs.find(VkKey(pi->pSwapchains[0]));
    if (si == g_vkScs.end()) { err = "vk: swapchain was created before vinput was injected (resize the window to make the game recreate it)"; return false; }
    VkScInfo& sc = si->second;
    uint32_t idx = pi->pImageIndices[0];
    if (idx >= sc.imgs.size()) { err = "vk: image index out of range"; return false; }
    if (!sc.transferSrc) { err = "vk: swapchain images lack TRANSFER_SRC usage (driver refused to add it)"; return false; }
    bool swapRB; if (VkToDxgi(sc.fmt, &swapRB) == DXGI_FORMAT_UNKNOWN) { char b[64]; sprintf_s(b, "vk: unsupported swapchain format %d", (int)sc.fmt); err = b; return false; }
    if (g_vkc.pending) {
        VkResult s = g_vkf.vkGetFenceStatus(g_vkc.dev, g_vkc.fence);
        if (s == VK_NOT_READY) { err = "vk: previous readback still in flight"; return false; }
        if (s != VK_SUCCESS) { VkReleaseCapLocked("device lost"); err = "vk: device lost"; return false; }
        g_vkc.pending = false;
    }
    if (g_vkc.dev && (g_vkc.dev != dev || g_vkc.family != fam)) VkReleaseCapLocked("device/queue family changed");
    if (!g_vkc.dev) {
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pc.queueFamilyIndex = fam; pc.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        g_vkc.dev = dev; g_vkc.family = fam;
        if (g_vkf.vkCreateCommandPool(dev, &pc, nullptr, &g_vkc.pool) != VK_SUCCESS) { err = "vk: vkCreateCommandPool failed"; VkReleaseCapLocked("create failed"); return false; }
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = g_vkc.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        if (g_vkf.vkAllocateCommandBuffers(dev, &ai, &g_vkc.cb) != VK_SUCCESS) { err = "vk: vkAllocateCommandBuffers failed"; VkReleaseCapLocked("create failed"); return false; }
        if (g_vkf.vkCreateFence(dev, &fc, nullptr, &g_vkc.fence) != VK_SUCCESS) { err = "vk: vkCreateFence failed"; VkReleaseCapLocked("create failed"); return false; }
        Log("vk capture: created pool/cb/fence on device %p family %u", dev, fam);
    }
    VkDeviceSize need = (VkDeviceSize)sc.ext.width * sc.ext.height * VkBpp(sc.fmt);
    if (g_vkc.size < need) {
        if (g_vkc.map) { g_vkf.vkUnmapMemory(dev, g_vkc.mem); g_vkc.map = nullptr; }
        if (g_vkc.buf) { g_vkf.vkDestroyBuffer(dev, g_vkc.buf, nullptr); g_vkc.buf = VK_NULL_HANDLE; }
        if (g_vkc.mem) { g_vkf.vkFreeMemory(dev, g_vkc.mem, nullptr); g_vkc.mem = VK_NULL_HANDLE; }
        g_vkc.size = 0;
        VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bc.size = need; bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (g_vkf.vkCreateBuffer(dev, &bc, nullptr, &g_vkc.buf) != VK_SUCCESS) { err = "vk: vkCreateBuffer failed"; return false; }
        VkMemoryRequirements mr; g_vkf.vkGetBufferMemoryRequirements(dev, g_vkc.buf, &mr);
        VkPhysicalDeviceMemoryProperties mp; g_vkf.vkGetPhysicalDeviceMemoryProperties(di->second, &mp);
        int best = -1, bestScore = -1;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            if (!(mr.memoryTypeBits & (1u << i))) continue;
            VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
            if (!(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
            int score = ((f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 2 : 0) + ((f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0);
            if (score > bestScore) { bestScore = score; best = (int)i; }
        }
        if (best < 0) { g_vkf.vkDestroyBuffer(dev, g_vkc.buf, nullptr); g_vkc.buf = VK_NULL_HANDLE; err = "vk: no host-visible memory type"; return false; }
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = mr.size; ma.memoryTypeIndex = (uint32_t)best;
        if (g_vkf.vkAllocateMemory(dev, &ma, nullptr, &g_vkc.mem) != VK_SUCCESS) { g_vkf.vkDestroyBuffer(dev, g_vkc.buf, nullptr); g_vkc.buf = VK_NULL_HANDLE; err = "vk: vkAllocateMemory failed"; return false; }
        if (g_vkf.vkBindBufferMemory(dev, g_vkc.buf, g_vkc.mem, 0) != VK_SUCCESS || g_vkf.vkMapMemory(dev, g_vkc.mem, 0, VK_WHOLE_SIZE, 0, &g_vkc.map) != VK_SUCCESS) {
            g_vkf.vkDestroyBuffer(dev, g_vkc.buf, nullptr); g_vkf.vkFreeMemory(dev, g_vkc.mem, nullptr); g_vkc.buf = VK_NULL_HANDLE; g_vkc.mem = VK_NULL_HANDLE; g_vkc.map = nullptr;
            err = "vk: bind/map failed"; return false;
        }
        g_vkc.coherent = (mp.memoryTypes[best].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        g_vkc.size = need;
    }
    if (sc.sems.size() < sc.imgs.size()) sc.sems.resize(sc.imgs.size(), VK_NULL_HANDLE);
    if (!sc.sems[idx]) {
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (g_vkf.vkCreateSemaphore(dev, &sci, nullptr, &sc.sems[idx]) != VK_SUCCESS) { sc.sems[idx] = VK_NULL_HANDLE; err = "vk: vkCreateSemaphore failed"; return false; }
    }
    VkImage img = sc.imgs[idx];
    if (g_vkf.vkResetFences(dev, 1, &g_vkc.fence) != VK_SUCCESS || g_vkf.vkResetCommandPool(dev, g_vkc.pool, 0) != VK_SUCCESS) { err = "vk: reset failed"; return false; }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g_vkf.vkBeginCommandBuffer(g_vkc.cb, &bi) != VK_SUCCESS) { err = "vk: vkBeginCommandBuffer failed"; return false; }
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = 0; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.image = img;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    g_vkf.vkCmdPipelineBarrier(g_vkc.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkBufferImageCopy rg{}; rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; rg.imageExtent = {sc.ext.width, sc.ext.height, 1};
    g_vkf.vkCmdCopyImageToBuffer(g_vkc.cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_vkc.buf, 1, &rg);
    ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; ib.dstAccessMask = 0;
    ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; bb.buffer = g_vkc.buf; bb.offset = 0; bb.size = VK_WHOLE_SIZE;
    g_vkf.vkCmdPipelineBarrier(g_vkc.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &bb, 1, &ib);
    if (g_vkf.vkEndCommandBuffer(g_vkc.cb) != VK_SUCCESS) { err = "vk: vkEndCommandBuffer failed"; return false; }
    std::vector<VkPipelineStageFlags> stages(pi->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si2{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si2.waitSemaphoreCount = pi->waitSemaphoreCount; si2.pWaitSemaphores = pi->pWaitSemaphores; si2.pWaitDstStageMask = stages.empty() ? nullptr : stages.data();
    si2.commandBufferCount = 1; si2.pCommandBuffers = &g_vkc.cb;
    si2.signalSemaphoreCount = 1; si2.pSignalSemaphores = &sc.sems[idx];
    VkResult r = g_vkf.vkQueueSubmit(q, 1, &si2, g_vkc.fence);
    if (r != VK_SUCCESS) { char b[64]; sprintf_s(b, "vk: vkQueueSubmit failed %d", (int)r); err = b; if (r == VK_ERROR_DEVICE_LOST) VkReleaseCapLocked("device lost"); return false; }
    *sigOut = sc.sems[idx];
    *out = *pi; out->waitSemaphoreCount = 1; out->pWaitSemaphores = sigOut;
    g_vkc.pending = true; g_vkc.seq = ++g_vkSeq; g_vkc.w = sc.ext.width; g_vkc.h = sc.ext.height; g_vkc.fmt = sc.fmt;
    g_vkSubmits++;
    return true;
}

// Requester thread: wait on OUR fence, convert the host-visible copy into g_capPix.
bool FinishVk(DWORD timeoutMs, std::string& err) {
    VkDevice dev; VkFence fence; uint64_t seq;
    {
        std::lock_guard<std::mutex> lk(g_vkMx);
        if (!g_vkc.pending) { err = "vk: no readback pending"; return false; }
        dev = g_vkc.dev; fence = g_vkc.fence; seq = g_vkc.seq;
    }
    VkResult r = g_vkf.vkWaitForFences(dev, 1, &fence, VK_TRUE, (uint64_t)timeoutMs * 1000000ULL);
    std::lock_guard<std::mutex> lk(g_vkMx);
    if (!g_vkc.pending || g_vkc.seq != seq) { err = "vk: readback was dropped"; return false; }
    if (r == VK_TIMEOUT) { err = "vk: GPU copy did not complete within timeout"; return false; }
    if (r != VK_SUCCESS) { VkReleaseCapLocked("device lost (readback)"); err = "vk: device lost"; return false; }
    if (!g_vkc.coherent) { VkMappedMemoryRange mr{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; mr.memory = g_vkc.mem; mr.offset = 0; mr.size = VK_WHOLE_SIZE; g_vkf.vkInvalidateMappedMemoryRanges(dev, 1, &mr); }
    bool swapRB; DXGI_FORMAT f = VkToDxgi(g_vkc.fmt, &swapRB);
    uint32_t bpp = VkBpp(g_vkc.fmt);
    g_capPix.resize((size_t)g_vkc.w * g_vkc.h * 4);
    bool ok = true;
    for (uint32_t y = 0; y < g_vkc.h && ok; y++) {
        BYTE* d = g_capPix.data() + (size_t)y * g_vkc.w * 4;
        ok = ConvertRow(f, (const BYTE*)g_vkc.map + (size_t)y * g_vkc.w * bpp, d, g_vkc.w);
        if (ok && swapRB) for (uint32_t x = 0; x < g_vkc.w; x++) std::swap(d[4 * x], d[4 * x + 2]);
    }
    g_vkc.pending = false;
    if (!ok) { g_capPix.clear(); err = "vk: format conversion failed"; return false; }
    g_capW = g_vkc.w; g_capH = g_vkc.h; g_capFmt = f;
    return true;
}

// ---- hooks ----
thread_local int t_vkIn = 0;
VkResult VKAPI_CALL h_vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo* ci, const VkAllocationCallbacks* a, VkDevice* out) {
    VkResult r = o_vkCreateDevice(pd, ci, a, out);
    if (r == VK_SUCCESS && out && *out) { std::lock_guard<std::mutex> lk(g_vkMx); g_vkDevs[*out] = pd; Log("vk: device %p created (physical %p)", *out, pd); }
    return r;
}
void VKAPI_CALL h_vkDestroyDevice(VkDevice d, const VkAllocationCallbacks* a) {
    if (d) {
        std::lock_guard<std::mutex> lk(g_vkMx);
        if (g_vkc.dev == d) VkReleaseCapLocked("vkDestroyDevice");
        for (auto it = g_vkScs.begin(); it != g_vkScs.end();) {
            if (it->second.dev == d) { for (VkSemaphore s : it->second.sems) if (s && VkResolve()) g_vkf.vkDestroySemaphore(d, s, nullptr); it = g_vkScs.erase(it); } else ++it;
        }
        for (auto it = g_vkQueues.begin(); it != g_vkQueues.end();) { if (it->second.first == d) it = g_vkQueues.erase(it); else ++it; }
        g_vkDevs.erase(d);
    }
    o_vkDestroyDevice(d, a);
}
void VKAPI_CALL h_vkGetDeviceQueue(VkDevice d, uint32_t fam, uint32_t idx, VkQueue* q) {
    o_vkGetDeviceQueue(d, fam, idx, q);
    if (q && *q) { std::lock_guard<std::mutex> lk(g_vkMx); g_vkQueues[*q] = {d, fam}; }
}
void VKAPI_CALL h_vkGetDeviceQueue2(VkDevice d, const VkDeviceQueueInfo2* info, VkQueue* q) {
    o_vkGetDeviceQueue2(d, info, q);
    if (q && *q && info) { std::lock_guard<std::mutex> lk(g_vkMx); g_vkQueues[*q] = {d, info->queueFamilyIndex}; }
}
VkResult VKAPI_CALL h_vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* a, VkSwapchainKHR* sc) {
    if (!ci || !g_vkEnabled) return o_vkCreateSwapchainKHR(d, ci, a, sc);
    VkSwapchainCreateInfoKHR c = *ci;
    bool added = !(c.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    c.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkResult r = o_vkCreateSwapchainKHR(d, &c, a, sc);
    bool transferSrc = r == VK_SUCCESS;
    if (r != VK_SUCCESS && added) { r = o_vkCreateSwapchainKHR(d, ci, a, sc); transferSrc = false; Log("vk: swapchain refused TRANSFER_SRC usage; created as the game asked"); }
    if (r == VK_SUCCESS && sc && VkResolve()) {
        if (added && transferSrc) g_vkUsageAdded++;
        VkScInfo s; s.dev = d; s.fmt = ci->imageFormat; s.ext = ci->imageExtent; s.layers = ci->imageArrayLayers; s.transferSrc = transferSrc;
        uint32_t n = 0;
        if (g_vkf.vkGetSwapchainImagesKHR(d, *sc, &n, nullptr) == VK_SUCCESS && n) { s.imgs.resize(n); if (g_vkf.vkGetSwapchainImagesKHR(d, *sc, &n, s.imgs.data()) != VK_SUCCESS) s.imgs.clear(); }
        std::lock_guard<std::mutex> lk(g_vkMx);
        auto sf = g_vkSurfaces.find(VkKey(ci->surface)); if (sf != g_vkSurfaces.end()) s.hwnd = sf->second;
        Log("vk: swapchain %llx created %ux%u fmt %d images %u transfer_src %d hwnd %p", (unsigned long long)VkKey(*sc), ci->imageExtent.width, ci->imageExtent.height, (int)ci->imageFormat, n, (int)transferSrc, s.hwnd);
        g_vkScs[VkKey(*sc)] = std::move(s);
    }
    return r;
}
void VKAPI_CALL h_vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks* a) {
    {
        std::lock_guard<std::mutex> lk(g_vkMx);
        auto it = g_vkScs.find(VkKey(s));
        if (it != g_vkScs.end()) {
            if (g_vkc.pending && g_vkc.dev == d) g_vkf.vkWaitForFences(d, 1, &g_vkc.fence, VK_TRUE, 1000000000ULL);
            // legal: the game must have finished every use of the swapchain's images (incl. the present waits on our semaphores)
            for (VkSemaphore x : it->second.sems) if (x && VkResolve()) g_vkf.vkDestroySemaphore(d, x, nullptr);
            g_vkScs.erase(it);
        }
    }
    o_vkDestroySwapchainKHR(d, s, a);
}
VkResult VKAPI_CALL h_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR* pi) {
    if (t_vkIn) return o_vkQueuePresentKHR(q, pi);
    C.present++; g_vkPresents++;
    // only presents to the game window are frames (a swapchain whose surface we did not see is assumed to be it)
    HWND w = nullptr;
    if (pi && pi->swapchainCount && pi->pSwapchains) { std::lock_guard<std::mutex> lk(g_vkMx); auto it = g_vkScs.find(VkKey(pi->pSwapchains[0])); if (it != g_vkScs.end()) w = it->second.hwnd; }
    // t_vkIn also marks the driver's own DXGI Present (AMD presents Vulkan through an internal D3D12 swapchain on
    // this thread) as foreign, so the frame is not counted twice
    if (w && g_hwnd && w != g_hwnd) { g_vkPresentsOther++; t_vkIn++; VkResult r0 = o_vkQueuePresentKHR(q, pi); t_vkIn--; return r0; }
    t_vkIn++;
    OnFrame(API_VK);
    VkPresentInfoKHR mod; VkSemaphore sig = VK_NULL_HANDLE; bool use = false;
    if (BeginCaptureClaim()) {
        use = BeginVk(q, pi, &mod, &sig, g_capErr);
        if (use) g_capPending = API_VK;
        EndCapture(API_VK);
    }
    VkResult r = o_vkQueuePresentKHR(q, use ? &mod : pi);
    t_vkIn--;
    return r;
}
VkResult VKAPI_CALL h_vkCreateWin32SurfaceKHR(VkInstance inst, const VkWin32SurfaceCreateInfoKHR* ci, const VkAllocationCallbacks* a, VkSurfaceKHR* s) {
    VkResult r = o_vkCreateWin32SurfaceKHR(inst, ci, a, s);
    if (r == VK_SUCCESS && s && ci) { std::lock_guard<std::mutex> lk(g_vkMx); g_vkSurfaces[VkKey(*s)] = ci->hwnd; }
    return r;
}
PFN_vkVoidFunction VKAPI_CALL h_vkGetDeviceProcAddr(VkDevice d, const char* n) {
    PFN_vkVoidFunction f = o_vkGetDeviceProcAddr(d, n);
    if (!f || !n) return f;
    // only hand out a wrapper whose loader trampoline we actually hold
    if (!strcmp(n, "vkQueuePresentKHR") && o_vkQueuePresentKHR) return (PFN_vkVoidFunction)&h_vkQueuePresentKHR;
    if (!strcmp(n, "vkCreateSwapchainKHR") && o_vkCreateSwapchainKHR) return (PFN_vkVoidFunction)&h_vkCreateSwapchainKHR;
    if (!strcmp(n, "vkDestroySwapchainKHR") && o_vkDestroySwapchainKHR) return (PFN_vkVoidFunction)&h_vkDestroySwapchainKHR;
    if (!strcmp(n, "vkGetDeviceQueue") && o_vkGetDeviceQueue) return (PFN_vkVoidFunction)&h_vkGetDeviceQueue;
    if (!strcmp(n, "vkGetDeviceQueue2") && o_vkGetDeviceQueue2) return (PFN_vkVoidFunction)&h_vkGetDeviceQueue2;
    if (!strcmp(n, "vkDestroyDevice") && o_vkDestroyDevice) return (PFN_vkVoidFunction)&h_vkDestroyDevice;
    return f;
}
// Also called from the loader's DLL-load notification with the module just mapped (early injection), so a game
// that LoadLibrary's vulkan-1.dll and creates its device right away is hooked before its first Vulkan call.
void TryHookVk(HMODULE h = nullptr) {
    if (g_vkHooked) return;
    if (!h) h = GetModuleHandleW(L"vulkan-1.dll"); if (!h) return;
    if (g_vkHooked.exchange(true)) return;
    HookAddr("vkCreateDevice", (void*)GetProcAddress(h, "vkCreateDevice"), (void*)&h_vkCreateDevice, (void**)&o_vkCreateDevice);
    HookAddr("vkDestroyDevice", (void*)GetProcAddress(h, "vkDestroyDevice"), (void*)&h_vkDestroyDevice, (void**)&o_vkDestroyDevice);
    HookAddr("vkGetDeviceQueue", (void*)GetProcAddress(h, "vkGetDeviceQueue"), (void*)&h_vkGetDeviceQueue, (void**)&o_vkGetDeviceQueue);
    HookAddr("vkGetDeviceQueue2", (void*)GetProcAddress(h, "vkGetDeviceQueue2"), (void*)&h_vkGetDeviceQueue2, (void**)&o_vkGetDeviceQueue2);
    HookAddr("vkCreateSwapchainKHR", (void*)GetProcAddress(h, "vkCreateSwapchainKHR"), (void*)&h_vkCreateSwapchainKHR, (void**)&o_vkCreateSwapchainKHR);
    HookAddr("vkDestroySwapchainKHR", (void*)GetProcAddress(h, "vkDestroySwapchainKHR"), (void*)&h_vkDestroySwapchainKHR, (void**)&o_vkDestroySwapchainKHR);
    HookAddr("vkQueuePresentKHR", (void*)GetProcAddress(h, "vkQueuePresentKHR"), (void*)&h_vkQueuePresentKHR, (void**)&o_vkQueuePresentKHR);
    HookAddr("vkGetDeviceProcAddr", (void*)GetProcAddress(h, "vkGetDeviceProcAddr"), (void*)&h_vkGetDeviceProcAddr, (void**)&o_vkGetDeviceProcAddr);
    HookAddr("vkCreateWin32SurfaceKHR", (void*)GetProcAddress(h, "vkCreateWin32SurfaceKHR"), (void*)&h_vkCreateWin32SurfaceKHR, (void**)&o_vkCreateWin32SurfaceKHR);
    Log("vulkan hooks installed");
}
