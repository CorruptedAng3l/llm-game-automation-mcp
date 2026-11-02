// vi_keys.h - strict, shared VK names and chord parsing; no Windows header dependency.
// SDK aliases below mirror local Windows SDK 10.0.28000.0 um/WinUser.h (2026-11-02).
// Generic vk1..vk255 and 0x01..0xFF cover reserved/unassigned values as well.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace vi_keys {
struct KeyName { std::string name; int vk; };

inline const std::vector<KeyName>& Names() {
    static const std::vector<KeyName> names = [] {
        std::vector<KeyName> result;
        auto add = [&](const std::string& name, int vk) {
            for (const auto& entry : result) if (entry.name == name) return;
            result.push_back({name, vk});
        };
        for (int ch = 'a'; ch <= 'z'; ++ch) add(std::string(1, static_cast<char>(ch)), ch - 'a' + 'A');
        for (int ch = '0'; ch <= '9'; ++ch) add(std::string(1, static_cast<char>(ch)), ch);
        struct Alias { const char* name; int vk; };
        static const Alias aliases[] = {
            {"lbutton", 0x01},
            {"rbutton", 0x02},
            {"cancel", 0x03},
            {"mbutton", 0x04},
            {"xbutton1", 0x05},
            {"xbutton2", 0x06},
            {"back", 0x08},
            {"tab", 0x09},
            {"clear", 0x0C},
            {"return", 0x0D},
            {"shift", 0x10},
            {"control", 0x11},
            {"menu", 0x12},
            {"pause", 0x13},
            {"capital", 0x14},
            {"kana", 0x15},
            {"hangeul", 0x15},
            {"hangul", 0x15},
            {"ime_on", 0x16},
            {"junja", 0x17},
            {"final", 0x18},
            {"hanja", 0x19},
            {"kanji", 0x19},
            {"ime_off", 0x1A},
            {"escape", 0x1B},
            {"convert", 0x1C},
            {"nonconvert", 0x1D},
            {"accept", 0x1E},
            {"modechange", 0x1F},
            {"space", 0x20},
            {"prior", 0x21},
            {"next", 0x22},
            {"end", 0x23},
            {"home", 0x24},
            {"left", 0x25},
            {"up", 0x26},
            {"right", 0x27},
            {"down", 0x28},
            {"select", 0x29},
            {"print", 0x2A},
            {"execute", 0x2B},
            {"snapshot", 0x2C},
            {"insert", 0x2D},
            {"delete", 0x2E},
            {"help", 0x2F},
            {"lwin", 0x5B},
            {"rwin", 0x5C},
            {"apps", 0x5D},
            {"sleep", 0x5F},
            {"numpad0", 0x60},
            {"numpad1", 0x61},
            {"numpad2", 0x62},
            {"numpad3", 0x63},
            {"numpad4", 0x64},
            {"numpad5", 0x65},
            {"numpad6", 0x66},
            {"numpad7", 0x67},
            {"numpad8", 0x68},
            {"numpad9", 0x69},
            {"multiply", 0x6A},
            {"add", 0x6B},
            {"separator", 0x6C},
            {"subtract", 0x6D},
            {"decimal", 0x6E},
            {"divide", 0x6F},
            {"f1", 0x70},
            {"f2", 0x71},
            {"f3", 0x72},
            {"f4", 0x73},
            {"f5", 0x74},
            {"f6", 0x75},
            {"f7", 0x76},
            {"f8", 0x77},
            {"f9", 0x78},
            {"f10", 0x79},
            {"f11", 0x7A},
            {"f12", 0x7B},
            {"f13", 0x7C},
            {"f14", 0x7D},
            {"f15", 0x7E},
            {"f16", 0x7F},
            {"f17", 0x80},
            {"f18", 0x81},
            {"f19", 0x82},
            {"f20", 0x83},
            {"f21", 0x84},
            {"f22", 0x85},
            {"f23", 0x86},
            {"f24", 0x87},
            {"navigation_view", 0x88},
            {"navigation_menu", 0x89},
            {"navigation_up", 0x8A},
            {"navigation_down", 0x8B},
            {"navigation_left", 0x8C},
            {"navigation_right", 0x8D},
            {"navigation_accept", 0x8E},
            {"navigation_cancel", 0x8F},
            {"numlock", 0x90},
            {"scroll", 0x91},
            {"oem_nec_equal", 0x92},
            {"oem_fj_jisho", 0x92},
            {"oem_fj_masshou", 0x93},
            {"oem_fj_touroku", 0x94},
            {"oem_fj_loya", 0x95},
            {"oem_fj_roya", 0x96},
            {"lshift", 0xA0},
            {"rshift", 0xA1},
            {"lcontrol", 0xA2},
            {"rcontrol", 0xA3},
            {"lmenu", 0xA4},
            {"rmenu", 0xA5},
            {"browser_back", 0xA6},
            {"browser_forward", 0xA7},
            {"browser_refresh", 0xA8},
            {"browser_stop", 0xA9},
            {"browser_search", 0xAA},
            {"browser_favorites", 0xAB},
            {"browser_home", 0xAC},
            {"volume_mute", 0xAD},
            {"volume_down", 0xAE},
            {"volume_up", 0xAF},
            {"media_next_track", 0xB0},
            {"media_prev_track", 0xB1},
            {"media_stop", 0xB2},
            {"media_play_pause", 0xB3},
            {"launch_mail", 0xB4},
            {"launch_media_select", 0xB5},
            {"launch_app1", 0xB6},
            {"launch_app2", 0xB7},
            {"oem_1", 0xBA},
            {"oem_plus", 0xBB},
            {"oem_comma", 0xBC},
            {"oem_minus", 0xBD},
            {"oem_period", 0xBE},
            {"oem_2", 0xBF},
            {"oem_3", 0xC0},
            {"gamepad_a", 0xC3},
            {"gamepad_b", 0xC4},
            {"gamepad_x", 0xC5},
            {"gamepad_y", 0xC6},
            {"gamepad_right_shoulder", 0xC7},
            {"gamepad_left_shoulder", 0xC8},
            {"gamepad_left_trigger", 0xC9},
            {"gamepad_right_trigger", 0xCA},
            {"gamepad_dpad_up", 0xCB},
            {"gamepad_dpad_down", 0xCC},
            {"gamepad_dpad_left", 0xCD},
            {"gamepad_dpad_right", 0xCE},
            {"gamepad_menu", 0xCF},
            {"gamepad_view", 0xD0},
            {"gamepad_left_thumbstick_button", 0xD1},
            {"gamepad_right_thumbstick_button", 0xD2},
            {"gamepad_left_thumbstick_up", 0xD3},
            {"gamepad_left_thumbstick_down", 0xD4},
            {"gamepad_left_thumbstick_right", 0xD5},
            {"gamepad_left_thumbstick_left", 0xD6},
            {"gamepad_right_thumbstick_up", 0xD7},
            {"gamepad_right_thumbstick_down", 0xD8},
            {"gamepad_right_thumbstick_right", 0xD9},
            {"gamepad_right_thumbstick_left", 0xDA},
            {"oem_4", 0xDB},
            {"oem_5", 0xDC},
            {"oem_6", 0xDD},
            {"oem_7", 0xDE},
            {"oem_8", 0xDF},
            {"oem_ax", 0xE1},
            {"oem_102", 0xE2},
            {"ico_help", 0xE3},
            {"ico_00", 0xE4},
            {"processkey", 0xE5},
            {"ico_clear", 0xE6},
            {"packet", 0xE7},
            {"oem_reset", 0xE9},
            {"oem_jump", 0xEA},
            {"oem_pa1", 0xEB},
            {"oem_pa2", 0xEC},
            {"oem_pa3", 0xED},
            {"oem_wsctrl", 0xEE},
            {"oem_cusel", 0xEF},
            {"oem_attn", 0xF0},
            {"oem_finish", 0xF1},
            {"oem_copy", 0xF2},
            {"oem_auto", 0xF3},
            {"oem_enlw", 0xF4},
            {"oem_backtab", 0xF5},
            {"attn", 0xF6},
            {"crsel", 0xF7},
            {"exsel", 0xF8},
            {"ereof", 0xF9},
            {"play", 0xFA},
            {"zoom", 0xFB},
            {"noname", 0xFC},
            {"pa1", 0xFD},
            {"oem_clear", 0xFE},
            {"enter", 0x0D}, {"esc", 0x1B}, {"backspace", 0x08},
            {"ctrl", 0x11}, {"lctrl", 0xA2}, {"rctrl", 0xA3},
            {"alt", 0x12}, {"lalt", 0xA4}, {"ralt", 0xA5},
            {"ins", 0x2D}, {"del", 0x2E}, {"pageup", 0x21}, {"pgup", 0x21},
            {"pagedown", 0x22}, {"pgdn", 0x22}, {"capslock", 0x14},
            {"scrolllock", 0x91}, {"printscreen", 0x2C}, {"prtsc", 0x2C},
            {"win", 0x5B}, {"contextmenu", 0x5D},
            {"num+", 0x6B}, {"numplus", 0x6B}, {"numpad+", 0x6B}, {"numpadadd", 0x6B},
            {"num-", 0x6D}, {"numminus", 0x6D}, {"numpad-", 0x6D}, {"numpadsubtract", 0x6D},
            {"num*", 0x6A}, {"nummultiply", 0x6A}, {"numpad*", 0x6A}, {"numpadmultiply", 0x6A},
            {"num/", 0x6F}, {"numdivide", 0x6F}, {"numpad/", 0x6F}, {"numpaddivide", 0x6F},
            {"num.", 0x6E}, {"numdecimal", 0x6E}, {"numpad.", 0x6E}, {"numpaddecimal", 0x6E},
            {"numseparator", 0x6C}, {"numpadseparator", 0x6C},
            {"plus", 0xBB}, {"equals", 0xBB}, {"+", 0xBB}, {"=", 0xBB},
            {"minus", 0xBD}, {"-", 0xBD}, {"comma", 0xBC}, {",", 0xBC},
            {"period", 0xBE}, {".", 0xBE}, {"slash", 0xBF}, {"/", 0xBF},
            {"semicolon", 0xBA}, {";", 0xBA}, {"quote", 0xDE}, {"apostrophe", 0xDE},
            {"grave", 0xC0}, {"tilde", 0xC0}, {"backquote", 0xC0},
            {"lbracket", 0xDB}, {"[", 0xDB}, {"rbracket", 0xDD}, {"]", 0xDD},
            {"backslash", 0xDC},
        };
        for (const auto& entry : aliases) {
            add(entry.name, entry.vk);
            // Both SDK spellings (oem_102) and compact spellings (oem102) are accepted.
            std::string compact(entry.name);
            compact.erase(std::remove(compact.begin(), compact.end(), '_'), compact.end());
            add(compact, entry.vk);
        }
        for (int vk = 1; vk <= 255; ++vk) add("vk" + std::to_string(vk), vk);
        return result;
    }();
    return names;
}

inline std::string Normalize(std::string name) {
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
    for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (name.compare(0, 3, "vk_") == 0) name.erase(0, 3);
    return name;
}

inline bool ParseNumber(const std::string& text, size_t start, unsigned base, int& vk) {
    if (start >= text.size()) return false;
    unsigned value = 0;
    for (size_t i = start; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        unsigned digit = 256;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        if (digit >= base || value > (255 - digit) / base) return false;
        value = value * base + digit;
    }
    if (!value || value > 255) return false;
    vk = static_cast<int>(value);
    return true;
}

inline bool ParseKey(const std::string& text, int& vk) {
    vk = 0;
    const std::string name = Normalize(text);
    if (name.compare(0, 2, "0x") == 0) return ParseNumber(name, 2, 16, vk);
    if (name.size() > 2 && name.compare(0, 2, "vk") == 0 && name[2] >= '0' && name[2] <= '9')
        return ParseNumber(name, 2, 10, vk);
    for (const auto& entry : Names()) if (entry.name == name) { vk = entry.vk; return true; }
    return false;
}

inline int VkFromName(const std::string& text) {
    int vk = 0;
    return ParseKey(text, vk) ? vk : 0;
}

inline std::string Name(int vk) {
    for (const auto& entry : Names()) if (entry.vk == vk) return entry.name;
    return {};
}

namespace detail {
inline bool ParseChordAt(const std::string& text, size_t start, unsigned depth, std::vector<int>& keys) {
    if (start >= text.size() || depth > 255) return false;
    // Longest matching token first makes the '+' in num+ a key-name character.
    // Its chord delimiter is a second '+': ctrl+num++w. Literal '+' works as ctrl++.
    for (size_t end = text.size(); end > start; --end) {
        if (end != text.size() && text[end] != '+') continue;
        int vk = 0;
        if (!ParseKey(text.substr(start, end - start), vk)) continue;
        keys.push_back(vk);
        if (end == text.size() || ParseChordAt(text, end + 1, depth + 1, keys)) return true;
        keys.pop_back();
    }
    return false;
}
}  // namespace detail

inline bool ParseChord(const std::string& text, std::vector<int>& keys, std::string* error = nullptr) {
    keys.clear();
    if (error) error->clear();
    if (text.size() > 4096 || !detail::ParseChordAt(text, 0, 0, keys)) {
        keys.clear();
        if (error) *error = "unknown or malformed key chord: " + text;
        return false;
    }
    // A chord is a simultaneous state: repeated aliases of one VK must not release it early.
    std::vector<int> unique;
    for (int vk : keys) if (std::find(unique.begin(), unique.end(), vk) == unique.end()) unique.push_back(vk);
    keys.swap(unique);
    return true;
}

inline std::vector<int> ParseCombo(const std::string& text) {
    std::vector<int> keys;
    ParseChord(text, keys);
    return keys;
}
}  // namespace vi_keys
