#include "power_monitor.h"
#include "console_log.h"
#include "app.h"

#include <format>

PowerMonitor::PowerMonitor() = default;

PowerMonitor::~PowerMonitor() { stop(); }

bool PowerMonitor::start(HWND msg_hwnd) {
    poll_initial_state();

    reg_acdc_ = RegisterPowerSettingNotification(msg_hwnd,
        &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    reg_batt_ = RegisterPowerSettingNotification(msg_hwnd,
        &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_WINDOW_HANDLE);

    if (!reg_acdc_ || !reg_batt_) {
        Log::warn("some power notifications failed to register");
    }

    Log::info(std::format("power: {} at {}%",
        source_ == PowerSource::AC ? "AC" : "battery", battery_pct_));
    return true;
}

void PowerMonitor::stop() {
    if (reg_acdc_) { UnregisterPowerSettingNotification(reg_acdc_); reg_acdc_ = nullptr; }
    if (reg_batt_) { UnregisterPowerSettingNotification(reg_batt_); reg_batt_ = nullptr; }
}

void PowerMonitor::poll_initial_state() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return;

    source_ = (sps.ACLineStatus == 1) ? PowerSource::AC : PowerSource::Battery;
    if (sps.BatteryLifePercent != 255)
        battery_pct_ = sps.BatteryLifePercent;
}

void PowerMonitor::handle_power_event(WPARAM, LPARAM lp) {
    auto* pbs = reinterpret_cast<POWERBROADCAST_SETTING*>(lp);
    if (!pbs) return;

    if (pbs->PowerSetting == GUID_ACDC_POWER_SOURCE && pbs->DataLength >= sizeof(DWORD)) {
        DWORD val = *reinterpret_cast<DWORD*>(pbs->Data);
        auto src = (val == 0) ? PowerSource::AC : PowerSource::Battery;
        on_source_changed(src);
    } else if (pbs->PowerSetting == GUID_BATTERY_PERCENTAGE_REMAINING && pbs->DataLength >= sizeof(DWORD)) {
        int pct = static_cast<int>(*reinterpret_cast<DWORD*>(pbs->Data));
        on_battery_level(pct);
    }
}

void PowerMonitor::on_source_changed(PowerSource src) {
    if (src == source_) return;
    source_ = src;
    Log::info(std::format("power source -> {}", src == PowerSource::AC ? "AC" : "battery"));

    auto* app = app_instance();
    if (!app || !app->settings().respect_ac) return;

    if (src == PowerSource::AC) {
        app->set_mode(Mode::Off);
    } else {
        // restore last battery mode — balanced if nothing was set
        auto m = app->settings().active_mode;
        if (m == Mode::Off)
            app->set_mode(Mode::Balanced);
    }
}

void PowerMonitor::on_battery_level(int pct) {
    battery_pct_ = pct;
    Log::debug(std::format("battery {}%", pct));

    auto* app = app_instance();
    if (!app) return;

    auto& s = app->settings();
    if (pct <= s.low_battery_pct && app->active_mode() != s.low_battery_escalate) {
        Log::info(std::format("low battery ({}%), escalating to {}", pct,
            mode_to_string(s.low_battery_escalate)));
        app->set_mode(s.low_battery_escalate);
    }
}
