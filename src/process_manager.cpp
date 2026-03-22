#include "process_manager.h"
#include "whitelist.h"
#include "console_log.h"
#include "app.h"

#include "platform.h"
#include <Psapi.h>

#include <format>

static std::string narrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
        out.data(), len, nullptr, nullptr);
    return out;
}

ProcessManager::ProcessManager() = default;
ProcessManager::~ProcessManager() { resume_all(); }

bool ProcessManager::init() {
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        Log::error("ntdll.dll not loaded");
        return false;
    }
    nt_suspend_ = reinterpret_cast<NtSuspendFn>(GetProcAddress(ntdll, "NtSuspendProcess"));
    nt_resume_  = reinterpret_cast<NtResumeFn>(GetProcAddress(ntdll, "NtResumeProcess"));
    if (!nt_suspend_ || !nt_resume_) {
        Log::error("NtSuspend/ResumeProcess not found in ntdll");
        return false;
    }
    return true;
}

std::wstring ProcessManager::exe_name(unsigned long pid) const {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH]{};
    DWORD sz = MAX_PATH;
    QueryFullProcessImageNameW(h, 0, buf, &sz);
    CloseHandle(h);
    std::wstring full(buf);
    auto slash = full.find_last_of(L'\\');
    return (slash != std::wstring::npos) ? full.substr(slash + 1) : full;
}

bool ProcessManager::is_manageable(unsigned long pid, const std::wstring& name) const {
    if (pid == 0 || pid == 4) return false;
    if (pid == GetCurrentProcessId()) return false;
    if (Whitelist::is_system_protected(name)) return false;
    if (Whitelist::is_critical_process(pid)) return false;

    auto* app = app_instance();
    if (app && Whitelist::matches_user_list(name, app->settings().user_whitelist))
        return false;
    if (!Whitelist::has_visible_window(pid))
        return false;
    return true;
}

void ProcessManager::on_foreground_changed(unsigned long new_fg_pid) {
    std::lock_guard lock(mtx_);
    auto now = std::chrono::steady_clock::now();

    if (auto it = tracked_.find(new_fg_pid); it != tracked_.end()) {
        auto& tp = it->second;
        if (tp.state == ProcessState::Suspended)
            resume(tp);
        if (tp.state == ProcessState::Deprioritised)
            restore_priority(tp);
        tp.state = ProcessState::Active;
        tp.last_active = now;
        tp.state_changed = now;
        Log::info(std::format("focus -> {} (pid {}), restored", narrow(tp.name), tp.pid));
    } else {
        auto name = exe_name(new_fg_pid);
        if (!name.empty()) {
            Log::info(std::format("focus -> {} (pid {})", narrow(name), new_fg_pid));
        }
    }

    auto* app = app_instance();
    if (!app) { fg_pid_ = new_fg_pid; return; }

    auto mode = app->active_mode();
    if (mode == Mode::Off) { fg_pid_ = new_fg_pid; return; }

    auto& cfg = app->settings().cfg_for(mode);

    if (fg_pid_ != 0 && fg_pid_ != new_fg_pid) {
        auto name = exe_name(fg_pid_);
        if (!name.empty() && is_manageable(fg_pid_, name)) {
            auto& tp = tracked_[fg_pid_];
            tp.pid = fg_pid_;
            tp.name = name;
            tp.last_active = now;
            tp.state_changed = now;
            deprioritise(tp, cfg.priority_class);
        }
    }

    fg_pid_ = new_fg_pid;
}

void ProcessManager::tick(const ModeConfig& cfg) {
    if (cfg.suspend_threshold_sec <= 0) return;

    std::lock_guard lock(mtx_);
    auto now = std::chrono::steady_clock::now();
    std::vector<unsigned long> dead;

    for (auto& [pid, tp] : tracked_) {
        if (pid == fg_pid_) continue;
        if (tp.state == ProcessState::Suspended) continue;

        // verify process still exists
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            dead.push_back(pid);
            continue;
        }
        CloseHandle(h);

        auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(
            now - tp.last_active).count();
        if (idle_sec >= cfg.suspend_threshold_sec) {
            if (Whitelist::has_active_audio(pid)) continue;
            suspend(tp);
        }
    }

    for (auto pid : dead) {
        Log::debug(std::format("pid {} exited, removing from tracking", pid));
        tracked_.erase(pid);
    }
}

void ProcessManager::resume_all() {
    std::lock_guard lock(mtx_);
    for (auto& [pid, tp] : tracked_) {
        if (tp.state == ProcessState::Suspended)
            resume(tp);
        if (tp.state == ProcessState::Deprioritised)
            restore_priority(tp);
        tp.state = ProcessState::Active;
    }
    tracked_.clear();
}

void ProcessManager::set_mode_config(const ModeConfig& cfg) {
    if (cfg.suspend_threshold_sec <= 0) {
        std::lock_guard lock(mtx_);
        for (auto& [pid, tp] : tracked_) {
            if (tp.state == ProcessState::Suspended) {
                resume(tp);
                tp.state = ProcessState::Deprioritised;
            }
        }
    }
}

void ProcessManager::deprioritise(TrackedProcess& tp, unsigned long pclass) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, tp.pid);
    if (!h) return;

    tp.original_priority = GetPriorityClass(h);
    if (SetPriorityClass(h, pclass)) {
        tp.state = ProcessState::Deprioritised;
        tp.state_changed = std::chrono::steady_clock::now();
        Log::debug(std::format("deprioritised {} (pid {}) -> {}",
            narrow(tp.name), tp.pid, priority_class_to_string(pclass)));
    }
    CloseHandle(h);
}

void ProcessManager::suspend(TrackedProcess& tp) {
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, tp.pid);
    if (!h) return;

    if (nt_suspend_(h) >= 0) {
        tp.state = ProcessState::Suspended;
        tp.state_changed = std::chrono::steady_clock::now();
        Log::info(std::format("suspended {} (pid {})", narrow(tp.name), tp.pid));
    }
    CloseHandle(h);
}

void ProcessManager::resume(TrackedProcess& tp) {
    HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, tp.pid);
    if (!h) return;

    if (nt_resume_(h) >= 0) {
        Log::info(std::format("resumed {} (pid {})", narrow(tp.name), tp.pid));
    }
    CloseHandle(h);
    restore_priority(tp);
    tp.state = ProcessState::Active;
    tp.state_changed = std::chrono::steady_clock::now();
}

void ProcessManager::restore_priority(TrackedProcess& tp) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, tp.pid);
    if (!h) return;
    SetPriorityClass(h, tp.original_priority);
    CloseHandle(h);
}

std::unordered_map<unsigned long, TrackedProcess> ProcessManager::snapshot() const {
    std::lock_guard lock(mtx_);
    return tracked_;
}
