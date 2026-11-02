// vi_vktest.cpp - synthetic Vulkan capture target for vinput. Every frame the CPU writes vi_pattern.h (frame counter +
// colour bars) into a host-visible staging buffer and the GPU copies it into the acquired swapchain image
// (swapchain usage = TRANSFER_DST only: vinput must add TRANSFER_SRC itself). Recreates the swapchain on WM_SIZE /
// OUT_OF_DATE / SUBOPTIMAL. vulkan-1.dll is loaded at runtime (never linked).
//   vi_vktest.exe <log> <x> <y> <w> <h> [--fmt bgra|rgba|r10] [--gdpa] [--wait-vinput <ms>]
//   --gdpa         resolve instance functions through vkGetInstanceProcAddr and device functions through
//                  vkGetDeviceProcAddr (the volk path) instead of the loader's exports
//   --wait-vinput  load vulkan-1.dll, then wait up to <ms> for \\.\pipe\vinput_<pid> (vinput injected) BEFORE
//                  creating the instance / device / swapchain
// Log: START, VINPUT_PIPE, RESIZE WxH hr=0x00000000, FRAME n maxms= worstms=, FAIL ...
// Never activates: WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <share.h>
#include <vector>
#include <string>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "../third_party/vulkan/vulkan_core.h"
#include "../third_party/vulkan/vulkan_win32.h"
#include "vi_pattern.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

static FILE* g_log; static HWND g_hwnd; static volatile bool g_resize = false;
static void L(const char* f, ...) { if (!g_log) return; va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log); }
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE) g_resize = true;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { L("FAIL %s vk=%d line %d", #x, (int)r_, __LINE__); return 2; } } while (0)

#define INST_FNS(X) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkCreateWin32SurfaceKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) X(vkDestroySurfaceKHR) X(vkDestroyInstance)
#define DEV_FNS(X) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) \
    X(vkQueueSubmit) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkDeviceWaitIdle) X(vkDestroyDevice)
#define DECL(n) static PFN_##n n;
INST_FNS(DECL) DEV_FNS(DECL)

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_log = _wfsopen(argc > 1 ? argv[1] : L"vi_vktest.log", L"w", _SH_DENYNO);
    int x = argc > 5 ? _wtoi(argv[2]) : 100, y = argc > 5 ? _wtoi(argv[3]) : 100, w = argc > 5 ? _wtoi(argv[4]) : 800, hgt = argc > 5 ? _wtoi(argv[5]) : 450;
    vipat::Fmt pf = vipat::BGRA8; VkFormat want = VK_FORMAT_B8G8R8A8_UNORM; bool gdpa = false; int waitVinput = 0;
    for (int i = 6; i < argc; i++) {
        if (!wcscmp(argv[i], L"--fmt") && i + 1 < argc) {
            const wchar_t* f = argv[++i];
            if (!wcscmp(f, L"rgba")) { pf = vipat::RGBA8; want = VK_FORMAT_R8G8B8A8_UNORM; }
            else if (!wcscmp(f, L"r10")) { pf = vipat::R10G10B10A2; want = VK_FORMAT_A2B10G10R10_UNORM_PACK32; }
        } else if (!wcscmp(argv[i], L"--gdpa")) gdpa = true;
        else if (!wcscmp(argv[i], L"--wait-vinput") && i + 1 < argc) waitVinput = _wtoi(argv[++i]);
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_vktest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput vulkan capture target", WS_OVERLAPPEDWINDOW, x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) { L("FAIL LoadLibrary vulkan-1.dll err=%lu", GetLastError()); return 2; }
    auto gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr");
    auto createInstance = (PFN_vkCreateInstance)GetProcAddress(vk, "vkCreateInstance");
    if (!gipa || !createInstance) { L("FAIL loader exports missing"); return 2; }
    if (waitVinput > 0) {
        wchar_t pipe[64]; swprintf_s(pipe, L"\\\\.\\pipe\\vinput_%lu", GetCurrentProcessId());
        DWORD t0 = GetTickCount(); bool seen = false;
        while (GetTickCount() - t0 < (DWORD)waitVinput) {
            MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            if (WaitNamedPipeW(pipe, 1) || GetLastError() == ERROR_SEM_TIMEOUT) { seen = true; break; }
            Sleep(20);
        }
        L("VINPUT_PIPE seen=%d after %lu ms", seen ? 1 : 0, GetTickCount() - t0);
    }
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName = "vi_vktest"; ai.apiVersion = VK_API_VERSION_1_1;
    const char* iexts[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = iexts;
    VkInstance inst = VK_NULL_HANDLE;
    VKCK(createInstance(&ici, nullptr, &inst));
    int same = 0, total = 0;
#define LOADI(n) n = gdpa ? (PFN_##n)gipa(inst, #n) : (PFN_##n)GetProcAddress(vk, #n); if (!n) { L("FAIL resolve %s", #n); return 2; } \
    total++; if ((void*)n == (void*)GetProcAddress(vk, #n)) same++;
    INST_FNS(LOADI)
    L("RESOLVE instance mode=%s same_as_export=%d/%d", gdpa ? "gipa" : "exports", same, total);

    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR}; sci.hinstance = hi; sci.hwnd = g_hwnd;
    VkSurfaceKHR surf = VK_NULL_HANDLE; VKCK(vkCreateWin32SurfaceKHR(inst, &sci, nullptr, &surf));
    uint32_t npd = 0; VKCK(vkEnumeratePhysicalDevices(inst, &npd, nullptr));
    std::vector<VkPhysicalDevice> pds(npd); VKCK(vkEnumeratePhysicalDevices(inst, &npd, pds.data()));
    VkPhysicalDevice pd = VK_NULL_HANDLE; uint32_t fam = UINT32_MAX;
    for (VkPhysicalDevice d : pds) {
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qp(nq); vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qp.data());
        for (uint32_t i = 0; i < nq; i++) {
            VkBool32 sup = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surf, &sup);
            if (sup && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { pd = d; fam = i; break; }
        }
        if (pd) break;
    }
    if (!pd) { L("FAIL no graphics+present queue family"); return 2; }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = fam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* dexts[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = dexts;
    VkDevice dev = VK_NULL_HANDLE; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev));
    same = 0; total = 0;
#define LOADD(n) n = gdpa ? (PFN_##n)vkGetDeviceProcAddr(dev, #n) : (PFN_##n)GetProcAddress(vk, #n); if (!n) { L("FAIL resolve %s", #n); return 2; } \
    total++; if ((void*)n == (void*)GetProcAddress(vk, #n)) same++;
    DEV_FNS(LOADD)
    L("RESOLVE device mode=%s same_as_export=%d/%d gpu=%s", gdpa ? "gdpa" : "exports", same, total, props.deviceName);
    VkQueue q = VK_NULL_HANDLE; vkGetDeviceQueue(dev, fam, 0, &q);

    // surface format
    uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> sfs(nf); vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, sfs.data());
    VkSurfaceFormatKHR sf{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (auto& f : sfs) if (f.format == want) { sf = f; break; }
    if (sf.format == VK_FORMAT_UNDEFINED) {
        std::string have; for (auto& f : sfs) { have += std::to_string((int)f.format) + " "; }
        L("FAIL surface does not offer format %d (offers %s)", (int)want, have.c_str()); return 5;
    }
    uint32_t npm = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surf, &npm, nullptr);
    std::vector<VkPresentModeKHR> pms(npm); vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surf, &npm, pms.data());
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : pms) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) pm = m;
    for (auto m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR && pm != VK_PRESENT_MODE_IMMEDIATE_KHR) pm = m;
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);

    VkSwapchainKHR sc = VK_NULL_HANDLE; std::vector<VkImage> imgs; std::vector<VkSemaphore> done; VkExtent2D ext{};
    auto destroySems = [&]() { for (auto s : done) vkDestroySemaphore(dev, s, nullptr); done.clear(); };
    // returns 0 ok, 1 zero-size (minimized), 2+ failure
    auto makeSwapchain = [&]() -> int {
        VkSurfaceCapabilitiesKHR caps; VKCK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps));
        RECT rc; GetClientRect(g_hwnd, &rc);
        ext = caps.currentExtent.width != 0xFFFFFFFFu ? caps.currentExtent : VkExtent2D{(uint32_t)rc.right, (uint32_t)rc.bottom};
        if (!ext.width || !ext.height) return 1;
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) { L("FAIL surface lacks TRANSFER_DST usage"); return 2; }
        uint32_t n = caps.minImageCount + 1; if (caps.maxImageCount && n > caps.maxImageCount) n = caps.maxImageCount;
        VkCompositeAlphaFlagBitsKHR ca = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surf; ci.minImageCount = n; ci.imageFormat = sf.format; ci.imageColorSpace = sf.colorSpace; ci.imageExtent = ext;
        ci.imageArrayLayers = 1; ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT; ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = caps.currentTransform; ci.compositeAlpha = ca; ci.presentMode = pm; ci.clipped = VK_TRUE; ci.oldSwapchain = sc;
        VkSwapchainKHR nsc = VK_NULL_HANDLE; VKCK(vkCreateSwapchainKHR(dev, &ci, nullptr, &nsc));
        if (sc) vkDestroySwapchainKHR(dev, sc, nullptr);
        sc = nsc;
        uint32_t ni = 0; VKCK(vkGetSwapchainImagesKHR(dev, sc, &ni, nullptr)); imgs.resize(ni); VKCK(vkGetSwapchainImagesKHR(dev, sc, &ni, imgs.data()));
        destroySems(); done.resize(ni);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& s : done) VKCK(vkCreateSemaphore(dev, &si, nullptr, &s));
        return 0;
    };
    if (makeSwapchain() != 0) { L("FAIL initial swapchain"); return 2; }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.queueFamilyIndex = fam; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool; VKCK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cb; VKCK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore acq; VKCK(vkCreateSemaphore(dev, &semi, nullptr, &acq));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; VKCK(vkCreateFence(dev, &fci, nullptr, &fence));
    VkBuffer stg = VK_NULL_HANDLE; VkDeviceMemory stgMem = VK_NULL_HANDLE; VkDeviceSize stgSize = 0; uint8_t* stgMap = nullptr;

    L("START hwnd=%p fmt=%d client=%ux%u present_mode=%d images=%u", g_hwnd, (int)sf.format, ext.width, ext.height, (int)pm, (unsigned)imgs.size());
    uint32_t frame = 0; DWORD lastLog = 0; int resizes = 0; bool recreate = false;
    LARGE_INTEGER qpf, prevT{}; QueryPerformanceFrequency(&qpf); double maxMs = 0, worstMs = 0;
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { vkDeviceWaitIdle(dev); L("EXIT frames=%u resizes=%d", frame, resizes); return 0; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (g_resize || recreate) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            if (recreate || (uint32_t)rc.right != ext.width || (uint32_t)rc.bottom != ext.height) {
                vkDeviceWaitIdle(dev);
                int r = makeSwapchain();
                if (r == 1) { Sleep(10); continue; }
                if (r != 0) return 3;
                L("RESIZE %ux%u hr=0x00000000", ext.width, ext.height);
                resizes++; prevT.QuadPart = 0;
            }
            g_resize = false; recreate = false;
        }
        VkDeviceSize need = (VkDeviceSize)ext.width * ext.height * vipat::Bpp(pf);
        if (need > stgSize) {
            if (stg) { vkDeviceWaitIdle(dev); vkDestroyBuffer(dev, stg, nullptr); vkFreeMemory(dev, stgMem, nullptr); }
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size = need; bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            VKCK(vkCreateBuffer(dev, &bci, nullptr, &stg));
            VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, stg, &mr);
            uint32_t mt = UINT32_MAX;
            for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
                if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = i; break; }
            if (mt == UINT32_MAX) { L("FAIL no host-coherent memory"); return 2; }
            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt;
            VKCK(vkAllocateMemory(dev, &mai, nullptr, &stgMem)); VKCK(vkBindBufferMemory(dev, stg, stgMem, 0));
            VKCK(vkMapMemory(dev, stgMem, 0, VK_WHOLE_SIZE, 0, (void**)&stgMap)); stgSize = need;
        }
        uint32_t idx = 0;
        VkResult ar = vkAcquireNextImageKHR(dev, sc, 2000000000ULL, acq, VK_NULL_HANDLE, &idx);
        if (ar == VK_ERROR_OUT_OF_DATE_KHR) { recreate = true; continue; }
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { L("FAIL vkAcquireNextImageKHR vk=%d", (int)ar); return 4; }
        for (uint32_t yy = 0; yy < ext.height; yy++) vipat::Row(pf, (int)yy, (int)ext.width, frame, stgMap + (size_t)yy * ext.width * vipat::Bpp(pf));
        VKCK(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKCK(vkBeginCommandBuffer(cb, &bi));
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = imgs[idx]; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy rg{}; rg.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; rg.imageExtent = {ext.width, ext.height, 1};
        vkCmdCopyBufferToImage(cb, stg, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = 0; b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VKCK(vkEndCommandBuffer(cb));
        VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.waitSemaphoreCount = 1; si.pWaitSemaphores = &acq; si.pWaitDstStageMask = &ws;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &done[idx];
        VKCK(vkResetFences(dev, 1, &fence));
        VKCK(vkQueueSubmit(q, 1, &si, fence));
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &done[idx]; pi.swapchainCount = 1; pi.pSwapchains = &sc; pi.pImageIndices = &idx;
        VkResult pr = vkQueuePresentKHR(q, &pi);
        // one frame in flight: the staging buffer and the command buffer are reused next frame
        VkResult fr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ULL);
        if (fr != VK_SUCCESS) { L("FAIL vkWaitForFences vk=%d", (int)fr); return 4; }
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) recreate = true;
        else if (pr != VK_SUCCESS) { L("FAIL vkQueuePresentKHR vk=%d", (int)pr); return 4; }
        ++frame;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (prevT.QuadPart) { double ms = (now.QuadPart - prevT.QuadPart) * 1000.0 / qpf.QuadPart; if (ms > maxMs) maxMs = ms; if (ms > worstMs) worstMs = ms; }
        prevT = now;
        if (GetTickCount() - lastLog > 1000) { lastLog = GetTickCount(); L("FRAME %u maxms=%.1f worstms=%.1f", frame, maxMs, worstMs); maxMs = 0; }
        Sleep(4);
    }
}
