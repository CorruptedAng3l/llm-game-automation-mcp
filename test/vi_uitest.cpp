// vi_uitest.cpp - unit tests for the pure UI-navigation text matching (vi_text.h). Positive AND negative controls. Exit code = number of failures.
#include "vi_text.h"
#include <cstdio>

static int failures = 0;
static void Check(bool value, const char* name) { std::printf("%s %s\n", value ? "PASS" : "FAIL", name); if (!value) ++failures; }

using vi_text::Box; using vi_text::Line; using vi_text::Word;
static Line MakeLine(std::initializer_list<const wchar_t*> words, int x, int y, int h = 20) {
    Line l; int cx = x;
    for (const wchar_t* w : words) {
        Word word; word.text = w; word.box = {cx, y, 10 * (int)wcslen(w), h}; cx += 10 * (int)wcslen(w) + 8;
        if (l.words.empty()) l.box = word.box; else l.box = vi_text::Union(l.box, word.box);
        if (!l.text.empty()) l.text += L' ';
        l.text += w; l.words.push_back(word);
    }
    return l;
}

static void TextTests() {
    std::vector<Line> lines = {
        MakeLine({L"MAIN", L"MENU"}, 300, 20), MakeLine({L"PLAY"}, 60, 110), MakeLine({L"Load", L"Game"}, 300, 110),
        MakeLine({L"BATTLE"}, 540, 340), MakeLine({L"BATTLE"}, 60, 340), MakeLine({L"PLAYERS", L"ONLINE"}, 60, 200)};
    vi_text::Options o;
    auto hits = vi_text::Find(lines, L"main menu", o);
    Check(hits.size() == 1 && hits[0].box.x == 300 && hits[0].box.w == 4 * 10 + 8 + 4 * 10, "case-insensitive multi-word match, box = union of words");
    hits = vi_text::Find(lines, L"LoadGame", o);
    Check(hits.size() == 1, "fuzzy whitespace: query without space matches 'Load Game'");
    hits = vi_text::Find(lines, L"  load    game ", o);
    Check(hits.size() == 1, "fuzzy whitespace: extra spaces in the query");
    hits = vi_text::Find(lines, L"BATTLE", o);
    Check(hits.size() == 2 && hits[0].box.x == 60 && hits[1].box.x == 540, "two BATTLE hits are in reading order (left before right)");
    hits = vi_text::Find(lines, L"play", o);
    Check(hits.size() == 2 && hits[0].box.y == 110 && hits[1].box.y == 200, "substring 'play' also hits PLAYERS (documented), top-to-bottom order");
    vi_text::Options whole; whole.whole_word = true;
    hits = vi_text::Find(lines, L"play", whole);
    Check(hits.size() == 1 && hits[0].line_text == L"PLAY", "whole-word mode is case-insensitive and excludes PLAYERS");
    const std::vector<Line> player = {MakeLine({L"Player"}, 20, 20)};
    Check(vi_text::Find(player, L"PLAY", whole).empty(), "whole-word PLAY cannot select Player");
    whole.whole_word = false;
    Check(vi_text::Find(player, L"PLAY", whole).size() == 1, "explicit whole-word false preserves substring opt-out");
    o.exact = true;
    hits = vi_text::Find(lines, L"PLAY", o);
    Check(hits.size() == 1 && hits[0].box.y == 110, "--exact: PLAY does not match inside PLAYERS");
    hits = vi_text::Find(lines, L"play", o);
    Check(hits.empty(), "--exact is case-sensitive (negative control)");
    o.exact = false;
    hits = vi_text::Find(lines, L"NONEXISTENT", o);
    Check(hits.empty(), "absent text gives no hit");
    o.fuzzy = 1;
    std::vector<Line> noisy = {MakeLine({L"BATTIE"}, 10, 10)};
    hits = vi_text::Find(noisy, L"BATTLE", o);
    Check(hits.size() == 1 && hits[0].score < 1.0 && hits[0].score > 0.8, "--fuzzy 1 accepts one OCR slip (BATTIE) with score < 1");
    o.fuzzy = 0;
    hits = vi_text::Find(noisy, L"BATTLE", o);
    Check(hits.empty(), "without --fuzzy the OCR slip is not matched (negative control)");
    o.fuzzy = 1;
    std::vector<Line> distant = {MakeLine({L"XXXXXX"}, 10, 10)};
    Check(vi_text::Find(distant, L"BATTLE", o).empty(), "--fuzzy 1 rejects a 6-edit difference");
    // same row but y jittered by a couple of pixels: still left-to-right
    std::vector<Line> row = {MakeLine({L"OK"}, 500, 102), MakeLine({L"OK"}, 100, 99)};
    hits = vi_text::Find(row, L"OK", vi_text::Options());
    Check(hits.size() == 2 && hits[0].box.x == 100, "row grouping tolerates a few pixels of vertical jitter");
    Check(vi_text::Edits(L"kitten", L"sitting") == 3, "edit distance sanity");
}

int main() {
    TextTests();
    std::printf(failures ? "FAILED %d\n" : "ALL PASS\n", failures);
    return failures;
}
