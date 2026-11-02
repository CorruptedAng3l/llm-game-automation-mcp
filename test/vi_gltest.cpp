// vi_gltest.cpp - synthetic OpenGL capture target for vinput. Draws vi_pattern.h (frame-counter bits + colour bars)
// with scissored glClear into the default framebuffer, then presents with gdi32 SwapBuffers (default) or by
// calling opengl32!wglSwapBuffers directly (--wgl). Also leaves a non-default GL state behind every frame (pack
// alignment 1, a bound pixel-pack buffer when available) so a capture that does not restore state is visible.
//   vi_gltest.exe <log> <x> <y> <w> <h> [--wgl]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <share.h>
#include "vi_pattern.h"
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

static FILE* g_log;
static void L(const char* f, ...) { if (!g_log) return; va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a); fputc('\n', g_log); fflush(g_log); }
static LRESULT CALLBACK Wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
typedef BOOL(WINAPI* PFN_wglSwap)(HDC);
typedef void(APIENTRY* PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_glBindBuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_glBufferData)(GLenum, ptrdiff_t, const void*, GLenum);

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    int argc; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_log = _wfsopen(argc > 1 ? argv[1] : L"vi_gltest.log", L"w", _SH_DENYNO);
    int x = argc > 5 ? _wtoi(argv[2]) : 100, y = argc > 5 ? _wtoi(argv[3]) : 100, w = argc > 5 ? _wtoi(argv[4]) : 800, hgt = argc > 5 ? _wtoi(argv[5]) : 450;
    bool useWgl = false;
    for (int i = 6; i < argc; i++) if (!wcscmp(argv[i], L"--wgl")) useWgl = true;
    WNDCLASSW wc = {}; wc.lpfnWndProc = Wnd; wc.hInstance = hi; wc.lpszClassName = L"vi_gltest"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_OWNDC;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"vinput gl capture target", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    pfd.cDepthBits = 24; pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { L("FAIL pixel format %lu", GetLastError()); return 2; }
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) { L("FAIL context %lu", GetLastError()); return 2; }
    PFN_wglSwap wglSwap = (PFN_wglSwap)GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "wglSwapBuffers");
    // leave non-default pack state + a bound pixel-pack buffer (if GL >= 2.1) for the capture to save/restore
    PFN_glGenBuffers genB = (PFN_glGenBuffers)wglGetProcAddress("glGenBuffers");
    PFN_glBindBuffer bindB = (PFN_glBindBuffer)wglGetProcAddress("glBindBuffer");
    PFN_glBufferData dataB = (PFN_glBufferData)wglGetProcAddress("glBufferData");
    GLuint pbo = 0;
    if (genB && bindB && dataB) { genB(1, &pbo); bindB(0x88EB, pbo); dataB(0x88EB, 64, nullptr, 0x88E1 /*STREAM_READ*/); }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    L("START hwnd=%p gl=%s vendor=%s swap=%s pbo=%u", hwnd, (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_VENDOR), useWgl ? "wglSwapBuffers" : "SwapBuffers", pbo);
    UINT32 frame = 0; DWORD lastLog = 0; int stateBad = 0;
    LARGE_INTEGER qpf, prevT{}; QueryPerformanceFrequency(&qpf); double maxMs = 0;
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) { L("EXIT frames=%u state_bad=%d", frame, stateBad); return 0; } TranslateMessage(&msg); DispatchMessageW(&msg); }
        RECT cr; GetClientRect(hwnd, &cr); int cw = cr.right, ch = cr.bottom;
        if (cw > 0 && ch > 0) {
            glViewport(0, 0, cw, ch);
            glEnable(GL_SCISSOR_TEST);
            for (int b = 0; b < 8; b++) {
                int x0 = (b * cw + 7) / 8, x1 = ((b + 1) * cw + 7) / 8;
                glScissor(x0, 0, x1 - x0, ch - vipat::kBandH);
                glClearColor(vipat::kBars[b][0] / 255.f, vipat::kBars[b][1] / 255.f, vipat::kBars[b][2] / 255.f, 1);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glScissor(0, ch - vipat::kBandH, cw, vipat::kBandH); glClearColor(0.5f, 0.5f, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
            for (int i = 0; i < 32; i++) {
                float v = ((frame >> (31 - i)) & 1) ? 1.f : 0.f;
                glScissor(i * vipat::kCell, ch - vipat::kBandH, vipat::kCell, vipat::kBandH); glClearColor(v, v, v, 1); glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
        }
        if (useWgl) wglSwap(dc); else SwapBuffers(dc);
        GLint al = 0; glGetIntegerv(GL_PACK_ALIGNMENT, &al);
        GLint bound = 0; if (pbo) glGetIntegerv(0x88ED, &bound);
        GLint rb = 0; glGetIntegerv(GL_READ_BUFFER, &rb);
        if (al != 1 || (pbo && (GLuint)bound != pbo) || rb != GL_BACK) { if (stateBad++ < 5) L("STATE_CHANGED align=%d pbo=%d readbuf=0x%X", al, bound, rb); }
        ++frame;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (prevT.QuadPart) { double ms = (now.QuadPart - prevT.QuadPart) * 1000.0 / qpf.QuadPart; if (ms > maxMs) maxMs = ms; }
        prevT = now;
        if (GetTickCount() - lastLog > 1000) { lastLog = GetTickCount(); L("FRAME %u maxms=%.1f", frame, maxMs); maxMs = 0; }
        Sleep(4);
    }
}
