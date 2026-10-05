#pragma once

#include <cstdint>
#include <string_view>

#include <glm/vec2.hpp>

namespace dk2vr {

enum class ProjectionMode : std::int32_t {
    Mono360 = 0,
    StereoTopBottom = 1,
    StereoLeftRight = 2,
    CubemapEac = 3,
    Fisheye180 = 4,
    Fisheye180Sbs = 5,
};


[[nodiscard]] glm::vec2 mapProjectionUv(glm::vec2 uv, ProjectionMode mode, int eye);
[[nodiscard]] std::string_view projectionName(ProjectionMode mode) noexcept;

// Guesses the projection of a local video from common file name tags
// (_TB, _SBS, _LR, _180, _EAC ...) and falls back to the frame aspect ratio.
[[nodiscard]] ProjectionMode guessProjection(
    std::string_view fileName, unsigned width, unsigned height);

} // namespace dk2vr
