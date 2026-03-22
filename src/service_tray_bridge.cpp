#include "service_tray_bridge.h"
#include "console_log.h"

#include <WtsApi32.h>
#include <userenv.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

ServiceTrayBridge::ServiceTrayBridge() = default;

ServiceTrayBridge::~ServiceTrayBridge() { kill(); }

void ServiceTrayBridge::spawn_for_session(DWORD session_id) {
    if (alive_.load()) return;
    if (launch_into_session(session_id))
        alive_.store(true);
}

void ServiceTrayBridge::kill() {
    if (helper_process_) {
        TerminateProcess(helper_process_, 0);
        CloseHandle(helper_process_);
        helper_process_ = nullptr;
    }
    alive_.store(false);
}

bool ServiceTrayBridge::launch_into_session(DWORD session_id) {
    HANDLE token = nullptr;
    if (!WTSQueryUserToken(session_id, &token)) {
        Log::error("WTSQueryUserToken failed", GetLastError());
        return false;
    }

    HANDLE dup_token = nullptr;
    DuplicateTokenEx(token, MAXIMUM_ALLOWED, nullptr,
        SecurityIdentification, TokenPrimary, &dup_token);
    CloseHandle(token);
    if (!dup_token) {
        Log::error("DuplicateTokenEx failed", GetLastError());
        return false;
    }

    void* env = nullptr;
    CreateEnvironmentBlock(&env, dup_token, FALSE);

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    // the tray helper is the same binary, just launched without --service
    std::wstring cmd = std::wstring(L"\"") + path + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessAsUserW(dup_token, nullptr, cmd.data(),
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
        env, nullptr, &si, &pi);

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(dup_token);

    if (!ok) {
        Log::error("CreateProcessAsUser failed", GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    helper_process_ = pi.hProcess;
    Log::info("tray helper spawned into session");
    return true;
}
