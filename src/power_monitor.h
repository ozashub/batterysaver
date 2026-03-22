#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

enum class PowerSource { AC, Battery, Unknown };

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();

    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    bool start(HWND msg_hwnd);
    void stop();

    void handle_power_event(WPARAM wp, LPARAM lp);

    PowerSource source() const { return source_; }
    int         battery_pct() const { return battery_pct_; }

private:
    HPOWERNOTIFY reg_acdc_ = nullptr;
    HPOWERNOTIFY reg_batt_ = nullptr;
    PowerSource  source_ = PowerSource::Unknown;
    int          battery_pct_ = 100;

    void poll_initial_state();
    void on_source_changed(PowerSource src);
    void on_battery_level(int pct);
};
