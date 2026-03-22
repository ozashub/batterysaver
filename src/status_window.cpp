#include "status_window.h"
#include "process_manager.h"
#include "power_monitor.h"
#include "app.h"
#include "console_log.h"

#include <format>
#include <vector>
#include <algorithm>

static HWND s_wnd = nullptr;
static UINT_PTR s_timer = 0;

struct DisplayEntry {
    std::wstring name;
    unsigned long pid;
    ProcessState state;
    int seconds_in_state;
};

static std::vector<DisplayEntry> s_entries;

static const wchar_t* state_str(ProcessState s) {
    switch (s) {
    case ProcessState::Active:        return L"Active";
    case ProcessState::Deprioritised: return L"Deprioritised";
    case ProcessState::Suspended:     return L"Suspended";
    }
    return L"?";
}

void StatusWindow::refresh(HWND hwnd) {
    auto* app = app_instance();
    if (!app || !app->proc_mgr()) return;

    auto snap = app->proc_mgr()->snapshot();
    auto now = std::chrono::steady_clock::now();

    s_entries.clear();
    s_entries.reserve(snap.size());
    for (auto& [pid, tp] : snap) {
        auto sec = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
            now - tp.state_changed).count());
        s_entries.push_back({ tp.name, tp.pid, tp.state, sec });
    }
    std::sort(s_entries.begin(), s_entries.end(),
        [](auto& a, auto& b) { return a.name < b.name; });

    InvalidateRect(hwnd, nullptr, TRUE);
}

void StatusWindow::paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    auto old_font = SelectObject(dc, font);

    SetBkMode(dc, TRANSPARENT);

    auto* app = app_instance();
    int y = 10;

    if (app) {
        auto mode_str = [](Mode m) -> const wchar_t* {
            switch (m) {
            case Mode::Off:        return L"Off";
            case Mode::Passive:    return L"Passive";
            case Mode::Balanced:   return L"Balanced";
            case Mode::Aggressive: return L"Aggressive";
            case Mode::Custom:     return L"Custom";
            }
            return L"?";
        };
        auto line = std::format(L"Mode: {}   Tracked: {}",
            mode_str(app->active_mode()), s_entries.size());
        TextOutW(dc, 10, y, line.c_str(), static_cast<int>(line.size()));
    }

    y += 25;
    SetTextColor(dc, RGB(100, 100, 100));
    const wchar_t* hdr = L"Process                  PID      State           Time";
    TextOutW(dc, 10, y, hdr, static_cast<int>(wcslen(hdr)));
    y += 18;

    // divider
    RECT div = { 10, y, 460, y + 1 };
    auto gray = CreateSolidBrush(RGB(200, 200, 200));
    FillRect(dc, &div, gray);
    DeleteObject(gray);
    y += 5;

    SetTextColor(dc, RGB(30, 30, 30));
    for (auto& e : s_entries) {
        COLORREF col = RGB(30, 30, 30);
        if (e.state == ProcessState::Suspended)     col = RGB(180, 60, 40);
        else if (e.state == ProcessState::Deprioritised) col = RGB(60, 120, 180);
        SetTextColor(dc, col);

        auto truncated = e.name.substr(0, 22);
        while (truncated.size() < 22) truncated += L' ';

        auto line = std::format(L"{}  {:>6}   {:14s}  {}s",
            truncated, e.pid, state_str(e.state), e.seconds_in_state);
        TextOutW(dc, 10, y, line.c_str(), static_cast<int>(line.size()));
        y += 16;

        if (y > ps.rcPaint.bottom) break;
    }

    if (s_entries.empty()) {
        SetTextColor(dc, RGB(140, 140, 140));
        const wchar_t* msg = L"No processes tracked";
        TextOutW(dc, 10, y, msg, static_cast<int>(wcslen(msg)));
    }

    SelectObject(dc, old_font);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK StatusWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp; (void)lp;
    switch (msg) {
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_TIMER:
        refresh(hwnd);
        return 0;
    case WM_CLOSE:
        close();
        return 0;
    case WM_DESTROY:
        if (s_timer) { KillTimer(hwnd, s_timer); s_timer = 0; }
        s_wnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void StatusWindow::show() {
    if (s_wnd) {
        SetForegroundWindow(s_wnd);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BatterySaverStatus";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    s_wnd = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"BatterySaverStatus", L"BatterySaver Status",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    refresh(s_wnd);
    s_timer = SetTimer(s_wnd, 1, 1000, nullptr);

    ShowWindow(s_wnd, SW_SHOW);
    UpdateWindow(s_wnd);
}

void StatusWindow::close() {
    if (s_wnd) {
        DestroyWindow(s_wnd);
        s_wnd = nullptr;
    }
}
