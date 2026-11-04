#pragma once
#include <cctype>
#include <string>

// Triggers are XInput byte axes, never invented bits in wButtons.
namespace vi_pad_buttons {
struct State { unsigned short buttons = 0; unsigned trigger_mask = 0; };
inline bool ParsePadButtonState(const std::string& names, State& state, std::string& error) {
    State parsed;
    size_t start = 0;
    while (start < names.size()) {
        const size_t end = names.find_first_of("+, ", start);
        std::string name = names.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        unsigned short bits = 0;
        if (name == "a") bits = 0x1000; else if (name == "b") bits = 0x2000;
        else if (name == "x") bits = 0x4000; else if (name == "y") bits = 0x8000;
        else if (name == "up") bits = 1; else if (name == "down") bits = 2;
        else if (name == "left") bits = 4; else if (name == "right") bits = 8;
        else if (name == "start") bits = 0x10; else if (name == "back") bits = 0x20;
        else if (name == "ls") bits = 0x40; else if (name == "rs") bits = 0x80;
        else if (name == "lb") bits = 0x100; else if (name == "rb") bits = 0x200;
        else if (name == "guide") bits = 0x400;
        else if (name == "lt") parsed.trigger_mask |= 1;
        else if (name == "rt") parsed.trigger_mask |= 2;
        else if (!name.empty() && name != "none") { error = "unknown pad button '" + name + "'"; return false; }
        parsed.buttons = static_cast<unsigned short>(parsed.buttons | bits);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    state = parsed;
    error.clear();
    return true;
}
inline bool ApplyTriggerAliases(unsigned aliases, int& mask, int* values, std::string& error) {
    if ((aliases & static_cast<unsigned>(mask)) != 0) {
        error = "LT/RT button alias conflicts with an explicit value for the same trigger (even255); use either the alias or the axis";
        return false;
    }
    for (int axis = 0; axis < 2; ++axis) if (aliases & (1u << axis)) {
        values[axis] = 255;
        mask |= 1 << axis;
    }
    error.clear();
    return true;
}
}
