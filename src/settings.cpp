#include "settings.h"
#include "console_log.h"

#include "platform.h"
#include <ShlObj.h>

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

Settings::Settings()
    : passive_cfg(mode_defaults(Mode::Passive))
    , balanced_cfg(mode_defaults(Mode::Balanced))
    , aggressive_cfg(mode_defaults(Mode::Aggressive))
    , custom_cfg(mode_defaults(Mode::Custom))
{}

ModeConfig& Settings::cfg_for(Mode m) {
    switch (m) {
    case Mode::Passive:    return passive_cfg;
    case Mode::Balanced:   return balanced_cfg;
    case Mode::Aggressive: return aggressive_cfg;
    case Mode::Custom:     return custom_cfg;
    default:               return balanced_cfg;
    }
}

const ModeConfig& Settings::cfg_for(Mode m) const {
    return const_cast<Settings*>(this)->cfg_for(m);
}

static json mode_cfg_to_json(const ModeConfig& mc) {
    return {
        {"priorityClass",           priority_class_to_string(mc.priority_class)},
        {"suspendThresholdSeconds", mc.suspend_threshold_sec}
    };
}

static ModeConfig mode_cfg_from_json(const json& j, Mode fallback) {
    ModeConfig mc = mode_defaults(fallback);
    if (j.contains("priorityClass") && j["priorityClass"].is_string())
        mc.priority_class = priority_class_from_string(j["priorityClass"].get<std::string>());
    if (j.contains("suspendThresholdSeconds") && j["suspendThresholdSeconds"].is_number_integer())
        mc.suspend_threshold_sec = j["suspendThresholdSeconds"].get<int>();
    return mc;
}

std::wstring SettingsIO::config_dir() {
    wchar_t* appdata = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata);
    std::wstring dir = std::wstring(appdata) + L"\\BatterySaver";
    CoTaskMemFree(appdata);
    return dir;
}

std::wstring SettingsIO::config_path() {
    return config_dir() + L"\\settings.json";
}

Settings SettingsIO::load() {
    Settings s;
    auto path = config_path();

    if (!fs::exists(path)) {
        fs::create_directories(config_dir());
        save(s);
        return s;
    }

    try {
        std::ifstream f(path);
        auto j = json::parse(f, nullptr, false);
        if (j.is_discarded()) {
            Log::warn("config parse failed, using defaults");
            return s;
        }

        if (j.contains("mode") && j["mode"].is_string())
            s.active_mode = mode_from_string(j["mode"].get<std::string>());
        if (j.contains("respectACState") && j["respectACState"].is_boolean())
            s.respect_ac = j["respectACState"].get<bool>();
        if (j.contains("startWithWindows") && j["startWithWindows"].is_boolean())
            s.start_with_windows = j["startWithWindows"].get<bool>();
        if (j.contains("showNotifications") && j["showNotifications"].is_boolean())
            s.show_notifications = j["showNotifications"].get<bool>();
        if (j.contains("timerIntervalSeconds") && j["timerIntervalSeconds"].is_number_integer())
            s.timer_interval_sec = j["timerIntervalSeconds"].get<int>();
        if (j.contains("lowBatteryThreshold") && j["lowBatteryThreshold"].is_number_integer())
            s.low_battery_pct = j["lowBatteryThreshold"].get<int>();
        if (j.contains("lowBatteryEscalateTo") && j["lowBatteryEscalateTo"].is_string())
            s.low_battery_escalate = mode_from_string(j["lowBatteryEscalateTo"].get<std::string>());

        if (j.contains("userWhitelist") && j["userWhitelist"].is_array()) {
            for (auto& entry : j["userWhitelist"]) {
                if (entry.is_string())
                    s.user_whitelist.push_back(entry.get<std::string>());
            }
        }

        if (j.contains("modes") && j["modes"].is_object()) {
            auto& m = j["modes"];
            if (m.contains("passive"))    s.passive_cfg    = mode_cfg_from_json(m["passive"],    Mode::Passive);
            if (m.contains("balanced"))   s.balanced_cfg   = mode_cfg_from_json(m["balanced"],   Mode::Balanced);
            if (m.contains("aggressive")) s.aggressive_cfg = mode_cfg_from_json(m["aggressive"], Mode::Aggressive);
            if (m.contains("custom"))     s.custom_cfg     = mode_cfg_from_json(m["custom"],     Mode::Custom);
        }

        if (j.contains("configVersion") && j["configVersion"].is_number_integer())
            s.config_version = j["configVersion"].get<int>();

    } catch (...) {
        Log::warn("config load exception, using defaults");
    }

    return s;
}

bool SettingsIO::save(const Settings& s) {
    json j = {
        {"configVersion",        s.config_version},
        {"mode",                 mode_to_string(s.active_mode)},
        {"respectACState",       s.respect_ac},
        {"startWithWindows",     s.start_with_windows},
        {"showNotifications",    s.show_notifications},
        {"timerIntervalSeconds", s.timer_interval_sec},
        {"lowBatteryThreshold",  s.low_battery_pct},
        {"lowBatteryEscalateTo", mode_to_string(s.low_battery_escalate)},
        {"userWhitelist",        s.user_whitelist},
        {"modes", {
            {"passive",    mode_cfg_to_json(s.passive_cfg)},
            {"balanced",   mode_cfg_to_json(s.balanced_cfg)},
            {"aggressive", mode_cfg_to_json(s.aggressive_cfg)},
            {"custom",     mode_cfg_to_json(s.custom_cfg)}
        }}
    };

    auto dir = config_dir();
    fs::create_directories(dir);

    // atomic write: tmp then rename
    auto path = config_path();
    auto tmp  = path + L".tmp";

    try {
        std::ofstream f(tmp);
        f << j.dump(2);
        f.close();
        if (f.fail()) {
            Log::error("failed writing config tmp file");
            return false;
        }
        fs::rename(tmp, path);
    } catch (...) {
        Log::error("config save exception");
        return false;
    }

    return true;
}
