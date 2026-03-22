#include "pch.h"
#include "settings_window.h"
#include "app.h"
#include "console_log.h"

#include <CommCtrl.h>
#include <format>
#include <string>

#pragma comment(lib, "comctl32.lib")

static constexpr int IDC_MODE_COMBO       = 2001;
static constexpr int IDC_START_WIN        = 2002;
static constexpr int IDC_RESPECT_AC       = 2003;
static constexpr int IDC_NOTIFICATIONS    = 2004;
static constexpr int IDC_TIMER_INTERVAL   = 2005;
static constexpr int IDC_LOW_BATT_PCT     = 2006;
static constexpr int IDC_LOW_BATT_MODE    = 2007;
static constexpr int IDC_APPLY            = 2010;

static HWND s_dlg = nullptr;

static int mode_index(Mode m) {
    switch (m) {
    case Mode::Off:        return 0;
    case Mode::Passive:    return 1;
    case Mode::Balanced:   return 2;
    case Mode::Aggressive: return 3;
    case Mode::Custom:     return 4;
    }
    return 2;
}

static Mode index_mode(int i) {
    switch (i) {
    case 0: return Mode::Off;
    case 1: return Mode::Passive;
    case 2: return Mode::Balanced;
    case 3: return Mode::Aggressive;
    case 4: return Mode::Custom;
    }
    return Mode::Balanced;
}

static HWND make_label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static HWND make_combo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowW(L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

static HWND make_check(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

static HWND make_edit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowW(L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

static HWND make_button(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

void SettingsWindow::init_controls(HWND dlg) {
    auto* app = app_instance();
    if (!app) return;
    auto& s = app->settings();

    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessage(dlg, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    int y = 15;
    int lx = 20, cx = 180, w = 160, lw = 150;

    make_label(dlg, L"Active Mode:", lx, y + 3, lw, 20);
    auto combo = make_combo(dlg, IDC_MODE_COMBO, cx, y, w, 200);
    const wchar_t* modes[] = { L"Off", L"Passive", L"Balanced", L"Aggressive", L"Custom" };
    for (auto* m : modes) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m));
    SendMessageW(combo, CB_SETCURSEL, mode_index(s.active_mode), 0);

    y += 35;
    auto chk_win = make_check(dlg, L"Start with Windows", IDC_START_WIN, lx, y, 250, 20);
    if (s.start_with_windows) SendMessageW(chk_win, BM_SETCHECK, BST_CHECKED, 0);

    y += 25;
    auto chk_ac = make_check(dlg, L"Switch to Off on AC power", IDC_RESPECT_AC, lx, y, 250, 20);
    if (s.respect_ac) SendMessageW(chk_ac, BM_SETCHECK, BST_CHECKED, 0);

    y += 25;
    auto chk_notif = make_check(dlg, L"Show notifications", IDC_NOTIFICATIONS, lx, y, 250, 20);
    if (s.show_notifications) SendMessageW(chk_notif, BM_SETCHECK, BST_CHECKED, 0);

    y += 35;
    make_label(dlg, L"Timer interval (sec):", lx, y + 3, lw, 20);
    auto ed_timer = make_edit(dlg, IDC_TIMER_INTERVAL, cx, y, 60, 22);
    SetWindowTextW(ed_timer, std::to_wstring(s.timer_interval_sec).c_str());

    y += 35;
    make_label(dlg, L"Low battery (%):", lx, y + 3, lw, 20);
    auto ed_batt = make_edit(dlg, IDC_LOW_BATT_PCT, cx, y, 60, 22);
    SetWindowTextW(ed_batt, std::to_wstring(s.low_battery_pct).c_str());

    y += 35;
    make_label(dlg, L"Escalate to:", lx, y + 3, lw, 20);
    auto esc_combo = make_combo(dlg, IDC_LOW_BATT_MODE, cx, y, w, 200);
    for (auto* m : modes) SendMessageW(esc_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m));
    SendMessageW(esc_combo, CB_SETCURSEL, mode_index(s.low_battery_escalate), 0);

    y += 45;
    make_button(dlg, L"Apply", IDC_APPLY, cx, y, 80, 28);

    // apply font to all children
    EnumChildWindows(dlg, [](HWND child, LPARAM lp) -> BOOL {
        SendMessage(child, WM_SETFONT, lp, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

void SettingsWindow::apply(HWND dlg) {
    auto* app = app_instance();
    if (!app) return;
    auto& s = app->settings();

    int idx = static_cast<int>(SendDlgItemMessageW(dlg, IDC_MODE_COMBO, CB_GETCURSEL, 0, 0));
    if (idx >= 0) app->set_mode(index_mode(idx));

    s.start_with_windows = SendDlgItemMessageW(dlg, IDC_START_WIN, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.respect_ac         = SendDlgItemMessageW(dlg, IDC_RESPECT_AC, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.show_notifications = SendDlgItemMessageW(dlg, IDC_NOTIFICATIONS, BM_GETCHECK, 0, 0) == BST_CHECKED;

    wchar_t buf[16]{};
    GetDlgItemTextW(dlg, IDC_TIMER_INTERVAL, buf, 16);
    int timer = _wtoi(buf);
    if (timer > 0) s.timer_interval_sec = timer;

    GetDlgItemTextW(dlg, IDC_LOW_BATT_PCT, buf, 16);
    int pct = _wtoi(buf);
    if (pct >= 0 && pct <= 100) s.low_battery_pct = pct;

    int esc = static_cast<int>(SendDlgItemMessageW(dlg, IDC_LOW_BATT_MODE, CB_GETCURSEL, 0, 0));
    if (esc >= 0) s.low_battery_escalate = index_mode(esc);

    // start-with-windows registry key
    HKEY key = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key);
    if (key) {
        if (s.start_with_windows) {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            RegSetValueExW(key, L"BatterySaver", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(path),
                static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(key, L"BatterySaver");
        }
        RegCloseKey(key);
    }

    SettingsIO::save(s);
    Log::info("settings applied");
}

INT_PTR CALLBACK SettingsWindow::dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_CREATE:
        init_controls(dlg);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_APPLY)
            apply(dlg);
        return 0;
    case WM_CLOSE:
        ShowWindow(dlg, SW_HIDE);
        s_dlg = nullptr;
        DestroyWindow(dlg);
        return 0;
    case WM_DESTROY:
        s_dlg = nullptr;
        return 0;
    }
    return DefWindowProcW(dlg, msg, wp, reinterpret_cast<LPARAM>(nullptr));
}

void SettingsWindow::show(HWND) {
    if (s_dlg) {
        SetForegroundWindow(s_dlg);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(dlg_proc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BatterySaverSettings";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    s_dlg = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"BatterySaverSettings", L"BatterySaver Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 380, 350,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    SendMessage(s_dlg, WM_CREATE, 0, 0);
    ShowWindow(s_dlg, SW_SHOW);
    UpdateWindow(s_dlg);
}
