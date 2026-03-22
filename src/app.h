#pragma once
#include "settings.h"
#include <atomic>
#include <memory>

enum class LaunchMode { Tray, Console, Service };

class ProcessManager;
class FocusWatcher;
class SuspensionTimer;

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
    bool            paused() const { return paused_; }

    ProcessManager* proc_mgr() { return proc_mgr_.get(); }

    void set_mode(Mode m);
    void set_paused(bool p);

private:
    LaunchMode launch_;
    Settings   settings_;
    std::atomic<bool> shutdown_{false};
    bool paused_ = false;

    std::unique_ptr<ProcessManager>  proc_mgr_;
    std::unique_ptr<FocusWatcher>    focus_;
    std::unique_ptr<SuspensionTimer> timer_;

    void pump_messages();
};

App* app_instance();
