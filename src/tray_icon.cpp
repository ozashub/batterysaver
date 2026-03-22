#include "tray_icon.h"
#include "power_monitor.h"
#include "process_manager.h"
#include "settings_window.h"
#include "status_window.h"
#include "app.h"
#include "console_log.h"

#include <format>
#include <shellapi.h>

static const wchar_t* WND_CLASS = L"BatterySaverMsg";

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() { destroy(); }

HICON TrayIcon::make_icon(COLORREF color) {
    // 16x16 filled circle — distinct per mode, no external .ico needed for now
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, nullptr);
    auto old = SelectObject(dc, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    RECT r = { 0, 0, 16, 16 };
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    HBRUSH fill = CreateSolidBrush(color);
    auto old_brush = SelectObject(dc, fill);
    Ellipse(dc, 2, 2, 14, 14);
    SelectObject(dc, old_brush);
    DeleteObject(fill);

    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask = mask;
    HICON ico = CreateIconIndirect(&ii);

    DeleteObject(bmp);
    DeleteObject(mask);
    return ico;
}

static COLORREF mode_color(Mode m, bool paused) {
    if (paused) return RGB(128, 128, 128);
    switch (m) {
    case Mode::Off:        return RGB(80, 80, 80);
    case Mode::Passive:    return RGB(100, 180, 100);
    case Mode::Balanced:   return RGB(60, 140, 220);
    case Mode::Aggressive: return RGB(220, 80, 60);
    case Mode::Custom:     return RGB(180, 140, 60);
    }
    return RGB(60, 140, 220);
}

bool TrayIcon::create(PowerMonitor* power) {
    power_ = power;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, WND_CLASS, L"BatterySaver", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!hwnd_) {
        Log::error("tray message window creation failed", GetLastError());
        return false;
    }

    auto* app = app_instance();
    icon_ = make_icon(mode_color(app ? app->active_mode() : Mode::Balanced,
                                  app ? app->paused() : false));

    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = icon_;
    wcscpy_s(nid_.szTip, L"BatterySaver");

    Shell_NotifyIconW(NIM_ADD, &nid_);
    update_tooltip();

    Log::info("tray icon created");
    return true;
}

void TrayIcon::destroy() {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        nid_.hWnd = nullptr;
    }
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::update_tooltip() {
    auto* app = app_instance();
    if (!app) return;

    auto snap = app->proc_mgr() ? app->proc_mgr()->snapshot() : decltype(app->proc_mgr()->snapshot()){};
    int active = 0, suspended = 0;
    for (auto& [pid, tp] : snap) {
        if (tp.state == ProcessState::Suspended) ++suspended;
        else ++active;
    }

    auto tip = std::format(L"BatterySaver [{}] \u2014 {} active / {} suspended",
        [&]() -> const wchar_t* {
            if (app->paused()) return L"Paused";
            switch (app->active_mode()) {
            case Mode::Off:        return L"Off";
            case Mode::Passive:    return L"Passive";
            case Mode::Balanced:   return L"Balanced";
            case Mode::Aggressive: return L"Aggressive";
            case Mode::Custom:     return L"Custom";
            }
            return L"?";
        }(),
        active, suspended);

    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::update_icon() {
    auto* app = app_instance();
    if (!app) return;

    if (icon_) DestroyIcon(icon_);
    icon_ = make_icon(mode_color(app->active_mode(), app->paused()));
    nid_.hIcon = icon_;
    nid_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

LRESULT CALLBACK TrayIcon::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_POWERBROADCAST && wp == PBT_POWERSETTINGCHANGE && self && self->power_) {
        self->power_->handle_power_event(wp, lp);
        self->update_tooltip();
        self->update_icon();
        return TRUE;
    }

    if (msg == WM_TRAYICON && self) {
        if (LOWORD(lp) == WM_RBUTTONUP)
            self->show_context_menu();
        else if (LOWORD(lp) == WM_LBUTTONUP)
            StatusWindow::show();
        return 0;
    }

    if (msg == WM_COMMAND && self) {
        self->on_command(LOWORD(wp));
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayIcon::show_context_menu() {
    auto* app = app_instance();
    if (!app) return;

    HMENU menu = CreatePopupMenu();
    HMENU modes = CreatePopupMenu();

    auto cur = app->active_mode();
    auto check = [&](UINT id, const wchar_t* label, Mode m) {
        AppendMenuW(modes, MF_STRING | (cur == m ? MF_CHECKED : 0), id, label);
    };
    check(IDM_MODE_OFF,        L"Off",        Mode::Off);
    check(IDM_MODE_PASSIVE,    L"Passive",    Mode::Passive);
    check(IDM_MODE_BALANCED,   L"Balanced",   Mode::Balanced);
    check(IDM_MODE_AGGRESSIVE, L"Aggressive", Mode::Aggressive);
    check(IDM_MODE_CUSTOM,     L"Custom",     Mode::Custom);

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"Mode");
    AppendMenuW(menu, MF_STRING | (app->paused() ? MF_CHECKED : 0), IDM_PAUSE, L"Pause");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_STATUS, L"View Status");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessage(hwnd_, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

void TrayIcon::on_command(UINT cmd) {
    auto* app = app_instance();
    if (!app) return;

    switch (cmd) {
    case IDM_MODE_OFF:        app->set_mode(Mode::Off); break;
    case IDM_MODE_PASSIVE:    app->set_mode(Mode::Passive); break;
    case IDM_MODE_BALANCED:   app->set_mode(Mode::Balanced); break;
    case IDM_MODE_AGGRESSIVE: app->set_mode(Mode::Aggressive); break;
    case IDM_MODE_CUSTOM:     app->set_mode(Mode::Custom); break;
    case IDM_PAUSE:           app->set_paused(!app->paused()); break;
    case IDM_STATUS:          StatusWindow::show(); return;
    case IDM_SETTINGS:        SettingsWindow::show(hwnd_); return;
    case IDM_EXIT:            app->request_shutdown(); return;
    }

    update_icon();
    update_tooltip();
}
