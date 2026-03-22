#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Whitelist {

bool is_system_protected(std::wstring_view exe_name);
bool is_critical_process(unsigned long pid);
bool has_active_audio(unsigned long pid);
bool matches_user_list(std::wstring_view exe_name, const std::vector<std::string>& patterns);
bool has_visible_window(unsigned long pid);

} // namespace Whitelist
