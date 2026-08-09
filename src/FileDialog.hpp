#pragma once

#include <filesystem>
#include <optional>

namespace dk2vr {

[[nodiscard]] std::optional<std::filesystem::path> openVideoFileDialog();
[[nodiscard]] std::filesystem::path executableDirectory();

} // namespace dk2vr
