#pragma once
#include <string_view>

enum class LogLevel { Debug, Info, Warn, Error };

namespace Log {

void init(bool console_visible);
void shutdown();

void debug(std::string_view msg);
void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);
void error(std::string_view msg, unsigned long win_error);

void debug(std::wstring_view msg);
void info(std::wstring_view msg);
void warn(std::wstring_view msg);
void error(std::wstring_view msg);

void set_level(LogLevel level);

} // namespace Log
