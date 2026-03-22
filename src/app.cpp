#include "app.h"
#include "process_manager.h"
#include "focus_watcher.h"
#include "suspension_timer.h"
#include "power_monitor.h"
#include "tray_icon.h"
#include "console_log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <format>

static App* g_app = nullptr;

App* app_instance() { return g_app; }

static LONG WINAPI crash_handler(EXCEPTION_POINTERS*) {
    if (g_app && g_app->proc_mgr())
        g_app->proc_mgr()->resume_all();
    return EXCEPTION_CONTINUE_SEARCH;
}

App::App(LaunchMode launch)
    : launch_(launch)
    , settings_(SettingsIO::load())
{
    g_app = this;
    SetUnhandledExceptionFilter(crash_handler);
}

App::~App() {
    tray_.reset();
    power_.reset();
    timer_.reset();
    focus_.reset();
    if (proc_mgr_)
        proc_mgr_->resume_all();
    proc_mgr_.reset();
    g_app = nullptr;
}

void App::set_mode(Mode m) {
    auto old = settings_.active_mode;
    settings_.active_mode = m;
    SettingsIO::save(settings_);

    if (proc_mgr_) {
        if (m == Mode::Off) {
            proc_mgr_->resume_all();
        } else {
            proc_mgr_->set_mode_config(settings_.cfg_for(m));
        }
    }

    Log::info(std::format("mode {} -> {}", mode_to_string(old), mode_to_string(m)));
}

void App::set_paused(bool p) {
    paused_ = p;
    if (p && proc_mgr_)
        proc_mgr_->resume_all();
    Log::info(paused_ ? "paused" : "unpaused");
}

void App::request_shutdown() {
    shutdown_.store(true);
    PostQuitMessage(0);
}

int App::run() {
    Log::info(std::format("BatterySaver started [mode={}] [launch={}]",
        mode_to_string(settings_.active_mode),
        launch_ == LaunchMode::Console ? "console" :
        launch_ == LaunchMode::Service ? "service" : "tray"));

    proc_mgr_ = std::make_unique<ProcessManager>();
    if (!proc_mgr_->init()) {
        Log::error("process manager init failed");
        return 1;
    }

    focus_ = std::make_unique<FocusWatcher>(*proc_mgr_);
    if (!focus_->start()) {
        Log::error("focus watcher init failed");
        return 1;
    }

    timer_ = std::make_unique<SuspensionTimer>(*proc_mgr_);
    timer_->start(settings_.timer_interval_sec);

    tray_ = std::make_unique<TrayIcon>();
    power_ = std::make_unique<PowerMonitor>();

    if (tray_->create(power_.get()))
        power_->start(tray_->hwnd());

    pump_messages();

    Log::info("shutting down");
    tray_->destroy();
    power_->stop();
    timer_->stop();
    focus_->stop();
    proc_mgr_->resume_all();
    SettingsIO::save(settings_);
    return 0;
}

void App::pump_messages() {
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (shutdown_.load())
            break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
