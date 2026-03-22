#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

class SettingsWindow {
public:
    static void show(HWND parent);

private:
    static INT_PTR CALLBACK dlg_proc(HWND, UINT, WPARAM, LPARAM);
    static void init_controls(HWND dlg);
    static void apply(HWND dlg);
};
