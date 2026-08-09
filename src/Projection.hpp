#pragma once

#include <cstdint>
#include <string_view>

#include <glm/vec2.hpp>

namespace dk2vr {

enum class ProjectionMode : std::int32_t {
    Mono360 = 0,
    StereoTopBottom = 1,
    StereoLeftRight = 2,
};

[[nodiscard]] glm::vec2 mapProjectionUv(glm::vec2 uv, ProjectionMode mode, int eye);
[[nodiscard]] std::string_view projectionName(ProjectionMode mode) noexcept;

} // namespace dk2vr
