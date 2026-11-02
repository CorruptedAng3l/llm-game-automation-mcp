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
        const vi_vision::Image scaled = Upscale(image, scale);
        const auto maxDimension = static_cast<int>(wmo::OcrEngine::MaxImageDimension());
        if (scaled.w > maxDimension || scaled.h > maxDimension) { error = "image exceeds OcrEngine.MaxImageDimension (" + std::to_string(maxDimension) + ")"; return false; }
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
