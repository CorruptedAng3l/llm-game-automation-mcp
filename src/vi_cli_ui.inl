// vi_cli_ui.inl - UI-navigation commands of vinput.exe (textually included by vinput_cli.cpp, after the pipe client,
// Args helpers, Reply and WaitChange). Everything here runs in the CLI process; nothing is loaded into the game.
//   ocr  click-text  wait-text  assert-text  find-image  click-image  wait-image  pixel  scroll  drag  script
// Boxes in replies are [x,y,w,h] in CLIENT pixels (identical to capture pixels when the swapchain matches the window;
// the capture's origin/scale is applied when --region or a differently-sized swapchain is in play).

static double JsonNum(const std::string& json, const char* key, double def = 0) {
    std::string mark = "\"" + std::string(key) + "\":";
    size_t p = json.find(mark); if (p == std::string::npos) return def;
    return atof(json.c_str() + p + mark.size());
}
static std::string JU(const std::wstring& w) { return JEsc(W2U(w)); }
static std::string Num(double v) { char b[64]; sprintf_s(b, "%.3f", v); std::string s = b; while (s.size() > 1 && s.back() == '0') s.pop_back(); if (!s.empty() && s.back() == '.') s.pop_back(); return s; }

struct Frame {
    vi_vision::Image img;
    double ox = 0, oy = 0, sx = 1, sy = 1;   // client = origin + image * scale
    int clientW = 0, clientH = 0;
    std::string method, hash;
};
static vi_text::Box ToClient(const Frame& f, const vi_text::Box& b) {
    const int x0 = (int)std::lround(f.ox + b.x * f.sx), y0 = (int)std::lround(f.oy + b.y * f.sy);
    const int x1 = (int)std::lround(f.ox + (b.x + b.w) * f.sx), y1 = (int)std::lround(f.oy + (b.y + b.h) * f.sy);
    return {x0, y0, x1 - x0, y1 - y0};
}
static std::string BoxJ(const vi_text::Box& b) { return "[" + std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(b.w) + "," + std::to_string(b.h) + "]"; }

// <dist>\run\logs, created and granted to AppContainers (a UWP target can only write there).
static std::wstring AcLogsDir() {
    // An inherited logs directory may be owned by another/elevated agent. Create a
    // caller-owned staging directory so granting its DACL does not require elevation.
    static const std::wstring logs = ExeDir() + L"\\run\\capture_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    CreateDirectoryW((ExeDir() + L"\\run").c_str(), nullptr); CreateDirectoryW(logs.c_str(), nullptr);
    std::string e;
    if (!GrantAppContainers(logs, FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE, true, e)) {
        fprintf(stderr,"[vinput] capture staging permission failed: %s\n",e.c_str()); RemoveDirectoryW(logs.c_str()); return {};
    }
    return logs;
}
static bool Grab(DWORD pid, const Args& a, Frame& f, std::string& err) {
    std::wstring dir = QueryTargetToken(pid).appContainer ? AcLogsDir() : ExeDir() + L"\\logs"; CreateDirectoryW(dir.c_str(), nullptr);
    if (dir.empty()) { err="cannot grant AppContainer capture staging directory"; return false; }
    std::wstring path = dir + L"\\ui_" + std::to_wstring(pid) + L"_" + std::to_wstring(GetCurrentProcessId()) + L".png";
    std::string cmd = "{"; PutS(cmd, "cmd", "capture"); PutS(cmd, "path", W2U(path));
    if (HasOpt(a, "region")) PutS(cmd, "region", GetOpt(a, "region"));
    if (HasOpt(a, "method")) PutS(cmd, "method", GetOpt(a, "method"));
    if (HasOpt(a, "after-frames")) Put(cmd, "after_frames", GetOpt(a, "after-frames"));
    cmd += "}";
    std::string resp;
    if (!Request(pid, cmd, resp) || resp.find("\"ok\":true") == std::string::npos) {
        err = JsonText(resp, "error"); if (err.empty()) err = resp; return false;
    }
    std::string e;
    if (!vi_vision::LoadPng(path, f.img, e)) { err = "cannot read capture PNG: " + e; DeleteFileW(path.c_str()); return false; }
    DeleteFileW(path.c_str());
    if (QueryTargetToken(pid).appContainer) RemoveDirectoryW(dir.c_str());
    const double w = JsonNum(resp, "w"), h = JsonNum(resp, "h");
    f.ox = JsonNum(resp, "origin_x"); f.oy = JsonNum(resp, "origin_y");
    f.sx = w > 0 ? JsonNum(resp, "region_client_w", w) / w : 1; f.sy = h > 0 ? JsonNum(resp, "region_client_h", h) / h : 1;
    f.clientW = (int)JsonNum(resp, "client_w"); f.clientH = (int)JsonNum(resp, "client_h");
    f.method = JsonText(resp, "method"); f.hash = JsonText(resp, "hash");
    return true;
}

// ---- OCR ---------------------------------------------------------------------------------------------------------
static std::vector<double> ParseScales(const std::string& s, bool& autoScale, std::string& err) {
    autoScale = false; std::vector<double> v;
    if (s.empty() || s == "auto") { autoScale = true; return {1.0, 2.0}; }
    size_t i = 0;
    while (i <= s.size()) {
        size_t c = s.find(',', i); if (c == std::string::npos) c = s.size();
        std::string t = s.substr(i, c - i); char* end = nullptr; double d = strtod(t.c_str(), &end);
        if (t.empty() || *end || !(d >= 0.2 && d <= 5)) { err = "scale list must be numbers 0.2..5 (or auto)"; return {}; }
        v.push_back(d); i = c + 1;
    }
    return v;
}
static bool OcrOnce(const Frame& f, double scale, const Args& a, std::vector<vi_text::Line>& lines, std::string& lang, std::string& err) {
    return vi_ocr::Recognize(f.img, (std::max)(1.0, scale), GetOpt(a, "lang"), lines, lang, err);
}
// Windows OCR layout analysis is not stable: the same words can vanish from a full frame and appear in a crop
// (seen on the synthetic target: OPTIONS and QUIT missing at 800x450, found in a 400x450 half). Tiled pass: 2x3
// overlapping tiles, merged into `lines` (a tile line replaces an overlapping line only when its text is longer).
static double LineOverlap(const vi_text::Box& a, const vi_text::Box& b) {
    const int l = (std::max)(a.x, b.x), t = (std::max)(a.y, b.y), r = (std::min)(a.x + a.w, b.x + b.w), bo = (std::min)(a.y + a.h, b.y + b.h);
    if (r <= l || bo <= t) return 0;
    const double inter = (double)(r - l) * (bo - t);
    return inter / (std::min)((double)a.w * a.h, (double)b.w * b.h);
}
static bool OcrTiles(const Frame& f, double scale, const Args& a, std::vector<vi_text::Line>& lines, std::string& lang, std::string& err) {
    const int W = f.img.w, H = f.img.h;
    if (W < 64 || H < 64) return true;
    const int tw = (std::min)(W, (int)std::ceil(W * 0.6)), th = (std::min)(H, (int)std::ceil(H * 0.45));
    const int xs[2] = {0, W - tw}, ys[3] = {0, (H - th) / 2, H - th};
    for (int yi = 0; yi < 3; ++yi) for (int xi = 0; xi < 2; ++xi) {
        vi_vision::Image tile; std::string e;
        if (!vi_vision::Crop(f.img, {xs[xi], ys[yi], tw, th}, tile, e)) { err = e; return false; }
        std::vector<vi_text::Line> tl;
        if (!vi_ocr::Recognize(tile, (std::max)(1.0, scale), GetOpt(a, "lang"), tl, lang, err)) return false;
        for (auto& line : tl) {
            line.box.x += xs[xi]; line.box.y += ys[yi];
            for (auto& w : line.words) { w.box.x += xs[xi]; w.box.y += ys[yi]; }
            int match = -1;
            for (size_t i = 0; i < lines.size(); ++i) if (LineOverlap(lines[i].box, line.box) > 0.5) { match = (int)i; break; }
            if (match < 0) lines.push_back(std::move(line));
            else if (line.text.size() > lines[(size_t)match].text.size()) lines[(size_t)match] = std::move(line);
        }
    }
    return true;
}
static std::string LineJson(const Frame& f, const vi_text::Line& l, bool words) {
    std::string j = "{\"text\":\"" + JU(l.text) + "\",\"box\":" + BoxJ(ToClient(f, l.box));
    if (words) {
        j += ",\"words\":[";
        for (size_t i = 0; i < l.words.size(); ++i) j += (i ? "," : "") + std::string("{\"text\":\"") + JU(l.words[i].text) + "\",\"box\":" + BoxJ(ToClient(f, l.words[i].box)) + "}";
        j += "]";
    }
    return j + "}";
}
static const char* kNoConfidence = "\"ocr_confidence\":null,\"ocr_confidence_note\":\"Windows.Media.Ocr reports no confidence; match_score is textual (1 = literal match, <1 = fuzzy)\"";

static Reply CmdOcr(DWORD pid, const Args& a) {
    bool autoScale; std::string err; auto scales = ParseScales(GetOpt(a, "scale", "1"), autoScale, err);
    if (scales.empty()) return Fail(err);
    Frame f; if (!Grab(pid, a, f, err)) return Fail(err);
    std::vector<vi_text::Line> lines; std::string lang;
    DWORD t0 = GetTickCount();
    if (!OcrOnce(f, scales[0], a, lines, lang, err)) return Fail(err);
    if (HasOpt(a, "tiles") && !OcrTiles(f, scales[0], a, lines, lang, err)) return Fail(err);
    const bool words = !HasOpt(a, "lines-only");
    std::string j = "{\"ok\":true,\"w\":" + std::to_string(f.img.w) + ",\"h\":" + std::to_string(f.img.h) + ",\"origin_x\":" + Num(f.ox) + ",\"origin_y\":" + Num(f.oy) +
        ",\"client_w\":" + std::to_string(f.clientW) + ",\"client_h\":" + std::to_string(f.clientH) + ",\"method\":\"" + f.method + "\",\"lang\":\"" + JEsc(lang) +
        "\",\"scale\":" + Num(scales[0]) + ",\"ocr_ms\":" + std::to_string(GetTickCount() - t0) + "," + kNoConfidence + ",\"line_count\":" + std::to_string(lines.size());
    std::wstring text; for (auto& l : lines) { if (!text.empty()) text += L"\n"; text += l.text; }
    j += ",\"text\":\"" + JU(text) + "\",\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i) j += (i ? "," : "") + LineJson(f, lines[i], words);
    j += "]}";
    if (HasOpt(a, "out")) {
        FILE* o = nullptr; _wfopen_s(&o, U2W(GetOpt(a, "out")).c_str(), L"wb");
        if (!o) return Fail("cannot write --out file");
        fwrite(j.data(), 1, j.size(), o); fclose(o);
        return {true, "{\"ok\":true,\"out\":\"" + JEsc(GetOpt(a, "out")) + "\",\"line_count\":" + std::to_string(lines.size()) + ",\"lang\":\"" + JEsc(lang) + "\"}"};
    }
    return {true, j};
}

#include "vi_cli_text.inl"   //@text
#include "vi_cli_image.inl"  //@image
#include "vi_cli_small.inl"  //@small

// ---- dispatch + script -------------------------------------------------------------------------------------------
static Reply RunScript(DWORD pid, const Args& a);   //@script
static Reply Dispatch(DWORD pid, const std::vector<std::string>& tk) {
    const std::string& cmd = tk[0];
    Args a = SplitArgs(std::vector<std::string>(tk.begin() + 1, tk.end()));
    if (cmd == "ocr") return CmdOcr(pid, a);
    if (cmd == "click-text") return CmdText(pid, a, 0); //@text
    if (cmd == "wait-text") return CmdText(pid, a, 1); //@text
    if (cmd == "assert-text") return CmdText(pid, a, 2); //@text
    if (cmd == "click-image") return CmdImage(pid, a, 0); //@image
    if (cmd == "wait-image") return CmdImage(pid, a, 1); //@image
    if (cmd == "find-image") return CmdImage(pid, a, 2); //@image
    if (cmd == "pixel") return CmdPixel(pid, a); //@small
    if (cmd == "scroll") return CmdScroll(pid, a); //@small
    if (cmd == "drag") return CmdDrag(pid, a); //@small
    if (cmd == "wait-change") return WaitChange(pid, a);
    if (cmd == "script") return RunScript(pid, a); //@script
    std::string j, err, resp;
    if (!BuildJson(tk, j, err)) return Fail(err);
    // An AppContainer target cannot write to the caller's folder: it writes the PNG into the granted
    // <dist>\run\logs folder and we move it to the requested path.
    std::wstring staged, wanted;
    if (cmd == "capture" && !a.pos.empty() && QueryTargetToken(pid).appContainer) {
        wchar_t full[MAX_PATH]; GetFullPathNameW(U2W(a.pos[0]).c_str(), MAX_PATH, full, nullptr); wanted = full;
        std::wstring stagingDir=AcLogsDir();
        if(stagingDir.empty()) return Fail("cannot grant AppContainer capture staging directory");
        staged = stagingDir + L"\\capture_" + std::to_wstring(pid) + L"_" + std::to_wstring(GetTickCount()) + L".png";
        std::string from = "\"path\":\"" + JEsc(W2U(wanted)) + "\"", to = "\"path\":\"" + JEsc(W2U(staged)) + "\"";
        size_t at = j.find(from); if (at != std::string::npos) { j.replace(at, from.size(), to); fprintf(stderr,"[vinput] staging AppContainer capture: %s\n", W2U(staged).c_str()); } else { return Fail("AppContainer capture path could not be staged"); }
    }
    bool ok = Request(pid, j, resp);
    if (!staged.empty() && ok && resp.find("\"ok\":true") != std::string::npos) {
        std::string from = "\"path\":\"" + JEsc(W2U(staged)) + "\"", to = "\"path\":\"" + JEsc(W2U(wanted)) + "\"";
        if (!MoveFileExW(staged.c_str(), wanted.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            DWORD e = GetLastError(); DeleteFileW(staged.c_str());
            return Fail("AppContainer capture: cannot move staged PNG to the requested path (" + std::to_string(e) + ")");
        }
        size_t at = resp.find(from); if (at != std::string::npos) resp.replace(at, from.size(), to);
    }
    if(!staged.empty()) { DeleteFileW(staged.c_str()); RemoveDirectoryW(staged.substr(0,staged.find_last_of(L'\\')).c_str()); }
    return {ok && resp.find("\"ok\":true") != std::string::npos, resp};
}

#include "vi_cli_script.inl"  //@script
