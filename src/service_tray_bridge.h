#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <thread>
#include <atomic>

class ServiceTrayBridge {
public:
    ServiceTrayBridge();
    ~ServiceTrayBridge();

    ServiceTrayBridge(const ServiceTrayBridge&) = delete;
    ServiceTrayBridge& operator=(const ServiceTrayBridge&) = delete;

    void spawn_for_session(DWORD session_id);
    void kill();

private:
    HANDLE helper_process_ = nullptr;
    std::atomic<bool> alive_{false};

    bool launch_into_session(DWORD session_id);
};
