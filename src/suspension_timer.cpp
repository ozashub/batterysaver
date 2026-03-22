#include "suspension_timer.h"
#include "process_manager.h"
#include "app.h"
#include "console_log.h"

SuspensionTimer::SuspensionTimer(ProcessManager& pm) : pm_(pm) {}

SuspensionTimer::~SuspensionTimer() { stop(); }

void SuspensionTimer::start(int interval_sec) {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&SuspensionTimer::loop, this, interval_sec);
    Log::info("suspension timer started");
}

void SuspensionTimer::stop() {
    running_.store(false);
    if (thread_.joinable())
        thread_.join();
}

void SuspensionTimer::loop(int interval_sec) {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
        if (!running_.load()) break;

        auto* app = app_instance();
        if (!app) continue;

        auto mode = app->active_mode();
        if (mode == Mode::Off || mode == Mode::Passive)
            continue;

        auto& cfg = app->settings().cfg_for(mode);
        pm_.tick(cfg);
    }
}
