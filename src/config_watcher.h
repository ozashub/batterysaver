#pragma once
#include <thread>
#include <atomic>
#include <functional>

class ConfigWatcher {
public:
    using Callback = std::function<void()>;

    explicit ConfigWatcher(Callback on_change);
    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    bool start();
    void stop();

private:
    Callback cb_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    void* stop_event_ = nullptr;

    void watch_loop();
};
