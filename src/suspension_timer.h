#pragma once
#include <thread>
#include <atomic>

class ProcessManager;

class SuspensionTimer {
public:
    explicit SuspensionTimer(ProcessManager& pm);
    ~SuspensionTimer();

    SuspensionTimer(const SuspensionTimer&) = delete;
    SuspensionTimer& operator=(const SuspensionTimer&) = delete;

    void start(int interval_sec);
    void stop();

private:
    ProcessManager& pm_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void loop(int interval_sec);
};
