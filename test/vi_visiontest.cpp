#include "vi_vision.h"
#include <chrono>
#include <climits>
#include <cstdio>

using namespace vi_vision;
static int failures = 0;
static void Check(bool value, const char* name) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
    if (!value) ++failures;
}
static Image Solid(int w, int h, std::uint8_t value) {
    Image image; image.w = w; image.h = h; image.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t p = 0; p < image.pixels.size(); p += 4) {
        image.pixels[p] = image.pixels[p + 1] = image.pixels[p + 2] = value; image.pixels[p + 3] = 255;
    }
    return image;
}
static Image Pattern(int w, int h) {
    Image image = Solid(w, h, 0);
    std::uint32_t state = 0x3197a54bu;
    for (std::size_t p = 0; p < image.pixels.size(); p += 4)
        for (int c = 0; c < 3; ++c) { state = state * 1664525u + 1013904223u; image.pixels[p + c] = static_cast<std::uint8_t>(40u + (state >> 24) % 176u); }
    return image;
}
static void Paste(Image& image, const Image& pattern, int x, int y, int noise = 0) {
    for (int ty = 0; ty < pattern.h; ++ty) for (int tx = 0; tx < pattern.w; ++tx) {
        auto* out = image.pixels.data() + (static_cast<std::size_t>(y + ty) * image.w + x + tx) * 4u;
        const auto* in = pattern.pixels.data() + (static_cast<std::size_t>(ty) * pattern.w + tx) * 4u;
        for (int c = 0; c < 3; ++c) out[c] = static_cast<std::uint8_t>(in[c] + (((tx + ty + c) & 1) ? noise : -noise));
        out[3] = in[3];
    }
}
static bool At(const MatchResult& result, int x, int y) {
    for (const auto& match : result.matches) if (match.x == x && match.y == y) return true;
    return false;
}
static bool SavePngFixture(const std::wstring& path, const Image& image, std::string& error) {
    detail::ComScope com;
    detail::ComPtr<IWICImagingFactory> factory;
    if (detail::Failure(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory), reinterpret_cast<void**>(factory.GetAddressOf())), "fixture factory", error)) return false;
    detail::ComPtr<IWICStream> stream;
    if (detail::Failure(factory->CreateStream(stream.GetAddressOf()), "fixture stream", error)) return false;
    if (detail::Failure(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "fixture output", error)) return false;
    detail::ComPtr<IWICBitmapEncoder> encoder;
    if (detail::Failure(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()), "fixture encoder", error)) return false;
    if (detail::Failure(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "fixture initialize", error)) return false;
    detail::ComPtr<IWICBitmapFrameEncode> frame;
    if (detail::Failure(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr), "fixture frame", error)) return false;
    if (detail::Failure(frame->Initialize(nullptr), "fixture frame initialize", error)) return false;
    if (detail::Failure(frame->SetSize(static_cast<UINT>(image.w), static_cast<UINT>(image.h)), "fixture size", error)) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (detail::Failure(frame->SetPixelFormat(&format), "fixture pixel format", error)) return false;
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) { error = "PNG encoder changed fixture pixel format"; return false; }
    auto bgra = image.pixels;
    if (detail::Failure(frame->WritePixels(static_cast<UINT>(image.h), static_cast<UINT>(image.w) * 4u,
        static_cast<UINT>(bgra.size()), bgra.data()), "fixture pixels", error)) return false;
    if (detail::Failure(frame->Commit(), "fixture frame commit", error)) return false;
    return !detail::Failure(encoder->Commit(), "fixture encoder commit", error);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    Image pattern = Pattern(8, 6), image = Solid(96, 72, 250);
    Paste(image, pattern, 13, 19); Paste(image, pattern, 60, 40);
    MatchOptions options; options.threshold = 1;
    const auto exact = MatchTemplate(image, pattern, options);
    Check(exact.complete && !exact.truncated && exact.matches.size() == 2 && At(exact, 13, 19) && At(exact, 60, 40), "two exact instances return client boxes");
    Check(exact.matches.size() == 2 && exact.matches[0].score == 1 && exact.matches[1].score == 1, "exact scores equal one");

    Image noisyPattern = Pattern(16, 12), noisy = Solid(160, 100, 250);
    Paste(noisy, noisyPattern, 40, 55, 2); options.threshold = 0.99;
    const auto noisyMatch = MatchTemplate(noisy, noisyPattern, options);
    Check(noisyMatch.complete && noisyMatch.matches.size() == 1 && At(noisyMatch, 40, 55) && noisyMatch.matches[0].score >= 0.99 && noisyMatch.matches[0].score < 1,
        "small RGB noise matches within threshold");
    const auto absent = MatchTemplate(Solid(160, 100, 250), noisyPattern, options);
    Check(absent.complete && absent.matches.empty(), "negative fixture returns complete zero with preceding positive control");

    Image constant = Solid(12, 8, 100), tile = Solid(3, 3, 100); options.threshold = 1;
    auto nms = MatchTemplate(constant, tile, options);
    bool overlaps = false;
    for (std::size_t a = 0; a < nms.matches.size(); ++a) for (std::size_t b = a + 1; b < nms.matches.size(); ++b)
        overlaps = overlaps || IntersectionOverUnion(nms.matches[a], nms.matches[b]) > options.nms_iou;
    Check(nms.complete && !nms.matches.empty() && !overlaps, "NMS suppresses overlapping boxes");
    options.max_matches = 2;
    const auto capped = MatchTemplate(constant, tile, options);
    Check(capped.complete && capped.truncated && capped.matches.size() == 2, "result cap reports truncation separately from search completeness");

    options = {}; options.max_pixel_comparisons = 2;
    const auto budget = MatchTemplate(image, pattern, options);
    Check(!budget.complete && budget.pixel_comparisons == 2 && !budget.error.empty(), "work budget reports incomplete instead of false absence");
    options = {}; options.max_candidates = 2;
    const auto candidateBudget = MatchTemplate(image, pattern, options);
    Check(!candidateBudget.complete && candidateBudget.candidates == 2 && !candidateBudget.error.empty(), "candidate budget reports incomplete");
    options = {}; options.max_detections = 2; options.threshold = 1;
    const auto detectionBudget = MatchTemplate(constant, tile, options);
    Check(!detectionBudget.complete && !detectionBudget.error.empty(), "matching candidate memory cap reports incomplete");
    options = {}; options.threshold = std::numeric_limits<double>::quiet_NaN();
    Check(!MatchTemplate(image, pattern, options).complete, "NaN threshold rejected");
    Check(MatchTemplate(tile, constant).complete && MatchTemplate(tile, constant).matches.empty(), "larger-than-image template has complete zero");

    Image cropped; std::string error;
    Check(Crop(image, {13, 19, 8, 6}, cropped, error) && cropped.pixels == pattern.pixels, "crop uses exact BGRA client region");
    Check(!Crop(image, {INT_MAX, 0, INT_MAX, 1}, cropped, error), "overflowing crop bounds rejected");
    Pixel pixel;
    Check(ReadPixel(pattern, 0, 0, pixel, error) && pixel.r == pattern.pixels[2] && pixel.g == pattern.pixels[1] &&
        pixel.b == pattern.pixels[0] && pixel.a == 255, "pixel returns RGBA from BGRA bytes");
    Check(!ReadPixel(pattern, pattern.w, 0, pixel, error), "outside pixel rejected");
    Image malformed = pattern; malformed.pixels.pop_back();
    Check(!Validate(malformed, error), "malformed BGRA length rejected");

    const std::wstring fixture = std::wstring(argv[1]) + L"\\vision_fixture.png";
    const bool saved = SavePngFixture(fixture, pattern, error);
    if (!saved) std::printf("PNG fixture error: %s\n", error.c_str());
    Image loaded;
    const bool decoded = saved && LoadPng(fixture, loaded, error);
    if (!decoded) std::printf("PNG decode error: %s\n", error.c_str());
    Check(decoded && loaded.w == pattern.w && loaded.h == pattern.h && loaded.pixels == pattern.pixels, "WIC PNG roundtrip preserves exact BGRA pixels");
    Check(!LoadPng(std::wstring(argv[1]) + L"\\missing_fixture.png", loaded, error) && !error.empty(), "missing PNG returns diagnostic");

    Image large = Solid(1920, 1080, 250), marker = Pattern(24, 16); Paste(large, marker, 1712, 927);
    options = {}; options.threshold = 0.98;
    const auto start = std::chrono::steady_clock::now();
    const auto timed = MatchTemplate(large, marker, options);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    Check(timed.complete && timed.matches.size() == 1 && At(timed, 1712, 927), "1080p fixture completes within default work limits");
    std::printf("BENCH image=1920x1080 template=24x16 threshold=0.98 ms=%lld candidates=%llu comparisons=%llu complete=%d\n",
        static_cast<long long>(ms), static_cast<unsigned long long>(timed.candidates), static_cast<unsigned long long>(timed.pixel_comparisons), timed.complete);
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
