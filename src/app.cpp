#include "app.h"
#include "console_log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <format>

static App* g_app = nullptr;

App* app_instance() { return g_app; }

App::App(LaunchMode launch)
    : launch_(launch)
    , settings_(SettingsIO::load())
{
    g_app = this;
}

App::~App() {
    g_app = nullptr;
}

void App::set_mode(Mode m) {
    settings_.active_mode = m;
    SettingsIO::save(settings_);
    Log::info(std::format("mode -> {}", mode_to_string(m)));
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

    pump_messages();

    Log::info("shutting down");
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
