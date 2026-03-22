#include "console_log.h"

#include "platform.h"

#include <cstdio>
#include <chrono>
#include <mutex>

static FILE* s_out = nullptr;
static bool s_active = false;
static LogLevel s_min_level = LogLevel::Info;
static std::mutex s_mtx;

static const char* level_tag(LogLevel lvl) {
    switch (lvl) {
    case LogLevel::Debug: return "DBG";
    case LogLevel::Info:  return "INF";
    case LogLevel::Warn:  return "WRN";
    case LogLevel::Error: return "ERR";
    }
    return "???";
}

static void emit(LogLevel lvl, std::string_view msg) {
    if (lvl < s_min_level || !s_active)
        return;

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm local{};
    localtime_s(&local, &tt);

    std::lock_guard lock(s_mtx);
    std::fprintf(s_out, "[%02d:%02d:%02d.%03lld] [%s] %.*s\n",
        local.tm_hour, local.tm_min, local.tm_sec,
        static_cast<long long>(ms.count()),
        level_tag(lvl),
        static_cast<int>(msg.size()), msg.data());
    std::fflush(s_out);
}

namespace Log {

void init(bool console_visible) {
    if (console_visible) {
        if (GetConsoleWindow()) {
            s_out = stdout;
        } else {
            AllocConsole();
            freopen_s(&s_out, "CONOUT$", "w", stdout);
            if (!s_out)
                s_out = stdout;
        }
    } else {
        // tray mode — attach if a console exists, otherwise stay silent
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen_s(&s_out, "CONOUT$", "w", stdout);
            if (!s_out) s_out = stdout;
        } else {
            s_out = stdout;
            s_active = false;
            return;
        }
    }
    s_active = true;
}

void shutdown() {
    s_active = false;
}

void debug(std::string_view msg) { emit(LogLevel::Debug, msg); }
void info(std::string_view msg)  { emit(LogLevel::Info, msg); }
void warn(std::string_view msg)  { emit(LogLevel::Warn, msg); }
void error(std::string_view msg) { emit(LogLevel::Error, msg); }

static std::string narrow(std::wstring_view ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
        nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
        out.data(), len, nullptr, nullptr);
    return out;
}

void debug(std::wstring_view msg) { emit(LogLevel::Debug, narrow(msg)); }
void info(std::wstring_view msg)  { emit(LogLevel::Info, narrow(msg)); }
void warn(std::wstring_view msg)  { emit(LogLevel::Warn, narrow(msg)); }
void error(std::wstring_view msg) { emit(LogLevel::Error, narrow(msg)); }

void error(std::string_view msg, unsigned long win_error) {
    char buf[512];
    char err_msg[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, win_error, 0, err_msg, sizeof(err_msg), nullptr);
    // strip trailing newline from FormatMessage
    auto len = std::strlen(err_msg);
    while (len > 0 && (err_msg[len - 1] == '\n' || err_msg[len - 1] == '\r'))
        err_msg[--len] = '\0';
    std::snprintf(buf, sizeof(buf), "%.*s (0x%08lX: %s)",
        static_cast<int>(msg.size()), msg.data(), win_error, err_msg);
    emit(LogLevel::Error, buf);
}

void set_level(LogLevel level) {
    s_min_level = level;
}

} // namespace Log
