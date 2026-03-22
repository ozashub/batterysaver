#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

class PowerMonitor;

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool create(PowerMonitor* power);
    void destroy();
    void update_tooltip();
    void update_icon();

    HWND hwnd() const { return hwnd_; }

private:
    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT IDM_MODE_OFF        = 1001;
    static constexpr UINT IDM_MODE_PASSIVE    = 1002;
    static constexpr UINT IDM_MODE_BALANCED   = 1003;
    static constexpr UINT IDM_MODE_AGGRESSIVE = 1004;
    static constexpr UINT IDM_MODE_CUSTOM     = 1005;
    static constexpr UINT IDM_PAUSE           = 1010;
    static constexpr UINT IDM_EXIT            = 1020;

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    PowerMonitor* power_ = nullptr;
    HICON icon_ = nullptr;

    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    void show_context_menu();
    void on_command(UINT cmd);
    HICON make_icon(COLORREF color);
};
