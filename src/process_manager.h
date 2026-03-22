#pragma once
#include "mode.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

enum class ProcessState { Active, Deprioritised, Suspended };

struct TrackedProcess {
    unsigned long pid = 0;
    std::wstring  name;
    ProcessState  state = ProcessState::Active;
    unsigned long original_priority = 0x00000020; // NORMAL_PRIORITY_CLASS
    std::chrono::steady_clock::time_point last_active;
    std::chrono::steady_clock::time_point state_changed;
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    bool init();
    void scan_existing();

    void on_foreground_changed(unsigned long new_fg_pid);
    void tick(const ModeConfig& cfg);
    void resume_all();

    void set_mode_config(const ModeConfig& cfg);

    std::unordered_map<unsigned long, TrackedProcess> snapshot() const;

private:
    using NtSuspendFn = long(__stdcall*)(void*);
    using NtResumeFn  = long(__stdcall*)(void*);

    NtSuspendFn nt_suspend_ = nullptr;
    NtResumeFn  nt_resume_  = nullptr;

    mutable std::mutex mtx_;
    std::unordered_map<unsigned long, TrackedProcess> tracked_;
    unsigned long fg_pid_ = 0;

    void deprioritise(TrackedProcess& tp, unsigned long pclass);
    void suspend(TrackedProcess& tp);
    void resume(TrackedProcess& tp);
    void restore_priority(TrackedProcess& tp);

    bool is_manageable(unsigned long pid, const std::wstring& name) const;
    std::wstring exe_name(unsigned long pid) const;
};
