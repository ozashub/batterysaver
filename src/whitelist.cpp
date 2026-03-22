#include "whitelist.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <combaseapi.h>

#include <algorithm>
#include <cctype>

#pragma comment(lib, "ole32.lib")

static const wchar_t* s_system_list[] = {
    L"svchost.exe", L"services.exe", L"explorer.exe", L"csrss.exe",
    L"winlogon.exe", L"lsass.exe", L"audiodg.exe", L"dwm.exe",
    L"MsMpEng.exe", L"SearchHost.exe", L"ShellExperienceHost.exe",
    L"StartMenuExperienceHost.exe", L"RuntimeBroker.exe",
    L"taskhostw.exe", L"sihost.exe", L"fontdrvhost.exe",
    L"BatterySaver.exe",

    L"smss.exe", L"wininit.exe", L"conhost.exe", L"dllhost.exe",
    L"ctfmon.exe", L"TextInputHost.exe", L"SecurityHealthSystray.exe",
    L"SecurityHealthService.exe", L"spoolsv.exe", L"WmiPrvSE.exe",
    L"CompPkgSrv.exe", L"SystemSettings.exe", L"ApplicationFrameHost.exe",
    L"LockApp.exe", L"LogonUI.exe", L"SettingSyncHost.exe",
    L"SearchProtocolHost.exe", L"SearchFilterHost.exe",
    L"backgroundTaskHost.exe", L"smartscreen.exe",
    L"NisSrv.exe", L"MpDefenderCoreService.exe",
    L"WidgetService.exe", L"Widgets.exe",
    L"PhoneExperienceHost.exe", L"UserOOBEBroker.exe"
};

static bool iequals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (towlower(a[i]) != towlower(b[i]))
            return false;
    }
    return true;
}

static bool wildcard_match(std::wstring_view str, std::wstring_view pat) {
    size_t si = 0, pi = 0;
    size_t star_p = std::wstring_view::npos, star_s = 0;

    while (si < str.size()) {
        if (pi < pat.size() && (towlower(pat[pi]) == towlower(str[si]) || pat[pi] == L'?')) {
            ++si; ++pi;
        } else if (pi < pat.size() && pat[pi] == L'*') {
            star_p = pi++;
            star_s = si;
        } else if (star_p != std::wstring_view::npos) {
            pi = star_p + 1;
            si = ++star_s;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == L'*') ++pi;
    return pi == pat.size();
}

namespace Whitelist {

bool is_system_protected(std::wstring_view exe_name) {
    for (auto* sys : s_system_list) {
        if (iequals(exe_name, sys))
            return true;
    }
    return false;
}

bool is_critical_process(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return true; // can't open it — don't touch it
    BOOL critical = FALSE;
    // IsProcessCritical is Win8.1+, loaded dynamically for safety
    using FnType = BOOL(WINAPI*)(HANDLE, PBOOL);
    static auto fn = reinterpret_cast<FnType>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical"));
    if (fn) fn(h, &critical);
    CloseHandle(h);
    return critical != FALSE;
}

bool has_active_audio(unsigned long pid) {
    // WASAPI audio session check — skip processes actively producing sound
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) return false;

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || !device) {
        enumerator->Release();
        return false;
    }

    IAudioSessionManager2* mgr = nullptr;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(&mgr));
    device->Release();
    enumerator->Release();
    if (FAILED(hr) || !mgr) return false;

    IAudioSessionEnumerator* sessions = nullptr;
    hr = mgr->GetSessionEnumerator(&sessions);
    mgr->Release();
    if (FAILED(hr) || !sessions) return false;

    int count = 0;
    sessions->GetCount(&count);
    bool found = false;

    for (int i = 0; i < count && !found; ++i) {
        IAudioSessionControl* ctrl = nullptr;
        if (FAILED(sessions->GetSession(i, &ctrl)) || !ctrl) continue;

        IAudioSessionControl2* ctrl2 = nullptr;
        hr = ctrl->QueryInterface(__uuidof(IAudioSessionControl2),
            reinterpret_cast<void**>(&ctrl2));
        ctrl->Release();
        if (FAILED(hr) || !ctrl2) continue;

        DWORD session_pid = 0;
        ctrl2->GetProcessId(&session_pid);

        if (session_pid == pid) {
            AudioSessionState state;
            if (SUCCEEDED(ctrl2->GetState(&state)) && state == AudioSessionStateActive)
                found = true;
        }
        ctrl2->Release();
    }

    sessions->Release();
    return found;
}

bool matches_user_list(std::wstring_view exe_name, const std::vector<std::string>& patterns) {
    for (auto& pat : patterns) {
        std::wstring wpat(pat.begin(), pat.end());
        if (wildcard_match(exe_name, wpat))
            return true;
    }
    return false;
}

struct EnumCtx {
    unsigned long pid;
    bool found;
};

static BOOL CALLBACK enum_wnd(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    DWORD wnd_pid = 0;
    GetWindowThreadProcessId(hwnd, &wnd_pid);
    if (wnd_pid != ctx->pid)
        return TRUE;
    if (!IsWindowVisible(hwnd))
        return TRUE;
    // skip tiny or zero-size windows (toolbars, overlays)
    RECT r{};
    GetWindowRect(hwnd, &r);
    if ((r.right - r.left) < 50 || (r.bottom - r.top) < 50)
        return TRUE;
    ctx->found = true;
    return FALSE;
}

bool has_visible_window(unsigned long pid) {
    EnumCtx ctx{ pid, false };
    EnumWindows(enum_wnd, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

} // namespace Whitelist
