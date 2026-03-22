#include "pch.h"
#include "elevation.h"
#include "console_log.h"

#include "platform.h"

namespace Elevation {

bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elev{};
    DWORD size = sizeof(elev);
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size);
    CloseHandle(token);

    return ok && elev.TokenIsElevated;
}

void relaunch_elevated() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = GetCommandLineW();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei))
        ExitProcess(0);

    Log::error("failed to relaunch elevated", GetLastError());
}

} // namespace Elevation
