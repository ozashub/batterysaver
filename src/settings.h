#pragma once
#include "mode.h"
#include <string>
#include <vector>

struct Settings {
    Mode active_mode              = Mode::Balanced;
    bool respect_ac               = true;
    bool start_with_windows       = false;
    bool show_notifications       = true;
    int  timer_interval_sec       = 5;
    int  low_battery_pct          = 20;
    Mode low_battery_escalate     = Mode::Aggressive;
    std::vector<std::string> user_whitelist;

    ModeConfig passive_cfg;
    ModeConfig balanced_cfg;
    ModeConfig aggressive_cfg;
    ModeConfig custom_cfg;

    int config_version = 1;

    Settings();

    ModeConfig& cfg_for(Mode m);
    const ModeConfig& cfg_for(Mode m) const;
};

namespace SettingsIO {

std::wstring config_dir();
std::wstring config_path();

Settings load();
bool     save(const Settings& s);

} // namespace SettingsIO
