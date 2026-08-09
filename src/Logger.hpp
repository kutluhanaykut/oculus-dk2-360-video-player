#pragma once

#include <filesystem>
#include <string_view>

namespace dk2vr::log {

void initialize(const std::filesystem::path& directory);
void shutdown();
void info(std::string_view message);
void warning(std::string_view message);
void error(std::string_view message);

} // namespace dk2vr::log
