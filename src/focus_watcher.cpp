#include "focus_watcher.h"
#include "process_manager.h"
#include "console_log.h"

#include "platform.h"

static FocusWatcher* s_instance = nullptr;

FocusWatcher::FocusWatcher(ProcessManager& pm) : pm_(pm) {
    s_instance = this;
}

FocusWatcher::~FocusWatcher() {
    stop();
    s_instance = nullptr;
}

bool FocusWatcher::start() {
    hook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr,
        reinterpret_cast<WINEVENTPROC>(win_event_cb),
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!hook_) {
        Log::error("SetWinEventHook failed", GetLastError());
        return false;
    }
    Log::info("focus watcher started");
    return true;
}

void FocusWatcher::stop() {
    if (hook_) {
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(hook_));
        hook_ = nullptr;
    }
}

void __stdcall FocusWatcher::win_event_cb(void*, unsigned long, void* hwnd,
    long obj, long child, unsigned long, unsigned long)
{
    if (obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (!s_instance) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(static_cast<HWND>(hwnd), &pid);
    if (pid == 0) return;

    s_instance->pm_.on_foreground_changed(pid);
}
