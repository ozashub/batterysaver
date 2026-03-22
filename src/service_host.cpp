#include "service_host.h"
#include "app.h"
#include "ipc_pipe.h"
#include "service_tray_bridge.h"
#include "console_log.h"

#include "platform.h"

#include <format>
#include <string>

static constexpr const wchar_t* SVC_NAME = L"BatterySaver";

static SERVICE_STATUS_HANDLE s_status_handle = nullptr;
static SERVICE_STATUS s_status{};
static App* s_svc_app = nullptr;
static ServiceTrayBridge* s_bridge = nullptr;

static void report_status(DWORD state, DWORD exit_code = 0, DWORD wait_hint = 0) {
    s_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_status.dwCurrentState = state;
    s_status.dwWin32ExitCode = exit_code;
    s_status.dwWaitHint = wait_hint;

    if (state == SERVICE_START_PENDING)
        s_status.dwControlsAccepted = 0;
    else
        s_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE;

    static DWORD checkpoint = 0;
    s_status.dwCheckPoint = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : ++checkpoint;

    SetServiceStatus(s_status_handle, &s_status);
}

static DWORD WINAPI svc_ctrl_handler(DWORD ctrl, DWORD event_type, void* event_data, void*) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
        report_status(SERVICE_STOP_PENDING);
        if (s_svc_app) s_svc_app->request_shutdown();
        return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE: {
        auto* info = static_cast<WTSSESSION_NOTIFICATION*>(event_data);
        if ((event_type == WTS_SESSION_LOGON || event_type == WTS_CONSOLE_CONNECT) && s_bridge) {
            s_bridge->spawn_for_session(info->dwSessionId);
        }
        return NO_ERROR;
    }

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI svc_main(DWORD, LPWSTR*) {
    s_status_handle = RegisterServiceCtrlHandlerExW(SVC_NAME, svc_ctrl_handler, nullptr);
    if (!s_status_handle) return;

    report_status(SERVICE_START_PENDING, 0, 3000);

    Log::init(false);
    Log::info("service starting");

    App app(LaunchMode::Service);
    s_svc_app = &app;

    ServiceTrayBridge bridge;
    s_bridge = &bridge;

    // spawn tray helper into the active console session
    DWORD session = WTSGetActiveConsoleSessionId();
    if (session != 0xFFFFFFFF)
        bridge.spawn_for_session(session);

    report_status(SERVICE_RUNNING);

    app.run();

    bridge.kill();
    s_bridge = nullptr;
    s_svc_app = nullptr;

    report_status(SERVICE_STOPPED);
    Log::info("service stopped");
    Log::shutdown();
}

bool Service::install() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    auto cmd = std::wstring(L"\"") + path + L"\" --service";

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        Log::error("OpenSCManager failed", GetLastError());
        return false;
    }

    SC_HANDLE svc = CreateServiceW(scm, SVC_NAME, L"BatterySaver",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        cmd.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        auto err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_EXISTS) {
            Log::info("service already installed");
            return true;
        }
        Log::error("CreateService failed", err);
        return false;
    }

    // delayed auto-start
    SERVICE_DELAYED_AUTO_START_INFO delay{ TRUE };
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delay);

    Log::info("service installed");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool Service::uninstall() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status{};
    ControlService(svc, SERVICE_CONTROL_STOP, &status);

    // wait briefly for stop
    for (int i = 0; i < 10; ++i) {
        QueryServiceStatus(svc, &status);
        if (status.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(500);
    }

    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (ok) Log::info("service uninstalled");
    else    Log::error("DeleteService failed", GetLastError());
    return ok != FALSE;
}

void Service::run_service_main() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<wchar_t*>(SVC_NAME), svc_main },
        { nullptr, nullptr }
    };
    StartServiceCtrlDispatcherW(table);
}
