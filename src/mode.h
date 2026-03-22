#pragma once
#include <string>
#include <string_view>

enum class Mode { Off, Passive, Balanced, Aggressive, Custom };

struct ModeConfig {
    unsigned long priority_class = 0x00004000; // BELOW_NORMAL_PRIORITY_CLASS
    int suspend_threshold_sec = -1;            // -1 = never suspend
};

Mode         mode_from_string(std::string_view s);
const char*  mode_to_string(Mode m);
ModeConfig   mode_defaults(Mode m);

unsigned long priority_class_from_string(std::string_view s);
const char*   priority_class_to_string(unsigned long pc);
