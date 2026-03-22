#pragma once
#include "settings.h"
#include <atomic>

enum class LaunchMode { Tray, Console, Service };

class App {
public:
    explicit App(LaunchMode launch);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();
    void request_shutdown();

    Settings&       settings()       { return settings_; }
    const Settings& settings() const { return settings_; }
    Mode            active_mode() const { return settings_.active_mode; }
    LaunchMode      launch_mode() const { return launch_; }

    void set_mode(Mode m);

private:
    LaunchMode launch_;
    Settings   settings_;
    std::atomic<bool> shutdown_{false};

    void pump_messages();
};

App* app_instance();
