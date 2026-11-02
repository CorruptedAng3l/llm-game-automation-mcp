// Normalized cross-correlation template matching for vinput UI navigation (CLI process only).
//
//  * Greyscale (Rec.601) zero-mean NCC, so uniform brightness/contrast changes do not matter. Score range -1..1;
//    1.0 = identical up to gain/offset. A flat (textureless) template is refused: NCC is undefined for it.
//  * Scale: the template is tried at several scales (default 0.85 0.92 1.0 1.08 1.18) and each hit reports the
//    scale that matched. Larger differences (e.g. a UI scaled 1.5x) are NOT found: pass --scales explicitly.
//    Rotation, perspective and non-uniform scaling are not handled.
//  * Speed: coarse search on a box-downsampled pyramid level (factor 1/2/4 chosen so the template keeps >= 8 px),
//    then a full-resolution refinement around each coarse peak. Alpha is ignored.
#pragma once
#include "vi_vision.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace vi_ncc {
using vi_vision::Image;
struct Match { int x = 0, y = 0, w = 0, h = 0; double score = 0, scale = 1; };
struct Options {
    double threshold = 0.80;
    std::vector<double> scales{0.85, 0.92, 1.0, 1.08, 1.18};
    std::size_t max_matches = 50;
    double nms_iou = 0.30;
};
struct Result { std::vector<Match> matches; Match best; bool have_best = false; std::string error; };

struct Plane { int w = 0, h = 0; std::vector<float> v; };

inline Plane Gray(const Image& im) {
    Plane p; p.w = im.w; p.h = im.h; p.v.resize(static_cast<size_t>(im.w) * im.h);
    for (size_t i = 0; i < p.v.size(); ++i) {
        const std::uint8_t* q = &im.pixels[i * 4u];
        p.v[i] = 0.114f * q[0] + 0.587f * q[1] + 0.299f * q[2];
    }
    return p;
}
inline Plane Resample(const Plane& s, int nw, int nh) {  // area average when shrinking, bilinear when growing
    Plane o; o.w = nw; o.h = nh; o.v.resize(static_cast<size_t>(nw) * nh);
    const double sx = static_cast<double>(s.w) / nw, sy = static_cast<double>(s.h) / nh;
    for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) {
        if (sx >= 1.0 || sy >= 1.0) {
            const int x0 = static_cast<int>(x * sx), x1 = (std::min)(s.w, (std::max)(x0 + 1, static_cast<int>(std::ceil((x + 1) * sx))));
            const int y0 = static_cast<int>(y * sy), y1 = (std::min)(s.h, (std::max)(y0 + 1, static_cast<int>(std::ceil((y + 1) * sy))));
            double acc = 0; int n = 0;
            for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) { acc += s.v[static_cast<size_t>(yy) * s.w + xx]; ++n; }
            o.v[static_cast<size_t>(y) * nw + x] = static_cast<float>(acc / (std::max)(1, n));
        } else {
            const double fx = (x + 0.5) * sx - 0.5, fy = (y + 0.5) * sy - 0.5;
            const int x0 = (std::max)(0, static_cast<int>(std::floor(fx))), y0 = (std::max)(0, static_cast<int>(std::floor(fy)));
            const int x1 = (std::min)(s.w - 1, x0 + 1), y1 = (std::min)(s.h - 1, y0 + 1);
            const double wx = (std::max)(0.0, fx - x0), wy = (std::max)(0.0, fy - y0);
            auto at = [&](int xx, int yy) { return static_cast<double>(s.v[static_cast<size_t>(yy) * s.w + xx]); };
            o.v[static_cast<size_t>(y) * nw + x] = static_cast<float>((at(x0, y0) * (1 - wx) + at(x1, y0) * wx) * (1 - wy) + (at(x0, y1) * (1 - wx) + at(x1, y1) * wx) * wy);
        }
    }
    return o;
}

struct Template {
    int w = 0, h = 0; std::vector<float> zero_mean; double norm = 0;
    explicit Template(const Plane& p) : w(p.w), h(p.h), zero_mean(p.v) {
        double mean = 0; for (float f : p.v) mean += f; mean /= static_cast<double>(p.v.size());
        double sq = 0; for (float& f : zero_mean) { f = static_cast<float>(f - mean); sq += static_cast<double>(f) * f; }
        norm = std::sqrt(sq);
    }
    bool Flat() const { return norm / std::sqrt(static_cast<double>(w) * h) < 2.0; }  // std-dev below 2 grey levels
};

// Score of the template at (x, y). Window must fit.
inline double ScoreAt(const Plane& im, const Template& t, int x, int y) {
    double sum = 0, sum2 = 0, cross = 0;
    for (int ty = 0; ty < t.h; ++ty) {
        const float* row = &im.v[static_cast<size_t>(y + ty) * im.w + x];
        const float* tz = &t.zero_mean[static_cast<size_t>(ty) * t.w];
        for (int tx = 0; tx < t.w; ++tx) { const double v = row[tx]; sum += v; sum2 += v * v; cross += v * tz[tx]; }
    }
    const double n = static_cast<double>(t.w) * t.h, var = sum2 - sum * sum / n;
    if (var < 1e-3 * n) return 0.0;
    return cross / (t.norm * std::sqrt(var));
}

// Full coarse map via integral images.
inline void ScoreMap(const Plane& im, const Template& t, std::vector<float>& out, int& ow, int& oh) {
    ow = im.w - t.w + 1; oh = im.h - t.h + 1; out.assign(static_cast<size_t>((std::max)(0, ow)) * (std::max)(0, oh), 0.f);
    if (ow <= 0 || oh <= 0) return;
    const size_t iw = static_cast<size_t>(im.w) + 1;
    std::vector<double> s1((im.h + 1) * iw, 0.0), s2((im.h + 1) * iw, 0.0);
    for (int y = 0; y < im.h; ++y) {
        double r1 = 0, r2 = 0;
        for (int x = 0; x < im.w; ++x) {
            const double v = im.v[static_cast<size_t>(y) * im.w + x]; r1 += v; r2 += v * v;
            s1[(y + 1) * iw + x + 1] = s1[y * iw + x + 1] + r1; s2[(y + 1) * iw + x + 1] = s2[y * iw + x + 1] + r2;
        }
    }
    const double n = static_cast<double>(t.w) * t.h;
    for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) {
        const size_t a = static_cast<size_t>(y) * iw + x, b = a + t.w, c = static_cast<size_t>(y + t.h) * iw + x, d = c + t.w;
        const double sum = s1[d] - s1[b] - s1[c] + s1[a], sum2 = s2[d] - s2[b] - s2[c] + s2[a], var = sum2 - sum * sum / n;
        if (var < 1e-3 * n) continue;
        double cross = 0;
        for (int ty = 0; ty < t.h; ++ty) {
            const float* row = &im.v[static_cast<size_t>(y + ty) * im.w + x]; const float* tz = &t.zero_mean[static_cast<size_t>(ty) * t.w];
            for (int tx = 0; tx < t.w; ++tx) cross += static_cast<double>(row[tx]) * tz[tx];
        }
        out[static_cast<size_t>(y) * ow + x] = static_cast<float>(cross / (t.norm * std::sqrt(var)));
    }
}

inline double Iou(const Match& a, const Match& b) {
    const int l = (std::max)(a.x, b.x), t = (std::max)(a.y, b.y), r = (std::min)(a.x + a.w, b.x + b.w), bo = (std::min)(a.y + a.h, b.y + b.h);
    if (r <= l || bo <= t) return 0;
    const double inter = static_cast<double>(r - l) * (bo - t);
    return inter / (static_cast<double>(a.w) * a.h + static_cast<double>(b.w) * b.h - inter);
}

inline Result Find(const Image& image, const Image& pattern, const Options& options = {}) {
    Result result;
    if (!vi_vision::Validate(image, result.error) || !vi_vision::Validate(pattern, result.error)) return result;
    if (!(options.threshold > -1.0 && options.threshold <= 1.0) || options.scales.empty()) { result.error = "invalid threshold or scale list"; return result; }
    const Plane im = Gray(image), tp = Gray(pattern);
    if (Template(tp).Flat()) { result.error = "template has no texture (flat colour): normalized correlation is undefined for it"; return result; }
    std::vector<Match> pool;
    for (double scale : options.scales) {
        if (!(scale >= 0.2 && scale <= 5.0)) { result.error = "scale must be 0.2..5"; return result; }
        const int sw = (std::max)(1, static_cast<int>(std::lround(tp.w * scale))), sh = (std::max)(1, static_cast<int>(std::lround(tp.h * scale)));
        if (sw > im.w || sh > im.h || sw < 4 || sh < 4) continue;
        const Plane full = (sw == tp.w && sh == tp.h) ? tp : Resample(tp, sw, sh);
        const Template tfull(full);
        if (tfull.Flat()) continue;
        const int smallest = (std::min)(sw, sh), f = smallest >= 32 ? 4 : smallest >= 16 ? 2 : 1;
        std::vector<Match> peaks;
        if (f == 1) {
            std::vector<float> map; int ow, oh; ScoreMap(im, tfull, map, ow, oh);
            for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) {
                const float v = map[static_cast<size_t>(y) * ow + x]; if (v < options.threshold) continue;
                bool peak = true;
                for (int dy = -1; dy <= 1 && peak; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy; if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= ow || ny >= oh) continue;
                    if (map[static_cast<size_t>(ny) * ow + nx] > v) { peak = false; break; }
                }
                if (peak) peaks.push_back({x, y, sw, sh, v, scale});
            }
        } else {
            const Plane imc = Resample(im, (std::max)(1, im.w / f), (std::max)(1, im.h / f)), tc = Resample(full, (std::max)(2, sw / f), (std::max)(2, sh / f));
            const Template tcoarse(tc);
            if (tcoarse.Flat() || tc.w > imc.w || tc.h > imc.h) continue;
            std::vector<float> map; int ow, oh; ScoreMap(imc, tcoarse, map, ow, oh);
            const double loose = (std::max)(0.2, options.threshold - 0.25);
            std::vector<Match> coarse;
            for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) {
                const float v = map[static_cast<size_t>(y) * ow + x]; if (v < loose) continue;
                bool peak = true;
                for (int dy = -1; dy <= 1 && peak; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy; if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= ow || ny >= oh) continue;
                    if (map[static_cast<size_t>(ny) * ow + nx] > v) { peak = false; break; }
                }
                if (peak) coarse.push_back({x * f, y * f, sw, sh, v, scale});
            }
            std::sort(coarse.begin(), coarse.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
            if (coarse.size() > 60) coarse.resize(60);
            for (const Match& c : coarse) {
                Match best = c; best.score = -2;
                for (int dy = -(f + 1); dy <= f + 1; ++dy) for (int dx = -(f + 1); dx <= f + 1; ++dx) {
                    const int x = c.x + dx, y = c.y + dy; if (x < 0 || y < 0 || x + sw > im.w || y + sh > im.h) continue;
                    const double s = ScoreAt(im, tfull, x, y); if (s > best.score) { best.x = x; best.y = y; best.score = s; }
                }
                if (best.score >= options.threshold) peaks.push_back(best);
            }
        }
        for (const Match& p : peaks) pool.push_back(p);
    }
    std::stable_sort(pool.begin(), pool.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
    for (const Match& m : pool) {
        bool suppressed = false;
        for (const Match& e : result.matches) if (Iou(m, e) > options.nms_iou) { suppressed = true; break; }
        if (suppressed) continue;
        if (result.matches.size() >= options.max_matches) break;
        result.matches.push_back(m);
    }
    if (!result.matches.empty()) { result.best = result.matches.front(); result.have_best = true; }
    return result;
}
}
