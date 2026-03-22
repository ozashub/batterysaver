#pragma once

class ProcessManager;

class FocusWatcher {
public:
    explicit FocusWatcher(ProcessManager& pm);
    ~FocusWatcher();

    FocusWatcher(const FocusWatcher&) = delete;
    FocusWatcher& operator=(const FocusWatcher&) = delete;

    bool start();
    void stop();

private:
    ProcessManager& pm_;
    void* hook_ = nullptr;

    static void __stdcall win_event_cb(void* hook, unsigned long event,
        void* hwnd, long obj, long child, unsigned long thread, unsigned long time);
};
