// vi_ncctest.cpp - unit tests for NCC template matching (vi_ncc.h). Positive AND negative controls.
// Exit code = number of failures.
#include "vi_ncc.h"
#include <cstdio>

static int failures = 0;
static void Check(bool value, const char* name) { std::printf("%s %s\n", value ? "PASS" : "FAIL", name); if (!value) ++failures; }

// ---- NCC ----
using vi_vision::Image;
static Image Blank(int w, int h, std::uint8_t v) {
    Image im; im.w = w; im.h = h; im.pixels.assign((size_t)w * h * 4, v);
    for (size_t i = 3; i < im.pixels.size(); i += 4) im.pixels[i] = 255;
    return im;
}
static Image Texture(int w, int h) {   // structured pattern, not noise only
    Image im = Blank(w, h, 0); std::uint32_t s = 12345u;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        s = s * 1664525u + 1013904223u;
        int ring = (int)std::sqrt((double)((x - w / 2) * (x - w / 2) + (y - h / 2) * (y - h / 2)));
        int v = (ring % 8 < 4 ? 200 : 60) + ((s >> 24) % 20) + ((x * 3 + y) % 17);
        for (int c = 0; c < 3; ++c) im.pixels[((size_t)y * w + x) * 4 + c] = (std::uint8_t)(v + c * 7);
    }
    return im;
}
static void Paste(Image& dst, const Image& src, int x0, int y0, double gain = 1.0, int offset = 0) {
    for (int y = 0; y < src.h; ++y) for (int x = 0; x < src.w; ++x) for (int c = 0; c < 3; ++c) {
        int v = (int)(src.pixels[((size_t)y * src.w + x) * 4 + c] * gain) + offset; v = v < 0 ? 0 : v > 255 ? 255 : v;
        dst.pixels[((size_t)(y0 + y) * dst.w + x0 + x) * 4 + c] = (std::uint8_t)v;
    }
}
static Image ScaleBy(const Image& src, double f) {
    vi_ncc::Plane p = vi_ncc::Gray(src); vi_ncc::Plane q = vi_ncc::Resample(p, (int)std::lround(src.w * f), (int)std::lround(src.h * f));
    Image out = Blank(q.w, q.h, 0);
    for (size_t i = 0; i < q.v.size(); ++i) for (int c = 0; c < 3; ++c) out.pixels[i * 4 + c] = (std::uint8_t)(q.v[i] < 0 ? 0 : q.v[i] > 255 ? 255 : q.v[i]);
    return out;
}
static bool Near(const vi_ncc::Match& m, int x, int y, int tol) { return std::abs(m.x - x) <= tol && std::abs(m.y - y) <= tol; }

static void NccTests() {
    Image tpl = Texture(48, 40);
    Image scene = Blank(400, 240, 90);
    // background clutter so the window variance is never zero
    for (int y = 0; y < scene.h; ++y) for (int x = 0; x < scene.w; ++x) scene.pixels[((size_t)y * scene.w + x) * 4] = (std::uint8_t)(70 + (x * 7 + y * 13) % 40);
    Paste(scene, tpl, 120, 90);
    auto r = vi_ncc::Find(scene, tpl, {});
    Check(r.error.empty() && !r.matches.empty() && Near(r.matches[0], 120, 90, 1) && r.matches[0].score > 0.97, "template found at the pasted position with score > 0.97");
    Image dim = scene; Paste(dim, tpl, 120, 90, 0.7, -10);
    r = vi_ncc::Find(dim, tpl, {});
    Check(!r.matches.empty() && Near(r.matches[0], 120, 90, 1), "NCC survives a gain/offset change (0.7x - 10)");
    Image two = scene; Paste(two, tpl, 300, 150);
    r = vi_ncc::Find(two, tpl, {});
    Check(r.matches.size() == 2 && ((Near(r.matches[0], 120, 90, 1) && Near(r.matches[1], 300, 150, 1)) || (Near(r.matches[1], 120, 90, 1) && Near(r.matches[0], 300, 150, 1))), "two copies give exactly two matches");
    Image absent = Blank(400, 240, 0);
    for (int y = 0; y < absent.h; ++y) for (int x = 0; x < absent.w; ++x) absent.pixels[((size_t)y * absent.w + x) * 4 + 1] = (std::uint8_t)(70 + (x * 7 + y * 13) % 40);
    r = vi_ncc::Find(absent, tpl, {});
    Check(r.error.empty() && r.matches.empty(), "absent template: no match (negative control)");
    Image big = scene; Image scaled = ScaleBy(tpl, 1.1);
    Image sceneScaled = Blank(400, 240, 90);
    for (int y = 0; y < sceneScaled.h; ++y) for (int x = 0; x < sceneScaled.w; ++x) sceneScaled.pixels[((size_t)y * sceneScaled.w + x) * 4] = (std::uint8_t)(70 + (x * 7 + y * 13) % 40);
    Paste(sceneScaled, scaled, 200, 60);
    r = vi_ncc::Find(sceneScaled, tpl, {});
    Check(!r.matches.empty() && Near(r.matches[0], 200, 60, 2) && r.matches[0].scale > 1.0 && r.matches[0].score > 0.85, "1.1x scaled copy is found by the default scale list and reports scale > 1");
    vi_ncc::Options noScale; noScale.scales = {1.0}; noScale.threshold = 0.9;
    r = vi_ncc::Find(sceneScaled, tpl, noScale);
    Check(r.matches.empty(), "1.1x copy is NOT found with --no-scale at 0.9 (documents the limit)");
    Image huge = ScaleBy(tpl, 1.6);
    Image sceneHuge = Blank(400, 240, 90); Paste(sceneHuge, huge, 100, 80);
    r = vi_ncc::Find(sceneHuge, tpl, {});
    Check(r.matches.empty(), "1.6x copy is outside the default scale range (documented limit)");
    Image flat = Blank(30, 30, 128);
    r = vi_ncc::Find(scene, flat, {});
    Check(!r.error.empty(), "flat template is refused with an error");
    Image tiny = Texture(14, 12), tinyScene = Blank(200, 120, 90);
    for (int y = 0; y < tinyScene.h; ++y) for (int x = 0; x < tinyScene.w; ++x) tinyScene.pixels[((size_t)y * tinyScene.w + x) * 4] = (std::uint8_t)(70 + (x * 7 + y * 13) % 40);
    Paste(tinyScene, tiny, 77, 55);
    r = vi_ncc::Find(tinyScene, tiny, {});
    Check(!r.matches.empty() && Near(r.matches[0], 77, 55, 1), "small template (full-resolution path) found");
}

int main() {
    NccTests();
    std::printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures;
}
