// vi_cli_text.inl - click-text / wait-text / assert-text (included by vi_cli_ui.inl).
static std::wstring JoinWords(const vi_text::Line& l, int first, int last) {
    std::wstring s; for (int i = first; i <= last; ++i) { if (i > first) s += L' '; s += l.words[(size_t)i].text; } return s;
}
static std::string AllText(const std::vector<vi_text::Line>& lines, size_t limit = 400) {
    std::wstring s; for (auto& l : lines) { if (!s.empty()) s += L" | "; s += l.text; }
    if (s.size() > limit) s.resize(limit);
    return JU(s);
}
static std::string HitJson(const Frame& f, const std::vector<vi_text::Line>& lines, const vi_text::Hit& h, int index) {
    const vi_text::Box b = ToClient(f, h.box);
    std::string j = "{\"index\":" + std::to_string(index) + ",\"text\":\"" + JU(JoinWords(lines[(size_t)h.line], h.first, h.last)) + "\",\"line\":\"" + JU(h.line_text) +
        "\",\"box\":" + BoxJ(b) + ",\"center\":[" + std::to_string(b.x + b.w / 2) + "," + std::to_string(b.y + b.h / 2) + "],\"match_score\":" + Num(h.score) + "}";
    return j;
}
struct TextQuery { std::wstring text; vi_text::Options opt; std::vector<double> scales; bool autoScale = false; };
static bool ParseTextQuery(const Args& a, const char* cmd, TextQuery& q, std::string& err) {
    if (a.pos.empty() || a.pos[0].empty()) { err = std::string(cmd) + " needs the text to look for"; return false; }
    q.text = U2W(a.pos[0]); q.opt.exact = HasOpt(a, "exact");
    q.opt.fuzzy = atoi(GetOpt(a, "fuzzy", "0").c_str());
    if (q.opt.fuzzy < 0 || q.opt.fuzzy > 3) { err = "--fuzzy must be 0..3"; return false; }
    q.scales = ParseScales(GetOpt(a, "scale", "auto"), q.autoScale, err);
    return !q.scales.empty();
}
// One grab, then OCR passes until something matches: for each scale, the full frame and then (unless --no-tiles) the tiled
// pass merged on top of it. hits/lines describe the pass that matched, or the first pass.
static bool SearchText(DWORD pid, const Args& a, const TextQuery& q, Frame& f, std::vector<vi_text::Line>& lines, std::vector<vi_text::Hit>& hits, std::string& err) {
    if (!Grab(pid, a, f, err)) return false;
    std::string lang; bool first = true;
    for (size_t i = 0; i < q.scales.size(); ++i) {
        std::vector<vi_text::Line> l;
        if (!OcrOnce(f, q.scales[i], a, l, lang, err)) return false;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) { if (HasOpt(a, "no-tiles")) break; if (!OcrTiles(f, q.scales[i], a, l, lang, err)) return false; }
            auto h = vi_text::Find(l, q.text, q.opt);
            if (first || !h.empty()) { lines = l; hits = std::move(h); first = false; }
            if (!hits.empty()) return true;
        }
    }
    return true;
}
static std::string CandidatesJson(const Frame& f, const std::vector<vi_text::Line>& lines, const std::vector<vi_text::Hit>& hits) {
    std::string j = "[";
    for (size_t i = 0; i < hits.size() && i < 20; ++i) j += (i ? "," : "") + HitJson(f, lines, hits[i], (int)i + 1);
    return j + "]";
}
static std::string ClickJson(const Args& a, int x, int y) {
    std::string j = "{"; PutS(j, "cmd", "click"); PutS(j, "button", GetOpt(a, "button", "left")); Put(j, "x", std::to_string(x)); Put(j, "y", std::to_string(y));
    static const char* pass[][2] = {{"count", "count"}, {"ms", "ms"}, {"hover-frames", "hover_frames"}, {"hover-timeout-ms", "hover_timeout_ms"}, {"settle-ms", "settle_ms"}};
    for (auto& p : pass) if (HasOpt(a, p[0])) Put(j, p[1], GetOpt(a, p[0]));
    return j + "}";
}
static bool ParseNth(const Args& a, int& nth, std::string& err) {
    nth = 0; if (!HasOpt(a, "nth")) return true;
    nth = atoi(GetOpt(a, "nth").c_str()); if (nth < 1) { err = "--nth is 1-based (reading order: rows top to bottom, left to right)"; return false; }
    return true;
}

// mode: 0 click-text, 1 wait-text, 2 assert-text
static Reply CmdText(DWORD pid, const Args& a, int mode) {
    const char* name = mode == 0 ? "click-text" : mode == 1 ? "wait-text" : "assert-text";
    TextQuery q; std::string err;
    if (!ParseTextQuery(a, name, q, err)) return Fail(err);
    int nth; if (!ParseNth(a, nth, err)) return Fail(err);
    const bool gone = HasOpt(a, "gone") || HasOpt(a, "absent");
    if (mode == 0 && gone) return Fail("--gone only applies to wait-text / assert-text");
    double timeout = atof(GetOpt(a, "timeout", mode == 0 ? "5000" : mode == 1 ? "15000" : "0").c_str());
    if (!(timeout >= 0 && timeout <= 600000)) return Fail("--timeout is milliseconds, 0..600000");
    const DWORD t0 = GetTickCount(); unsigned polls = 0;
    Frame f; std::vector<vi_text::Line> lines; std::vector<vi_text::Hit> hits;
    for (;;) {
        if (!SearchText(pid, a, q, f, lines, hits, err)) return Fail(err, "\"polls\":" + std::to_string(polls));
        ++polls;
        const DWORD elapsed = GetTickCount() - t0;
        std::string stats = "\"elapsed_ms\":" + std::to_string(elapsed) + ",\"polls\":" + std::to_string(polls);
        if (mode == 0) {
            if (!hits.empty()) {
                if (nth == 0 && hits.size() > 1)
                    return Fail("ambiguous: " + std::to_string(hits.size()) + " matches for the text; pass --nth N (1-based, reading order) or --exact; nothing was clicked",
                                "\"ambiguous\":true,\"match_count\":" + std::to_string(hits.size()) + ",\"candidates\":" + CandidatesJson(f, lines, hits) + "," + stats);
                if (nth > (int)hits.size())
                    return Fail("--nth " + std::to_string(nth) + " but only " + std::to_string(hits.size()) + " matches; nothing was clicked", "\"match_count\":" + std::to_string(hits.size()) + ",\"candidates\":" + CandidatesJson(f, lines, hits) + "," + stats);
                const int index = nth ? nth : 1; const vi_text::Hit& h = hits[(size_t)index - 1];
                const vi_text::Box b = ToClient(f, h.box); const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
                std::string match = HitJson(f, lines, h, index);
                if (HasOpt(a, "dry-run")) return {true, "{\"ok\":true,\"clicked\":false,\"dry_run\":true,\"match\":" + match + ",\"match_count\":" + std::to_string(hits.size()) + "," + kNoConfidence + "," + stats + "}"};
                std::string resp; bool ok = Request(pid, ClickJson(a, cx, cy), resp) && resp.find("\"ok\":true") != std::string::npos;
                return {ok, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"clicked\":" + (ok ? "true" : "false") + ",\"match\":" + match + ",\"match_count\":" + std::to_string(hits.size()) + "," + kNoConfidence + "," + stats + ",\"click\":" + resp + "}"};
            }
        } else if (gone ? hits.empty() : !hits.empty()) {
            std::string match = hits.empty() ? "null" : HitJson(f, lines, hits[(size_t)((nth ? nth : 1) <= (int)hits.size() ? (nth ? nth : 1) : 1) - 1], nth ? nth : 1);
            return {true, std::string("{\"ok\":true,\"found\":") + (hits.empty() ? "false" : "true") + ",\"gone\":" + (hits.empty() ? "true" : "false") + ",\"match\":" + match + ",\"match_count\":" + std::to_string(hits.size()) + "," + stats + "}"};
        }
        if (elapsed >= timeout) {
            if (mode == 0 && HasOpt(a, "optional")) return {true, "{\"ok\":true,\"clicked\":false,\"skipped\":true,\"reason\":\"text not present (--optional)\"," + stats + "}"};
            if (mode == 1 && !gone && HasOpt(a, "optional")) return {true, "{\"ok\":true,\"found\":false,\"skipped\":true,\"reason\":\"text not present (--optional)\"," + stats + "}"};
            const std::string what = mode == 0 ? "text not found" : gone ? "text still present" : "text not found";
            return Fail(std::string(name) + ": " + what + (timeout > 0 ? " before timeout" : ""),
                        std::string("\"timed_out\":") + (timeout > 0 ? "true" : "false") + ",\"match_count\":" + std::to_string(hits.size()) + ",\"ocr_saw\":\"" + AllText(lines) + "\"," + stats);
        }
        Sleep(250);
    }
}
