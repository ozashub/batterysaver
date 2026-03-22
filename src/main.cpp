#include "app.h"
#include "console_log.h"
#include "elevation.h"
#include "service_host.h"

#include "platform.h"
#include <string_view>

static constexpr const wchar_t* MUTEX_NAME = L"Global\\BatterySaverMutex";

enum class CmdAction { Run, InstallService, UninstallService };

struct ParsedArgs {
    LaunchMode launch = LaunchMode::Tray;
    CmdAction  action = CmdAction::Run;
};

static bool already_running() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mtx) CloseHandle(mtx);
        return true;
    }
    // leak intentionally — held for process lifetime
    return false;
}

static ParsedArgs parse_args(int argc, wchar_t** argv) {
    ParsedArgs p;
    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--console")           p.launch = LaunchMode::Console;
        else if (arg == L"--service")      p.launch = LaunchMode::Service;
        else if (arg == L"--install-service")   p.action = CmdAction::InstallService;
        else if (arg == L"--uninstall-service") p.action = CmdAction::UninstallService;
    }
    return p;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto args = parse_args(argc, argv);
    LocalFree(argv);

    bool console = (args.launch == LaunchMode::Console);
    Log::init(console);

    if (args.action == CmdAction::InstallService) {
        if (!Elevation::is_elevated()) Elevation::relaunch_elevated();
        Service::install();
        Log::shutdown();
        return 0;
    }
    if (args.action == CmdAction::UninstallService) {
        if (!Elevation::is_elevated()) Elevation::relaunch_elevated();
        Service::uninstall();
        Log::shutdown();
        return 0;
    }

    if (args.launch == LaunchMode::Service) {
        Service::run_service_main();
        return 0;
    }

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

    App app(args.launch);
    int rc = app.run();

    Log::shutdown();
    return rc;
}
