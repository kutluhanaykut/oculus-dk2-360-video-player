#pragma once

#include "SpatialMetadata.hpp"

#include <cstdint>
#include <optional>
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

// Guesses the layout from the picture itself, for videos without metadata
// or file name tags:
//   - VR180: each half is circular-masked, so all eight corners are black;
//   - stereo: the two halves (top/bottom or left/right) are nearly the same;
//   - EAC: unrelated cube faces meet at a hard seam along the middle row.
// Undecided for frames too dark or flat to judge (fade-ins, title cards).
enum class FrameLayoutGuess {
    Undecided,
    Monoscopic,
    TopBottom,
    SideBySide360,
    SideBySide180,
    Cubemap,
};

[[nodiscard]] FrameLayoutGuess classifyFrameLayout(
    const std::uint8_t* bgra, unsigned width, unsigned height, unsigned pitch);
[[nodiscard]] std::optional<ProjectionMode> projectionFromFrameLayout(FrameLayoutGuess guess);

// Projection from file name tags alone; nullopt when the name has none.
[[nodiscard]] std::optional<ProjectionMode> projectionFromFileNameTags(
    std::string_view fileName, unsigned width, unsigned height);

// Projection from container metadata; nullopt when it does not settle it
// (mono with no projection: equirect and EAC still have to be told apart).
[[nodiscard]] std::optional<ProjectionMode> projectionFromMetadata(
    const SpatialMetadata& metadata, unsigned width, unsigned height);

} // namespace dk2vr
