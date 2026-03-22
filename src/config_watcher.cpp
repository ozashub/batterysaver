#include "pch.h"
#include "config_watcher.h"
#include "settings.h"
#include "console_log.h"

#include "platform.h"

ConfigWatcher::ConfigWatcher(Callback on_change) : cb_(std::move(on_change)) {}

ConfigWatcher::~ConfigWatcher() { stop(); }

bool ConfigWatcher::start() {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) return false;

    running_.store(true);
    thread_ = std::thread(&ConfigWatcher::watch_loop, this);
    Log::info("config watcher started");
    return true;
}

void ConfigWatcher::stop() {
    running_.store(false);
    if (stop_event_) {
        SetEvent(static_cast<HANDLE>(stop_event_));
    }
    if (thread_.joinable())
        thread_.join();
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
}

void ConfigWatcher::watch_loop() {
    auto dir = SettingsIO::config_dir();
    HANDLE h = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (h == INVALID_HANDLE_VALUE) {
        Log::warn("config watcher: FindFirstChangeNotification failed");
        return;
    }

    HANDLE handles[2] = { h, static_cast<HANDLE>(stop_event_) };

    while (running_.load()) {
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (!running_.load()) break;

        if (wait == WAIT_OBJECT_0) {
            // brief delay — file might still be mid-write
            Sleep(200);
            if (cb_) cb_();
            FindNextChangeNotification(h);
        } else {
            break;
        }
    }

    FindCloseChangeNotification(h);
}
