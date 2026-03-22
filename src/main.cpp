#include "app.h"
#include "console_log.h"
#include "elevation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <string_view>

static constexpr const wchar_t* MUTEX_NAME = L"Global\\BatterySaverMutex";

static bool already_running() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mtx) CloseHandle(mtx);
        return true;
    }
    // leak the mutex handle intentionally — held for process lifetime
    return false;
}

static LaunchMode parse_args(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--console")
            return LaunchMode::Console;
        if (arg == L"--service")
            return LaunchMode::Service;
    }
    return LaunchMode::Tray;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    auto launch = parse_args(argc, argv);
    LocalFree(argv);

    bool console = (launch == LaunchMode::Console);
    Log::init(console);

    if (already_running()) {
        Log::info("another instance is already running");
        Log::shutdown();
        return 0;
    }

    if (!Elevation::is_elevated()) {
        Log::info("not elevated, relaunching with UAC");
        Elevation::relaunch_elevated();
        Log::shutdown();
        return 0;
    }

    Log::info("running elevated");

    App app(launch);
    int rc = app.run();

    Log::shutdown();
    return rc;
}
