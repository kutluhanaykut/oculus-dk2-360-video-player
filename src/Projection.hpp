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

enum class FrameLayoutGuess { Undecided, Vr180SideBySide, Other };

// Looks at one decoded BGRA frame and tells VR180 side-by-side apart from
// other layouts: VR180 halves are circular-masked, so all eight corners
// (four per half) are black while the image centres are not. Returns
// Undecided for frames too dark to judge, e.g. a fade-in from black.
[[nodiscard]] FrameLayoutGuess classifyFrameLayout(
    const std::uint8_t* bgra, unsigned width, unsigned height, unsigned pitch);

} // namespace dk2vr
