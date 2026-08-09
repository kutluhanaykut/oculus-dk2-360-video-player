#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dk2vr {

struct ProcessResult {
    bool started {false};
    unsigned long exitCode {0};
    std::string output;
    std::string error;
};

[[nodiscard]] ProcessResult runProcess(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments);

[[nodiscard]] std::wstring utf8ToWide(const std::string& text);
[[nodiscard]] std::string wideToUtf8(const std::wstring& text);

} // namespace dk2vr
