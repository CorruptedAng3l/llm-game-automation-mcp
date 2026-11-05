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
        "\",\"box\":" + BoxJ(b) + ",\"center\":[" + std::to_string(b.x + b.w / 2) + "," + std::to_string(b.y + b.h / 2) + "],\"match_score\":" + Num(h.score) +
        (h.query.empty() ? "" : ",\"query\":\"" + JU(h.query) + "\"") + "}";
    return j;
}
struct TextQuery { std::vector<std::wstring> texts; vi_text::Options opt; std::vector<double> scales; bool autoScale = false; std::string preprocess; };
static bool ParseTextQuery(const Args& a, const char* cmd, TextQuery& q, std::string& err) {
    std::string input;
    if (HasOpt(a, "any")) {
        if (std::string(cmd) != "wait-text") { err = "--any is supported by wait-text"; return false; }
        if (!a.pos.empty()) { err = "pass a positional query or --any a|b, not both"; return false; }
        input = GetOpt(a, "any");
        if (input == "true") { err = "--any needs quoted alternatives, for example --any \"OK|RESUME GAME\""; return false; }
    } else {
        if (a.pos.empty()) { err = std::string(cmd) + " needs the text to look for"; return false; }
        input = a.pos[0];
    }
    size_t begin = 0;
    do {
        size_t end = HasOpt(a, "any") ? input.find('|', begin) : std::string::npos;
        std::wstring text = U2W(input.substr(begin, end == std::string::npos ? end : end - begin));
        if (vi_text::StripSpace(text).empty() || text.size() > 256) { err = "text queries must contain 1..256 characters"; return false; }
        if (q.texts.size() >= 16) { err = "--any supports at most 16 alternatives"; return false; }
        if (std::find(q.texts.begin(), q.texts.end(), text) == q.texts.end()) q.texts.push_back(std::move(text));
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (begin <= input.size());
    q.opt.exact = HasOpt(a, "exact");
    const std::string wholeWord = GetOpt(a, "whole-word", "false");
    if (wholeWord != "true" && wholeWord != "false") { err = "--whole-word accepts true or false"; return false; }
    q.opt.whole_word = wholeWord == "true";
    q.opt.fuzzy = atoi(GetOpt(a, "fuzzy", "0").c_str());
    if (q.opt.fuzzy < 0 || q.opt.fuzzy > 3) { err = "--fuzzy must be 0..3"; return false; }
    q.preprocess = GetOpt(a, "preprocess", "auto");
    if (q.preprocess != "auto" && q.preprocess != "none" && q.preprocess != "gray" && q.preprocess != "contrast" && q.preprocess != "threshold" && q.preprocess != "invert") {
        err = "--preprocess must be none, gray, contrast, threshold, invert or auto"; return false;
    }
    q.scales = ParseScales(GetOpt(a, "scale", "auto"), q.autoScale, err);
    return !q.scales.empty();
}
static std::vector<vi_text::Hit> FindQueries(const std::vector<vi_text::Line>& lines, const TextQuery& q) {
    std::vector<vi_text::Hit> hits;
    for (const auto& text : q.texts) {
        auto found = vi_text::Find(lines, text, q.opt);
        for (auto& h : found) {
            bool duplicate = false;
            for (const auto& previous : hits) if (h.line == previous.line && h.first == previous.first && h.last == previous.last) { duplicate = true; break; }
            if (!duplicate) { h.query = text; hits.push_back(std::move(h)); }
        }
    }
    vi_text::ReadingOrder(hits); return hits;
}
static void RememberOcr(const std::vector<vi_text::Line>& found, std::vector<vi_text::Line>& remembered) {
    for (const auto& line : found) {
        bool duplicate = false;
        for (const auto& previous : remembered) if (previous.text == line.text && LineOverlap(previous.box, line.box) > 0.6) { duplicate = true; break; }
        if (!duplicate && remembered.size() < 128) remembered.push_back(line);
    }
}
struct TextScan { std::string preprocessing = "none"; double scale = 1; unsigned passes = 0; };
// Ordinary OCR first, then bounded fallback passes. A successful pass is used on its own: alternate readings cannot
// manufacture duplicate clickable hits. Miss diagnostics retain distinct actual OCR readings across the passes.
static bool SearchText(DWORD pid, const Args& a, const TextQuery& q, Frame& f, std::vector<vi_text::Line>& lines, std::vector<vi_text::Hit>& hits, TextScan& scan, std::string& err) {
    if (!Grab(pid, a, f, err, true)) return false;
    std::string lang; bool analyzed = false; lines.clear(); hits.clear(); scan = TextScan{};
    struct Pass { double scale; std::string mode; bool tiles; };
    std::vector<Pass> plan;
    const bool automatic = q.preprocess == "auto";
    for (double scale : q.scales) plan.push_back({scale, automatic ? "none" : q.preprocess, !HasOpt(a, "no-tiles")});
    if (automatic) {
        const double scale = q.autoScale ? 2 : q.scales[0];
        plan.push_back({scale, "gray", false}); plan.push_back({scale, "contrast", false});
        plan.push_back({scale, "threshold", false}); plan.push_back({q.autoScale ? 3 : scale, "invert", false});
        if (q.autoScale) plan.push_back({3, "none", false});
    }
    for (const auto& pass : plan) {
        std::vector<vi_text::Line> l; ++scan.passes; scan.preprocessing = pass.mode; scan.scale = pass.scale;
        bool full = OcrOnce(f, pass.scale, a, l, lang, err, pass.mode);
        if (!full && err.find("exceeds OcrEngine.MaxImageDimension") == std::string::npos) return false;
        if (!full) err.clear();
        if (full) {
            analyzed = true;
            auto h = FindQueries(l, q); RememberOcr(l, lines);
            if (!h.empty()) { lines = std::move(l); hits = std::move(h); return true; }
        }
        if (pass.tiles || (!full && !HasOpt(a, "no-tiles"))) {
            ++scan.passes;
            if (!OcrTiles(f, pass.scale, a, l, lang, err, pass.mode)) return false;
            analyzed = true;
            auto h = FindQueries(l, q); RememberOcr(l, lines);
            if (!h.empty()) { lines = std::move(l); hits = std::move(h); return true; }
        }
    }
    if (!analyzed) { err = "no OCR pass fits OcrEngine.MaxImageDimension; use --region or remove --no-tiles"; return false; }
    return true;
}

// Ranked suggestions are observations, never accepted matches. Score is edit similarity, not OCR confidence.
static std::string NearestJson(const Frame& f, const std::vector<vi_text::Line>& lines, const TextQuery& q) {
    struct Near { vi_text::Hit hit; int edits; };
    std::vector<Near> nearest; unsigned compared = 0;
    for (size_t li = 0; li < lines.size() && compared < 512; ++li) {
        const auto& line = lines[li];
        for (size_t first = 0; first < line.words.size() && first < 64 && compared < 512; ++first) {
            std::wstring joined;
            for (size_t last = first; last < line.words.size() && last < first + 8 && compared < 512; ++last) {
                joined += vi_text::Lower(vi_text::StripSpace(line.words[last].text));
                if (joined.size() > 256) break;
                for (const auto& query : q.texts) {
                    const auto wanted = vi_text::Lower(vi_text::StripSpace(query));
                    const size_t difference = joined.size() > wanted.size() ? joined.size() - wanted.size() : wanted.size() - joined.size();
                    if (difference > (std::max)(size_t(8), wanted.size() / 2)) continue;
                    if (++compared > 512) break;
                    const int distance = vi_text::Edits(joined, wanted);
                    vi_text::Hit h; h.line = (int)li; h.first = (int)first; h.last = (int)last; h.query = query; h.line_text = line.text;
                    h.box = line.words[first].box; for (size_t wi = first + 1; wi <= last; ++wi) h.box = vi_text::Union(h.box, line.words[wi].box);
                    h.score = 1.0 - (double)distance / (std::max)(joined.size(), wanted.size());
                    if (h.score <= 0) continue;
                    bool duplicate = false;
                    for (auto& prior : nearest) if (vi_text::Lower(JoinWords(lines[(size_t)prior.hit.line], prior.hit.first, prior.hit.last)) == vi_text::Lower(JoinWords(line, (int)first, (int)last)) && LineOverlap(prior.hit.box, h.box) > 0.5) { duplicate = true; break; }
                    if (!duplicate) nearest.push_back({std::move(h), distance});
                }
            }
        }
    }
    std::stable_sort(nearest.begin(), nearest.end(), [](const Near& a, const Near& b) { return a.hit.score > b.hit.score; });
    std::string json = "[";
    for (size_t i = 0; i < nearest.size() && i < 5; ++i) {
        std::string h = HitJson(f, lines, nearest[i].hit, (int)i + 1); h.pop_back();
        json += (i ? "," : "") + h + ",\"edit_distance\":" + std::to_string(nearest[i].edits) + "}";
    }
    return json + "]";
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
    Frame f; std::vector<vi_text::Line> lines; std::vector<vi_text::Hit> hits; TextScan scan;
    for (;;) {
        if (!SearchText(pid, a, q, f, lines, hits, scan, err)) return Fail(err, "\"polls\":" + std::to_string(polls));
        ++polls;
        const DWORD elapsed = GetTickCount() - t0;
        std::string stats = "\"elapsed_ms\":" + std::to_string(elapsed) + ",\"polls\":" + std::to_string(polls) +
            ",\"ocr_pass\":\"" + scan.preprocessing + "\",\"ocr_scale\":" + Num(scan.scale) + ",\"ocr_passes\":" + std::to_string(scan.passes) + ",\"capture_space_updated\":false,\"text_word_boundary_version\":1,\"whole_word\":" + (q.opt.whole_word || q.opt.exact ? "true" : "false") + ",\"case_sensitive\":" + (q.opt.exact ? "true" : "false");
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
            const std::string diagnostics = "\"ocr_saw\":\"" + AllText(lines) + "\",\"nearest_candidates\":" + NearestJson(f, lines, q) +
                ",\"candidate_score_note\":\"text edit similarity only; suggestions were not accepted or clicked\"";
            if (mode == 0 && HasOpt(a, "optional")) return {true, "{\"ok\":true,\"clicked\":false,\"skipped\":true,\"reason\":\"text not present (--optional)\"," + diagnostics + "," + stats + "}"};
            if (mode == 1 && !gone && HasOpt(a, "optional")) return {true, "{\"ok\":true,\"found\":false,\"skipped\":true,\"reason\":\"text not present (--optional)\"," + diagnostics + "," + stats + "}"};
            const std::string what = mode == 0 ? "text not found" : gone ? "text still present" : "text not found";
            return Fail(std::string(name) + ": " + what + (timeout > 0 ? " before timeout" : ""),
                        std::string("\"timed_out\":") + (timeout > 0 ? "true" : "false") + ",\"match_count\":" + std::to_string(hits.size()) + "," + diagnostics + "," + stats);
        }
        Sleep(250);
    }
}
