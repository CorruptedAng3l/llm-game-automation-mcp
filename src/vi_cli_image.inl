// vi_cli_image.inl - find-image / click-image / wait-image (included by vi_cli_ui.inl).
// ---- image templates ---------------------------------------------------------------------------------------------
struct ImageQuery { vi_vision::Image pattern; vi_ncc::Options opt; };
static bool ParseImageQuery(const Args& a, const char* cmd, ImageQuery& q, std::string& err) {
    if (a.pos.empty()) { err = std::string(cmd) + " needs a template .png"; return false; }
    wchar_t full[MAX_PATH]; GetFullPathNameW(U2W(a.pos[0]).c_str(), MAX_PATH, full, nullptr);
    if (!vi_vision::LoadPng(full, q.pattern, err)) { err = a.pos[0] + ": " + err; return false; }
    if (HasOpt(a, "threshold")) { q.opt.threshold = atof(GetOpt(a, "threshold").c_str()); if (!(q.opt.threshold > 0 && q.opt.threshold <= 1)) { err = "--threshold must be in (0,1]"; return false; } }
    if (HasOpt(a, "no-scale")) q.opt.scales = {1.0};
    else if (HasOpt(a, "scales")) { bool dummy; q.opt.scales = ParseScales(GetOpt(a, "scales"), dummy, err); if (q.opt.scales.empty()) return false; }
    return true;
}
static std::string ImageHitJson(const Frame& f, const vi_ncc::Match& m, int index) {
    const vi_text::Box b = ToClient(f, {m.x, m.y, m.w, m.h});
    return "{\"index\":" + std::to_string(index) + ",\"box\":" + BoxJ(b) + ",\"center\":[" + std::to_string(b.x + b.w / 2) + "," + std::to_string(b.y + b.h / 2) +
        "],\"score\":" + Num(m.score) + ",\"scale\":" + Num(m.scale) + "}";
}
// mode: 0 click-image, 1 wait-image, 2 find-image
static Reply CmdImage(DWORD pid, const Args& a, int mode) {
    const char* name = mode == 0 ? "click-image" : mode == 1 ? "wait-image" : "find-image";
    ImageQuery q; std::string err;
    if (!ParseImageQuery(a, name, q, err)) return Fail(err);
    int nth; if (!ParseNth(a, nth, err)) return Fail(err);
    const bool gone = HasOpt(a, "gone");
    if (mode != 1 && gone) return Fail("--gone only applies to wait-image");
    double timeout = atof(GetOpt(a, "timeout", mode == 0 ? "5000" : mode == 1 ? "15000" : "0").c_str());
    if (!(timeout >= 0 && timeout <= 600000)) return Fail("--timeout is milliseconds, 0..600000");
    const DWORD t0 = GetTickCount(); unsigned polls = 0;
    for (;;) {
        Frame f; if (!Grab(pid, a, f, err)) return Fail(err, "\"polls\":" + std::to_string(polls));
        vi_ncc::Result r = vi_ncc::Find(f.img, q.pattern, q.opt);
        if (!r.error.empty()) return Fail(r.error);
        ++polls;
        // reading order for --nth
        std::vector<vi_text::Hit> order;
        for (size_t i = 0; i < r.matches.size(); ++i) { vi_text::Hit h; h.first = (int)i; h.box = {r.matches[i].x, r.matches[i].y, r.matches[i].w, r.matches[i].h}; order.push_back(h); }
        vi_text::ReadingOrder(order);
        std::vector<vi_ncc::Match> m; for (auto& h : order) m.push_back(r.matches[(size_t)h.first]);
        const DWORD elapsed = GetTickCount() - t0;
        std::string stats = "\"elapsed_ms\":" + std::to_string(elapsed) + ",\"polls\":" + std::to_string(polls);
        std::string list = "[";
        for (size_t i = 0; i < m.size() && i < 20; ++i) list += (i ? "," : "") + ImageHitJson(f, m[i], (int)i + 1);
        list += "]";
        if (mode == 2) return {!m.empty(), std::string("{\"ok\":") + (m.empty() ? "false" : "true") + ",\"match_count\":" + std::to_string(m.size()) + ",\"matches\":" + list + ",\"threshold\":" + Num(q.opt.threshold) + "," + stats +
            (m.empty() ? ",\"error\":\"template not found at the threshold\"" : "") + "}"};
        if (mode == 0) {
            if (!m.empty()) {
                if (nth == 0 && m.size() > 1) return Fail("ambiguous: " + std::to_string(m.size()) + " matches for the template; pass --nth N or raise --threshold; nothing was clicked",
                    "\"ambiguous\":true,\"match_count\":" + std::to_string(m.size()) + ",\"candidates\":" + list + "," + stats);
                if (nth > (int)m.size()) return Fail("--nth " + std::to_string(nth) + " but only " + std::to_string(m.size()) + " matches; nothing was clicked", "\"match_count\":" + std::to_string(m.size()) + ",\"candidates\":" + list + "," + stats);
                const int index = nth ? nth : 1; const vi_text::Box b = ToClient(f, {m[(size_t)index - 1].x, m[(size_t)index - 1].y, m[(size_t)index - 1].w, m[(size_t)index - 1].h});
                std::string match = ImageHitJson(f, m[(size_t)index - 1], index);
                if (HasOpt(a, "dry-run")) return {true, "{\"ok\":true,\"clicked\":false,\"dry_run\":true,\"match\":" + match + ",\"match_count\":" + std::to_string(m.size()) + "," + stats + "}"};
                std::string resp; bool ok = Request(pid, ClickJson(a, b.x + b.w / 2, b.y + b.h / 2), resp) && resp.find("\"ok\":true") != std::string::npos;
                return {ok, std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"clicked\":" + (ok ? "true" : "false") + ",\"match\":" + match + ",\"match_count\":" + std::to_string(m.size()) + "," + stats + ",\"click\":" + resp + "}"};
            }
        } else if (gone ? m.empty() : !m.empty()) {
            return {true, std::string("{\"ok\":true,\"found\":") + (m.empty() ? "false" : "true") + ",\"gone\":" + (m.empty() ? "true" : "false") + ",\"match_count\":" + std::to_string(m.size()) + ",\"matches\":" + list + "," + stats + "}"};
        }
        if (elapsed >= timeout && mode == 0 && HasOpt(a, "optional")) return {true, "{\"ok\":true,\"clicked\":false,\"skipped\":true,\"reason\":\"template not present (--optional)\"," + stats + "}"};
        if (elapsed >= timeout)
            return Fail(std::string(name) + ": " + (mode == 1 && gone ? "template still present" : "template not found") + (timeout > 0 ? " before timeout" : ""),
                        std::string("\"timed_out\":") + (timeout > 0 ? "true" : "false") + ",\"match_count\":" + std::to_string(m.size()) + "," + stats);
        Sleep(250);
    }
}
