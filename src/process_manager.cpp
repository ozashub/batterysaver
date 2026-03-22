#include "pch.h"
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

static bool name_eq(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i])) return false;
    }
    return true;
}

void ProcessManager::scan_existing() {
    auto* app = app_instance();
    if (!app || app->active_mode() == Mode::Off) return;

    HWND fg = GetForegroundWindow();
    DWORD fg_pid_now = 0;
    if (fg) GetWindowThreadProcessId(fg, &fg_pid_now);

    auto fg_exe = exe_name(fg_pid_now);
    auto& cfg = app->settings().cfg_for(app->active_mode());
    auto now = std::chrono::steady_clock::now();

    DWORD pids[2048];
    DWORD bytes_returned = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytes_returned)) return;

    DWORD count = bytes_returned / sizeof(DWORD);

    std::lock_guard lock(mtx_);
    fg_pid_ = fg_pid_now;
    fg_name_ = fg_exe;

    for (DWORD i = 0; i < count; ++i) {
        DWORD pid = pids[i];
        if (pid == fg_pid_now) continue;
        if (tracked_.count(pid)) continue;

        auto name = exe_name(pid);
        if (name.empty() || !is_manageable(pid, name)) continue;
        if (!fg_exe.empty() && name_eq(name, fg_exe)) continue;

        auto& tp = tracked_[pid];
        tp.pid = pid;
        tp.name = name;
        tp.last_active = now;
        tp.state_changed = now;
        deprioritise(tp, cfg.priority_class);
    }

    Log::info(std::format("scan: {} processes tracked", tracked_.size()));
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

    auto new_name = exe_name(new_fg_pid);
    if (new_name.empty()) return;

    // restore ALL processes with the same exe name as the new foreground
    for (auto& [pid, tp] : tracked_) {
        if (!name_eq(tp.name, new_name)) continue;
        if (tp.state == ProcessState::Suspended)
            resume(tp);
        if (tp.state == ProcessState::Deprioritised)
            restore_priority(tp);
        tp.state = ProcessState::Active;
        tp.last_active = now;
        tp.state_changed = now;
    }

    Log::info(std::format("focus -> {} (pid {})", narrow(new_name), new_fg_pid));

    auto* app = app_instance();
    if (!app) { fg_pid_ = new_fg_pid; fg_name_ = new_name; return; }

    auto mode = app->active_mode();
    if (mode == Mode::Off) { fg_pid_ = new_fg_pid; fg_name_ = new_name; return; }

    auto& cfg = app->settings().cfg_for(mode);

    // deprioritise the old foreground (only if it's a different exe)
    if (fg_pid_ != 0 && !name_eq(fg_name_, new_name)) {
        auto old_name = exe_name(fg_pid_);
        if (!old_name.empty() && is_manageable(fg_pid_, old_name)) {
            auto& tp = tracked_[fg_pid_];
            tp.pid = fg_pid_;
            tp.name = old_name;
            tp.last_active = now;
            tp.state_changed = now;
            deprioritise(tp, cfg.priority_class);
        }
    }

    fg_pid_ = new_fg_pid;
    fg_name_ = new_name;
}

void ProcessManager::tick(const ModeConfig& cfg) {
    if (cfg.suspend_threshold_sec <= 0) return;

    // re-scan for new processes each tick
    scan_existing();

    std::vector<unsigned long> to_suspend;
    std::vector<unsigned long> dead;

    {
        std::lock_guard lock(mtx_);
        auto now = std::chrono::steady_clock::now();

        for (auto& [pid, tp] : tracked_) {
            if (pid == fg_pid_) continue;
            if (!fg_name_.empty() && name_eq(tp.name, fg_name_)) continue;
            if (tp.state == ProcessState::Suspended) continue;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) { dead.push_back(pid); continue; }
            CloseHandle(h);

            auto idle_sec = std::chrono::duration_cast<std::chrono::seconds>(
                now - tp.last_active).count();
            if (idle_sec >= cfg.suspend_threshold_sec) {
                if (!Whitelist::has_active_audio(pid))
                    to_suspend.push_back(pid);
            }
        }

        for (auto pid : dead)
            tracked_.erase(pid);
    }

    // suspend outside the lock so focus changes aren't blocked
    for (auto pid : to_suspend) {
        std::lock_guard lock(mtx_);
        if (auto it = tracked_.find(pid); it != tracked_.end())
            suspend(it->second);
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
