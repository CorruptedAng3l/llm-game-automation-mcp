// Text matching over OCR output for vinput UI navigation. Pure functions, no OCR or pipe dependency,
// so the matching rules are unit-tested separately (test\vi_uitest.cpp).
//
// Rules (documented in README "UI navigation"):
//  * default: case-insensitive; ALL whitespace is ignored on both sides ("Load  Game" == "LoadGame" == "load game");
//    the query may be a substring of a line ("PLAY" also matches inside "PLAYERS" - use --exact to forbid that).
//  * exact:   case-sensitive, and the match must start and end on OCR word boundaries.
//  * whole_word: require the same OCR word boundaries independently of case; exact still requires case.
//  * fuzzy N: also accept a run of whole words whose letters differ from the query by <= N edits (OCR slips such as
//    BATTLE -> BATTIE). Fuzzy hits have score < 1.
//  * hits are returned in reading order (rows top to bottom, left to right inside a row); --nth is 1-based on that order.
//  * a hit's box is the union of the OCR words it touches (a match starting mid-word boxes the whole word).
#pragma once
#define VI_TEXT_LINE_MATCH 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#pragma comment(lib, "user32.lib")

namespace vi_text {
struct Box { int x = 0, y = 0, w = 0, h = 0; };
struct Word { std::wstring text; Box box; };
struct Line { std::wstring text; Box box; std::vector<Word> words; };
struct Hit {
    int line = 0, first = 0, last = 0;   // word span [first, last] inside lines[line]
    Box box;
    double score = 1.0;                  // 1.0 = literal match, <1 = fuzzy (1 - edits / query length)
    std::wstring line_text;
    std::wstring query;                  // populated by multi-query CLI search; not OCR confidence
};
struct Options { bool exact = false; int fuzzy = 0; bool whole_word = false; bool exact_line = false; };

inline std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}
inline std::wstring StripSpace(const std::wstring& s) {
    std::wstring o; o.reserve(s.size());
    for (wchar_t c : s) if (!iswspace(c)) o += c;
    return o;
}
inline Box Union(const Box& a, const Box& b) {
    const int l = (std::min)(a.x, b.x), t = (std::min)(a.y, b.y);
    const int r = (std::max)(a.x + a.w, b.x + b.w), bo = (std::max)(a.y + a.h, b.y + b.h);
    return {l, t, r - l, bo - t};
}
inline int Edits(const std::wstring& a, const std::wstring& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = (std::min)({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Hits inside ONE line, in word order.
inline void FindInLine(const Line& line, int lineIndex, const std::wstring& query, const Options& opt, std::vector<Hit>& out) {
    const std::wstring q = opt.exact ? StripSpace(query) : Lower(StripSpace(query));
    if (q.empty() || line.words.empty()) return;
    std::wstring s; std::vector<int> owner; std::vector<char> wordStart, wordEnd;
    for (size_t w = 0; w < line.words.size(); ++w) {
        std::wstring t = StripSpace(line.words[w].text);
        if (!opt.exact) t = Lower(t);
        for (size_t i = 0; i < t.size(); ++i) {
            s += t[i]; owner.push_back(static_cast<int>(w));
            wordStart.push_back(i == 0); wordEnd.push_back(i + 1 == t.size());
        }
    }
    std::vector<Hit> hits;
    auto addHit = [&](int first, int last, double score) {
        Hit h; h.line = lineIndex; h.first = first; h.last = last; h.score = score; h.line_text = line.text;
        h.box = line.words[first].box;
        for (int w = first + 1; w <= last; ++w) h.box = Union(h.box, line.words[w].box);
        hits.push_back(h);
    };
    for (size_t pos = s.find(q); pos != std::wstring::npos; pos = s.find(q, pos + q.size())) {
        const size_t end = pos + q.size() - 1;
        if(opt.exact_line&&(pos!=0||end+1!=s.size()))continue;
        if ((opt.exact || opt.whole_word) && !(wordStart[pos] && wordEnd[end])) continue;
        addHit(owner[pos], owner[end], 1.0);
    }
    if (opt.fuzzy > 0) {
        const int n = static_cast<int>(line.words.size());
        for (int i = 0; i < n; ++i) {
            std::wstring joined;
            for (int j = i; j < n; ++j) {
                std::wstring t = StripSpace(line.words[j].text);
                joined += opt.exact ? t : Lower(t);
                if (joined.size() > q.size() + static_cast<size_t>(opt.fuzzy)) break;
                if (joined.size() + static_cast<size_t>(opt.fuzzy) < q.size()) continue;
                if(opt.exact_line&&(i!=0||j+1!=n))continue;
                const int d = Edits(joined, q);
                if (d == 0 || d > opt.fuzzy) continue;
                bool overlaps = false;
                for (const Hit& h : hits) if (!(j < h.first || i > h.last)) { overlaps = true; break; }
                if (!overlaps) addHit(i, j, 1.0 - static_cast<double>(d) / static_cast<double>(q.size()));
            }
        }
    }
    for (auto& h : hits) out.push_back(std::move(h));
}

// Reading order: group by row (centre within half a box height of the row's first box), then left to right.
inline void ReadingOrder(std::vector<Hit>& hits) {
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.box.y + a.box.h / 2 < b.box.y + b.box.h / 2; });
    size_t i = 0;
    while (i < hits.size()) {
        const int rowCentre = hits[i].box.y + hits[i].box.h / 2, tolerance = (std::max)(4, hits[i].box.h / 2);
        size_t j = i + 1;
        while (j < hits.size() && hits[j].box.y + hits[j].box.h / 2 - rowCentre <= tolerance) ++j;
        std::stable_sort(hits.begin() + i, hits.begin() + j, [](const Hit& a, const Hit& b) { return a.box.x < b.box.x; });
        i = j;
    }
}

inline std::vector<Hit> Find(const std::vector<Line>& lines, const std::wstring& query, const Options& opt) {
    std::vector<Hit> hits;
    for (size_t i = 0; i < lines.size(); ++i) FindInLine(lines[i], static_cast<int>(i), query, opt, hits);
    ReadingOrder(hits);
    return hits;
}
}
