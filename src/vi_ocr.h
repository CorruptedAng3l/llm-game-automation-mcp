// Windows.Media.Ocr wrapper for the vinput CLI (never loaded into a game process).
// Input: BGRA image in capture pixels. Output: lines/words with boxes in the SAME pixel space
// (boxes are divided back by the up-scale factor).
// The WinRT OcrEngine reports NO per-word confidence, so none is invented here.
#pragma once
#include "vi_text.h"
#include "vi_vision.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma comment(lib, "windowsapp.lib")

namespace vi_ocr {
// All preprocessing preserves the source pixel grid; recognition divides any upscale back out.
// Contrast uses 1/99 percentiles so a stray bright HUD pixel does not dominate a dim label.
inline bool Preprocess(const vi_vision::Image& src, const std::string& mode, vi_vision::Image& dst, std::string& error) {
    if (!vi_vision::Validate(src, error)) return false;
    if (mode == "none") { dst = src; return true; }
    if (mode != "gray" && mode != "contrast" && mode != "threshold" && mode != "invert") {
        error = "--preprocess must be none, gray, contrast, threshold, invert or auto"; return false;
    }
    dst = src;
    size_t histogram[256] = {}, total = static_cast<size_t>(src.w) * src.h;
    for (size_t i = 0; i < total; ++i) {
        const size_t p = i * 4;
        const unsigned y = (29u * src.pixels[p] + 150u * src.pixels[p + 1] + 77u * src.pixels[p + 2] + 128u) >> 8;
        dst.pixels[p] = dst.pixels[p + 1] = dst.pixels[p + 2] = static_cast<uint8_t>(y); dst.pixels[p + 3] = 255;
        ++histogram[y];
    }
    if (mode == "gray") return true;
    int low = 0, high = 255; size_t count = 0;
    for (int i = 0; i < 256; ++i) { count += histogram[i]; if (count > total / 100) { low = i; break; } }
    count = 0;
    for (int i = 255; i >= 0; --i) { count += histogram[i]; if (count > total / 100) { high = i; break; } }
    if (high <= low) {
        for (int i = 0; i < 256; ++i) if (histogram[i]) { low = i; break; }
        for (int i = 255; i >= 0; --i) if (histogram[i]) { high = i; break; }
    }
    // Otsu's split chooses a threshold without assuming a light or dark theme.
    int threshold = low; double sum = 0, sumLow = 0, best = -1; size_t nLow = 0;
    if (mode == "threshold") {
        for (int i = 0; i < 256; ++i) sum += static_cast<double>(i) * histogram[i];
        for (int i = 0; i < 255; ++i) {
            nLow += histogram[i]; sumLow += static_cast<double>(i) * histogram[i];
            if (!nLow || nLow == total) continue;
            const double delta = sumLow / nLow - (sum - sumLow) / (total - nLow);
            const double variance = static_cast<double>(nLow) * (total - nLow) * delta * delta;
            if (variance > best) { best = variance; threshold = i; }
        }
    }
    size_t dark = 0; for (int i = 0; i <= threshold; ++i) dark += histogram[i];
    for (size_t i = 0; i < total; ++i) {
        const size_t p = i * 4; int v = dst.pixels[p];
        if (mode == "threshold") { v = v > threshold ? 255 : 0; if (dark > total / 2) v = 255 - v; }
        else {
            if (high > low) v = (std::clamp)((v - low) * 255 / (high - low), 0, 255);
            if (mode == "invert") v = 255 - v;
        }
        dst.pixels[p] = dst.pixels[p + 1] = dst.pixels[p + 2] = static_cast<uint8_t>(v);
    }
    return true;
}
inline std::string Narrow(const winrt::hstring& h) {
    std::wstring w(h.c_str(), h.size());
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Bilinear up-scale (integer or fractional factor >= 1) of a BGRA image.
inline vi_vision::Image Upscale(const vi_vision::Image& src, double factor) {
    if (factor <= 1.0001) return src;
    vi_vision::Image out;
    out.w = static_cast<int>(src.w * factor + 0.5); out.h = static_cast<int>(src.h * factor + 0.5);
    out.pixels.resize(static_cast<size_t>(out.w) * out.h * 4u);
    for (int y = 0; y < out.h; ++y) {
        const double fy = (y + 0.5) / factor - 0.5; const int y0 = (std::max)(0, static_cast<int>(std::floor(fy))), y1 = (std::min)(src.h - 1, y0 + 1);
        const double wy = (std::max)(0.0, fy - y0);
        for (int x = 0; x < out.w; ++x) {
            const double fx = (x + 0.5) / factor - 0.5; const int x0 = (std::max)(0, static_cast<int>(std::floor(fx))), x1 = (std::min)(src.w - 1, x0 + 1);
            const double wx = (std::max)(0.0, fx - x0);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int xx, int yy) { return static_cast<double>(src.pixels[(static_cast<size_t>(yy) * src.w + xx) * 4u + c]); };
                const double v = (at(x0, y0) * (1 - wx) + at(x1, y0) * wx) * (1 - wy) + (at(x0, y1) * (1 - wx) + at(x1, y1) * wx) * wy;
                out.pixels[(static_cast<size_t>(y) * out.w + x) * 4u + c] = static_cast<std::uint8_t>(v + 0.5);
            }
        }
    }
    return out;
}

// scale >= 1. lang empty = the user's profile languages. Returns false with `error` on failure.
inline bool Recognize(const vi_vision::Image& image, double scale, const std::string& lang,
                      std::vector<vi_text::Line>& lines, std::string& usedLanguage, std::string& error) {
    namespace wgi = winrt::Windows::Graphics::Imaging;
    namespace wmo = winrt::Windows::Media::Ocr;
    namespace wss = winrt::Windows::Storage::Streams;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        wmo::OcrEngine engine{nullptr};
        if (lang.empty()) engine = wmo::OcrEngine::TryCreateFromUserProfileLanguages();
        else {
            std::wstring wl(lang.begin(), lang.end());
            auto language = winrt::Windows::Globalization::Language(winrt::hstring(wl));
            if (!wmo::OcrEngine::IsLanguageSupported(language)) { error = "OCR language not supported/installed: " + lang; return false; }
            engine = wmo::OcrEngine::TryCreateFromLanguage(language);
        }
        if (!engine) { error = "no OCR engine for the requested language (install an OCR language pack)"; return false; }
        usedLanguage = Narrow(engine.RecognizerLanguage().LanguageTag());
        const auto maxDimension = static_cast<int>(wmo::OcrEngine::MaxImageDimension());
        // Check BEFORE allocating a scaled bitmap (a 4K frame at 5x would otherwise allocate ~800 MB).
        if (!(scale >= 1 && scale <= 5) || image.w <= 0 || image.h <= 0 || image.w * scale > maxDimension || image.h * scale > maxDimension) {
            error = "image exceeds OcrEngine.MaxImageDimension (" + std::to_string(maxDimension) + "); use --region or tiled text search"; return false;
        }
        const vi_vision::Image scaled = Upscale(image, scale);
        wss::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>(scaled.pixels.data(), scaled.pixels.data() + scaled.pixels.size()));
        wgi::SoftwareBitmap bitmap(wgi::BitmapPixelFormat::Bgra8, scaled.w, scaled.h, wgi::BitmapAlphaMode::Ignore);
        bitmap.CopyFromBuffer(writer.DetachBuffer());
        wmo::OcrResult result = engine.RecognizeAsync(bitmap).get();
        lines.clear();
        for (auto const& line : result.Lines()) {
            vi_text::Line out; out.text = std::wstring(line.Text().c_str(), line.Text().size());
            bool first = true;
            for (auto const& word : line.Words()) {
                const auto r = word.BoundingRect();
                vi_text::Word w; w.text = std::wstring(word.Text().c_str(), word.Text().size());
                const int left = static_cast<int>(std::floor(r.X / scale)), top = static_cast<int>(std::floor(r.Y / scale));
                const int right = static_cast<int>(std::ceil((r.X + r.Width) / scale)), bottom = static_cast<int>(std::ceil((r.Y + r.Height) / scale));
                w.box = {left, top, right - left, bottom - top};
                out.box = first ? w.box : vi_text::Union(out.box, w.box); first = false;
                out.words.push_back(std::move(w));
            }
            if (!out.words.empty()) lines.push_back(std::move(out));
        }
        return true;
    } catch (winrt::hresult_error const& e) {
        error = "WinRT OCR failed: " + Narrow(e.message()); return false;
    } catch (std::exception const& e) {
        error = std::string("OCR failed: ") + e.what(); return false;
    }
}
}
