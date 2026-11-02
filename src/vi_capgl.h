// vi_capgl.h - native OpenGL capture for vinput. Included INSIDE vinput_core.cpp's anonymous namespace.
//
// Hooks opengl32!wglSwapBuffers and gdi32!SwapBuffers (gdi32 calls wglSwapBuffers itself, so a thread-local
// guard counts each frame once). On a capture request the game's own GL thread, with its context current and
// BEFORE the swap, reads GL_BACK of the default framebuffer with glReadPixels(GL_BGRA). Every piece of GL
// state we touch is saved and restored (read buffer, read framebuffer, pixel-pack buffer, pack alignment /
// row length / skips / swap / lsb). glGetError is drained before and after the read; that hides one pending
// game-side GL error at the capture frame (documented). The read is synchronous: at most one frame of stall
// on the capture frame only. Nothing is cached, so there is nothing to release on resize.
using PFN_wglSwap = BOOL(WINAPI*)(HDC);
PFN_wglSwap o_wglSwapBuffers = nullptr, o_gdiSwapBuffers = nullptr;
std::atomic<bool> g_glHooked{false};
thread_local int t_inSwap = 0;

struct GlFns {
    void (WINAPI* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (WINAPI* GetIntegerv)(GLenum, GLint*) = nullptr;
    void (WINAPI* PixelStorei)(GLenum, GLint) = nullptr;
    void (WINAPI* ReadBuffer)(GLenum) = nullptr;
    GLenum (WINAPI* GetError)() = nullptr;
    HGLRC (WINAPI* GetCurrentContext)() = nullptr;
    PROC (WINAPI* GetProcAddr)(LPCSTR) = nullptr;
    void (WINAPI* BindFramebuffer)(GLenum, GLuint) = nullptr;   // GL 3.0 / ARB_framebuffer_object (per context)
    void (WINAPI* BindBuffer)(GLenum, GLuint) = nullptr;        // GL 1.5
    HGLRC fnCtx = nullptr;
} g_gl;

bool GlResolve() {
    HMODULE h = GetModuleHandleW(L"opengl32.dll"); if (!h) return false;
    if (!g_gl.ReadPixels) {
        g_gl.ReadPixels = (decltype(g_gl.ReadPixels))GetProcAddress(h, "glReadPixels");
        g_gl.GetIntegerv = (decltype(g_gl.GetIntegerv))GetProcAddress(h, "glGetIntegerv");
        g_gl.PixelStorei = (decltype(g_gl.PixelStorei))GetProcAddress(h, "glPixelStorei");
        g_gl.ReadBuffer = (decltype(g_gl.ReadBuffer))GetProcAddress(h, "glReadBuffer");
        g_gl.GetError = (decltype(g_gl.GetError))GetProcAddress(h, "glGetError");
        g_gl.GetCurrentContext = (decltype(g_gl.GetCurrentContext))GetProcAddress(h, "wglGetCurrentContext");
        g_gl.GetProcAddr = (decltype(g_gl.GetProcAddr))GetProcAddress(h, "wglGetProcAddress");
    }
    return g_gl.ReadPixels && g_gl.GetIntegerv && g_gl.PixelStorei && g_gl.ReadBuffer && g_gl.GetError && g_gl.GetCurrentContext && g_gl.GetProcAddr;
}
// Extension entry points are per context on Windows; wglGetProcAddress may return small sentinel values.
PROC GlExt(const char* n) { PROC p = g_gl.GetProcAddr(n); ULONG_PTR v = (ULONG_PTR)p; return (v <= 3 || v == (ULONG_PTR)-1) ? nullptr : p; }

void CaptureGL(HDC dc, HWND w) {
    if (!GlResolve()) { g_capErr = "gl: opengl32 entry points not found"; return; }
    HGLRC ctx = g_gl.GetCurrentContext();
    if (!ctx) { g_capErr = "gl: no current GL context on the SwapBuffers thread"; return; }
    if (ctx != g_gl.fnCtx) {
        g_gl.fnCtx = ctx;
        g_gl.BindFramebuffer = (decltype(g_gl.BindFramebuffer))GlExt("glBindFramebuffer");
        if (!g_gl.BindFramebuffer) g_gl.BindFramebuffer = (decltype(g_gl.BindFramebuffer))GlExt("glBindFramebufferEXT");
        g_gl.BindBuffer = (decltype(g_gl.BindBuffer))GlExt("glBindBuffer");
    }
    RECT rc{}; GetClientRect(w, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (cw <= 0 || ch <= 0) { g_capErr = "gl: empty client rect"; return; }
    const GLenum READ_FB_BINDING = 0x8CAA, READ_FB = 0x8CA8, PACK_BUF_BINDING = 0x88ED, PACK_BUF = 0x88EB;
    const GLenum PACK_ROW_LENGTH = 0x0D02, PACK_SKIP_ROWS = 0x0D03, PACK_SKIP_PIXELS = 0x0D04, PACK_SWAP = 0x0D00, PACK_LSB = 0x0D01, PACK_ALIGN = 0x0D05;
    const GLenum BGRA = 0x80E1;
    while (g_gl.GetError() != GL_NO_ERROR) {}
    GLint readBuf = 0, readFb = 0, packBuf = 0, rowLen = 0, skipRows = 0, skipPix = 0, swapB = 0, lsb = 0, align = 4;
    if (g_gl.BindFramebuffer) g_gl.GetIntegerv(READ_FB_BINDING, &readFb);
    if (g_gl.BindBuffer) g_gl.GetIntegerv(PACK_BUF_BINDING, &packBuf);
    g_gl.GetIntegerv(PACK_ROW_LENGTH, &rowLen); g_gl.GetIntegerv(PACK_SKIP_ROWS, &skipRows); g_gl.GetIntegerv(PACK_SKIP_PIXELS, &skipPix);
    g_gl.GetIntegerv(PACK_SWAP, &swapB); g_gl.GetIntegerv(PACK_LSB, &lsb); g_gl.GetIntegerv(PACK_ALIGN, &align);
    while (g_gl.GetError() != GL_NO_ERROR) {}   // GL_READ_FRAMEBUFFER_BINDING is invalid on pre-3.0 contexts
    if (g_gl.BindFramebuffer && readFb) g_gl.BindFramebuffer(READ_FB, 0);
    g_gl.GetIntegerv(GL_READ_BUFFER, &readBuf);   // read-buffer state belongs to the bound framebuffer: query FB 0's
    if (g_gl.BindBuffer && packBuf) g_gl.BindBuffer(PACK_BUF, 0);
    g_gl.PixelStorei(PACK_ROW_LENGTH, 0); g_gl.PixelStorei(PACK_SKIP_ROWS, 0); g_gl.PixelStorei(PACK_SKIP_PIXELS, 0);
    g_gl.PixelStorei(PACK_SWAP, 0); g_gl.PixelStorei(PACK_LSB, 0); g_gl.PixelStorei(PACK_ALIGN, 4);
    g_gl.ReadBuffer(GL_BACK);
    std::vector<BYTE> px((size_t)cw * ch * 4);
    g_gl.ReadPixels(0, 0, cw, ch, BGRA, GL_UNSIGNED_BYTE, px.data());
    GLenum e = g_gl.GetError();
    while (g_gl.GetError() != GL_NO_ERROR) {}
    g_gl.ReadBuffer((GLenum)readBuf);
    g_gl.PixelStorei(PACK_ROW_LENGTH, rowLen); g_gl.PixelStorei(PACK_SKIP_ROWS, skipRows); g_gl.PixelStorei(PACK_SKIP_PIXELS, skipPix);
    g_gl.PixelStorei(PACK_SWAP, swapB); g_gl.PixelStorei(PACK_LSB, lsb); g_gl.PixelStorei(PACK_ALIGN, align);
    if (g_gl.BindBuffer && packBuf) g_gl.BindBuffer(PACK_BUF, (GLuint)packBuf);
    if (g_gl.BindFramebuffer && readFb) g_gl.BindFramebuffer(READ_FB, (GLuint)readFb);
    while (g_gl.GetError() != GL_NO_ERROR) {}
    if (e != GL_NO_ERROR) { char b[64]; sprintf_s(b, "gl: glReadPixels error 0x%04X", (unsigned)e); g_capErr = b; return; }
    // GL rows are bottom-up
    g_capPix.resize(px.size());
    size_t row = (size_t)cw * 4;
    for (int y = 0; y < ch; y++) {
        BYTE* d = g_capPix.data() + (size_t)y * row; memcpy(d, px.data() + (size_t)(ch - 1 - y) * row, row);
        for (int x = 0; x < cw; x++) d[4 * x + 3] = 255;
    }
    g_capW = (UINT)cw; g_capH = (UINT)ch;
}
void GlFrame(HDC dc) {
    C.present++;
    HWND w = WindowFromDC(dc);
    if (!w) return;
    if (g_hwnd && w != g_hwnd && GetAncestor(w, GA_ROOT) != g_hwnd) return;
    OnFrame(API_GL);
    if (BeginCaptureClaim()) { CaptureGL(dc, w); EndCapture(API_GL); }
}
BOOL WINAPI h_wglSwapBuffers(HDC dc) {
    if (t_inSwap == 0) GlFrame(dc);
    t_inSwap++; BOOL r = o_wglSwapBuffers(dc); t_inSwap--;
    return r;
}
BOOL WINAPI h_gdiSwapBuffers(HDC dc) {
    if (t_inSwap == 0) GlFrame(dc);
    t_inSwap++; BOOL r = o_gdiSwapBuffers(dc); t_inSwap--;
    return r;
}
// Monitor thread: hook once opengl32.dll is in the process (never loads it).
// Also called from the loader's DLL-load notification with the module just mapped (early injection).
void TryHookGL(HMODULE h = nullptr) {
    if (g_glHooked) return;
    if (!h) h = GetModuleHandleW(L"opengl32.dll"); if (!h) return;
    if (g_glHooked.exchange(true)) return;
    HookAddr("wglSwapBuffers", (void*)GetProcAddress(h, "wglSwapBuffers"), (void*)&h_wglSwapBuffers, (void**)&o_wglSwapBuffers);
    HMODULE g = GetModuleHandleW(L"gdi32.dll");
    if (g) HookAddr("SwapBuffers", (void*)GetProcAddress(g, "SwapBuffers"), (void*)&h_gdiSwapBuffers, (void**)&o_gdiSwapBuffers);
    Log("opengl hooks installed");
}
