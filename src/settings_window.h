#pragma once

#include "platform.h"

class SettingsWindow {
public:
    static void show(HWND parent);

private:
    static INT_PTR CALLBACK dlg_proc(HWND, UINT, WPARAM, LPARAM);
    static void init_controls(HWND dlg);
    static void apply(HWND dlg);
};
