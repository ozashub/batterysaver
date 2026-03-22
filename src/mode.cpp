#include "mode.h"

#include "platform.h"

#include <algorithm>
#include <cctype>

static std::string lowercase(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

Mode mode_from_string(std::string_view s) {
    auto lc = lowercase(s);
    if (lc == "off")        return Mode::Off;
    if (lc == "passive")    return Mode::Passive;
    if (lc == "balanced")   return Mode::Balanced;
    if (lc == "aggressive") return Mode::Aggressive;
    if (lc == "custom")     return Mode::Custom;
    return Mode::Balanced;
}

const char* mode_to_string(Mode m) {
    switch (m) {
    case Mode::Off:        return "off";
    case Mode::Passive:    return "passive";
    case Mode::Balanced:   return "balanced";
    case Mode::Aggressive: return "aggressive";
    case Mode::Custom:     return "custom";
    }
    return "balanced";
}

ModeConfig mode_defaults(Mode m) {
    switch (m) {
    case Mode::Off:
        return { NORMAL_PRIORITY_CLASS, -1 };
    case Mode::Passive:
        return { BELOW_NORMAL_PRIORITY_CLASS, -1 };
    case Mode::Balanced:
        return { BELOW_NORMAL_PRIORITY_CLASS, 45 };
    case Mode::Aggressive:
        return { IDLE_PRIORITY_CLASS, 15 };
    case Mode::Custom:
        return { BELOW_NORMAL_PRIORITY_CLASS, 30 };
    }
    return { BELOW_NORMAL_PRIORITY_CLASS, 45 };
}

unsigned long priority_class_from_string(std::string_view s) {
    auto lc = lowercase(s);
    if (lc == "idle")         return IDLE_PRIORITY_CLASS;
    if (lc == "below_normal") return BELOW_NORMAL_PRIORITY_CLASS;
    if (lc == "normal")       return NORMAL_PRIORITY_CLASS;
    if (lc == "above_normal") return ABOVE_NORMAL_PRIORITY_CLASS;
    if (lc == "high")         return HIGH_PRIORITY_CLASS;
    return BELOW_NORMAL_PRIORITY_CLASS;
}

const char* priority_class_to_string(unsigned long pc) {
    switch (pc) {
    case IDLE_PRIORITY_CLASS:         return "IDLE";
    case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL";
    case NORMAL_PRIORITY_CLASS:       return "NORMAL";
    case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL";
    case HIGH_PRIORITY_CLASS:         return "HIGH";
    }
    return "BELOW_NORMAL";
}
