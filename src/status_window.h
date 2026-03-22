#pragma once

#include "platform.h"

class StatusWindow {
public:
    static void show();
    static void close();

private:
    static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
    static void refresh(HWND hwnd);
    static void paint(HWND hwnd);
};
