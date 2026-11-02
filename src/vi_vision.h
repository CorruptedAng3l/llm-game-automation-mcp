// Local image helpers for vinput UI commands. Images use tightly packed BGRA bytes in client pixels.
// Template score is 1 - RGB SAD / (255 * 3 * template pixels). Alpha is preserved but not scored.
// No scaling/rotation is performed. A bounded search reports complete=false instead of claiming absence.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace vi_vision {
struct Image { int w = 0, h = 0; std::vector<std::uint8_t> pixels; };
struct Region { int x = 0, y = 0, w = 0, h = 0; };
struct Pixel { std::uint8_t r = 0, g = 0, b = 0, a = 0; };
struct Match { int x = 0, y = 0, w = 0, h = 0; double score = 0; };
struct MatchOptions {
    double threshold = 0.95;
    double nms_iou = 0.30;
    std::size_t max_matches = 100;
    std::size_t max_detections = 20000;
    std::uint64_t max_candidates = 4000000;
    std::uint64_t max_pixel_comparisons = 200000000;
};
struct MatchResult {
    std::vector<Match> matches;
    bool complete = false;
    bool truncated = false;
    std::uint64_t candidates = 0, pixel_comparisons = 0;
    std::string error;
};

inline bool Validate(const Image& image, std::string& error) {
    if (image.w <= 0 || image.h <= 0 || image.w > 16384 || image.h > 16384) {
        error = "image dimensions must be 1..16384"; return false;
    }
    const std::uint64_t area = static_cast<std::uint64_t>(image.w) * static_cast<std::uint64_t>(image.h);
    if (area > 64u * 1024u * 1024u) { error = "image exceeds 64 million pixels"; return false; }
    if (image.pixels.size() != static_cast<std::size_t>(area * 4u)) {
        error = "image BGRA byte count does not match dimensions"; return false;
    }
    error.clear(); return true;
}

namespace detail {
using Microsoft::WRL::ComPtr;
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
inline bool Failure(HRESULT result, const char* operation, std::string& error) {
    if (SUCCEEDED(result)) return false;
    char code[32]; std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    error = std::string(operation) + " failed: " + code; return true;
}
inline unsigned Difference(std::uint8_t a, std::uint8_t b) noexcept {
    return a >= b ? static_cast<unsigned>(a - b) : static_cast<unsigned>(b - a);
}
}

inline bool LoadPng(const std::wstring& path, Image& output, std::string& error) {
    detail::ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
        return !detail::Failure(com.result, "CoInitializeEx", error);
    detail::ComPtr<IWICImagingFactory> factory;
    if (detail::Failure(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory), reinterpret_cast<void**>(factory.GetAddressOf())), "WIC factory", error)) return false;
    detail::ComPtr<IWICBitmapDecoder> decoder;
    if (detail::Failure(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()), "PNG decode", error)) return false;
    GUID container{};
    if (detail::Failure(decoder->GetContainerFormat(&container), "PNG container", error)) return false;
    if (!IsEqualGUID(container, GUID_ContainerFormatPng)) { error = "template must be a PNG image"; return false; }
    detail::ComPtr<IWICBitmapFrameDecode> frame;
    if (detail::Failure(decoder->GetFrame(0, frame.GetAddressOf()), "PNG frame", error)) return false;
    UINT w = 0, h = 0;
    if (detail::Failure(frame->GetSize(&w, &h), "PNG dimensions", error)) return false;
    if (!w || !h || w > 16384 || h > 16384 || static_cast<std::uint64_t>(w) * h > 64u * 1024u * 1024u) {
        error = "PNG dimensions exceed image limits"; return false;
    }
    detail::ComPtr<IWICFormatConverter> converter;
    if (detail::Failure(factory->CreateFormatConverter(converter.GetAddressOf()), "WIC converter", error)) return false;
    if (detail::Failure(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
        nullptr, 0, WICBitmapPaletteTypeCustom), "PNG BGRA conversion", error)) return false;
    Image image; image.w = static_cast<int>(w); image.h = static_cast<int>(h);
    image.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    if (detail::Failure(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(image.pixels.size()),
        image.pixels.data()), "PNG pixels", error)) return false;
    output = std::move(image); error.clear(); return true;
}

inline bool Crop(const Image& source, const Region& region, Image& output, std::string& error) {
    if (!Validate(source, error)) return false;
    if (region.x < 0 || region.y < 0 || region.w <= 0 || region.h <= 0 ||
        static_cast<std::int64_t>(region.x) + region.w > source.w ||
        static_cast<std::int64_t>(region.y) + region.h > source.h) {
        error = "crop region must be positive and inside the client image"; return false;
    }
    Image image; image.w = region.w; image.h = region.h;
    image.pixels.resize(static_cast<std::size_t>(region.w) * region.h * 4u);
    const std::size_t stride = static_cast<std::size_t>(region.w) * 4u;
    for (int y = 0; y < region.h; ++y) {
        const std::size_t offset = (static_cast<std::size_t>(region.y + y) * source.w + region.x) * 4u;
        std::copy_n(source.pixels.data() + offset, stride, image.pixels.data() + static_cast<std::size_t>(y) * stride);
    }
    output = std::move(image); error.clear(); return true;
}

inline bool ReadPixel(const Image& image, int x, int y, Pixel& pixel, std::string& error) {
    if (!Validate(image, error)) return false;
    if (x < 0 || y < 0 || x >= image.w || y >= image.h) { error = "pixel is outside the client image"; return false; }
    const auto* p = image.pixels.data() + (static_cast<std::size_t>(y) * image.w + x) * 4u;
    pixel = {p[2], p[1], p[0], p[3]}; error.clear(); return true;
}

inline double IntersectionOverUnion(const Match& a, const Match& b) noexcept {
    const int left = (std::max)(a.x, b.x), top = (std::max)(a.y, b.y);
    const int right = (std::min)(a.x + a.w, b.x + b.w), bottom = (std::min)(a.y + a.h, b.y + b.h);
    if (right <= left || bottom <= top) return 0;
    const double intersection = static_cast<double>(right - left) * (bottom - top);
    return intersection / (static_cast<double>(a.w) * a.h + static_cast<double>(b.w) * b.h - intersection);
}

inline MatchResult MatchTemplate(const Image& image, const Image& pattern, const MatchOptions& options = {}) {
    MatchResult result;
    if (!Validate(image, result.error) || !Validate(pattern, result.error)) return result;
    if (!std::isfinite(options.threshold) || options.threshold < 0 || options.threshold > 1 ||
        !std::isfinite(options.nms_iou) || options.nms_iou < 0 || options.nms_iou > 1 ||
        !options.max_matches || !options.max_detections || !options.max_candidates || !options.max_pixel_comparisons) {
        result.error = "invalid threshold, NMS value, or zero search limit"; return result;
    }
    if (pattern.w > image.w || pattern.h > image.h) { result.complete = true; return result; }
    const std::uint64_t area = static_cast<std::uint64_t>(pattern.w) * pattern.h;
    const double denominator = static_cast<double>(area) * 765.0;
    const auto allowed = static_cast<std::uint64_t>(std::floor((1.0 - options.threshold) * denominator + 1e-7));

    // Compare high-contrast rows first. All pixels are still compared for an accepted match;
    // the ordering changes only how soon exact lower-bound rejection can stop a bad candidate.
    double mean[3]{};
    for (std::size_t p = 0; p < pattern.pixels.size(); p += 4)
        for (int c = 0; c < 3; ++c) mean[c] += pattern.pixels[p + c];
    for (double& value : mean) value /= static_cast<double>(area);
    struct Row { int y; double activity; };
    std::vector<Row> rows; rows.reserve(static_cast<std::size_t>(pattern.h));
    for (int y = 0; y < pattern.h; ++y) {
        double activity = 0;
        for (int x = 0; x < pattern.w; ++x) {
            const auto* pixel = pattern.pixels.data() + (static_cast<std::size_t>(y) * pattern.w + x) * 4u;
            for (int c = 0; c < 3; ++c) activity += std::abs(pixel[c] - mean[c]);
        }
        rows.push_back({y, activity});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.activity > b.activity; });
    std::vector<Match> detections;
    bool stopped = false;
    for (int y = 0; y <= image.h - pattern.h && !stopped; ++y) {
        for (int x = 0; x <= image.w - pattern.w; ++x) {
            if (result.candidates >= options.max_candidates) { result.error = "candidate search budget reached"; stopped = true; break; }
            ++result.candidates;
            std::uint64_t sad = 0;
            bool rejected = false;
            for (const Row& row : rows) {
                const auto* source = image.pixels.data() + (static_cast<std::size_t>(y + row.y) * image.w + x) * 4u;
                const auto* target = pattern.pixels.data() + static_cast<std::size_t>(row.y) * pattern.w * 4u;
                for (int tx = 0; tx < pattern.w; ++tx, source += 4, target += 4) {
                    if (result.pixel_comparisons >= options.max_pixel_comparisons) {
                        result.error = "pixel comparison budget reached"; stopped = true; break;
                    }
                    ++result.pixel_comparisons;
                    sad += detail::Difference(source[0], target[0]) + detail::Difference(source[1], target[1]) + detail::Difference(source[2], target[2]);
                    if (sad > allowed) { rejected = true; break; }
                }
                if (rejected || stopped) break;
            }
            if (stopped) break;
            if (!rejected) {
                if (detections.size() >= options.max_detections) { result.error = "matching candidate limit reached"; stopped = true; break; }
                detections.push_back({x, y, pattern.w, pattern.h, 1.0 - static_cast<double>(sad) / denominator});
            }
        }
    }
    result.complete = !stopped;
    std::stable_sort(detections.begin(), detections.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
    for (const auto& match : detections) {
        bool suppressed = false;
        for (const auto& existing : result.matches) {
            if (IntersectionOverUnion(match, existing) > options.nms_iou) { suppressed = true; break; }
        }
        if (suppressed) continue;
        if (result.matches.size() >= options.max_matches) { result.truncated = true; break; }
        result.matches.push_back(match);
    }
    return result;
}
}
