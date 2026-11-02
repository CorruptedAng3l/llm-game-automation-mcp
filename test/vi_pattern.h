// vi_pattern.h - the known, changing test pattern drawn by the synthetic capture targets (D3D12 / GL / Vulkan).
//   rows 0..15 : 32 bit-cells of 16 px, MSB first, white = 1, black = 0, encoding the target's frame counter;
//                x >= 512 in that band is mid gray
//   rows 16..  : 8 vertical colour bars across the client width (white yellow cyan green magenta red blue black)
// verify\vi_patterncheck.ps1 decodes a capture PNG back into (frame, bars ok).
#pragma once
#include <cstdint>
#include <cstring>

namespace vipat {
static const uint8_t kBars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
const int kBandH = 16, kCell = 16;
// RGB (0/128/255) of pixel (x,y) for a w-wide client at frame f
inline void Pixel(int x, int y, int w, uint32_t f, uint8_t rgb[3]) {
    if (y < kBandH) {
        if (x < 32 * kCell) { uint8_t v = ((f >> (31 - x / kCell)) & 1) ? 255 : 0; rgb[0] = rgb[1] = rgb[2] = v; }
        else rgb[0] = rgb[1] = rgb[2] = 128;
        return;
    }
    int b = (int)((long long)x * 8 / (w > 0 ? w : 1)); if (b > 7) b = 7;
    memcpy(rgb, kBars[b], 3);
}
inline uint16_t Half(uint8_t v) { return v == 255 ? 0x3C00 : v == 0 ? 0 : 0x3800; }   // 1.0 / 0.0 / 0.5 (gray only)
enum Fmt { BGRA8, RGBA8, R10G10B10A2, RGBA16F };
inline int Bpp(Fmt f) { return f == RGBA16F ? 8 : 4; }
// Writes one row in the given layout.
inline void Row(Fmt fmt, int y, int w, uint32_t f, uint8_t* dst) {
    uint8_t c[3];
    for (int x = 0; x < w; x++) {
        Pixel(x, y, w, f, c);
        switch (fmt) {
        case BGRA8: dst[4 * x] = c[2]; dst[4 * x + 1] = c[1]; dst[4 * x + 2] = c[0]; dst[4 * x + 3] = 255; break;
        case RGBA8: dst[4 * x] = c[0]; dst[4 * x + 1] = c[1]; dst[4 * x + 2] = c[2]; dst[4 * x + 3] = 255; break;
        case R10G10B10A2: {
            auto ten = [](uint8_t v) -> uint32_t { return v == 255 ? 1023u : v == 0 ? 0u : 512u; };
            uint32_t p = ten(c[0]) | (ten(c[1]) << 10) | (ten(c[2]) << 20) | (3u << 30); memcpy(dst + 4 * x, &p, 4); break;
        }
        case RGBA16F: { uint16_t h[4] = {Half(c[0]), Half(c[1]), Half(c[2]), 0x3C00}; memcpy(dst + 8 * x, h, 8); break; }
        }
    }
}
}  // namespace vipat
