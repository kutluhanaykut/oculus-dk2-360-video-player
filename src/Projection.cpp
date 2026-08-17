#include "Projection.hpp"

namespace dk2vr {

glm::vec2 mapProjectionUv(glm::vec2 uv, const ProjectionMode mode, const int eye)
{
    const float eyeOffset = eye == 0 ? 0.0F : 0.5F;
    switch (mode) {
    case ProjectionMode::StereoTopBottom:
        uv.y = uv.y * 0.5F + eyeOffset;
        break;
    case ProjectionMode::StereoLeftRight:
        uv.x = uv.x * 0.5F + eyeOffset;
        break;
    case ProjectionMode::Fisheye180:
        // 180 derece mono: on yari kure (uv.x 0.25..0.75) 0..1 araligina olcekle.
        uv.x = (uv.x - 0.25F) * 2.0F;
        break;
    case ProjectionMode::Fisheye180Sbs:
        // 180 derece SBS 3D: once on yariyi 0..1 araligina olcekle, sonra goz sec.
        uv.x = (uv.x - 0.25F) * 2.0F;
        uv.x = uv.x * 0.5F + eyeOffset;
        break;
    case ProjectionMode::Mono360:
    case ProjectionMode::CubemapEac:
    default:
        break;
    }
    return uv;
}

std::string_view projectionName(const ProjectionMode mode) noexcept
{
    switch (mode) {
    case ProjectionMode::StereoTopBottom:
        return "3D 360 - ust/alt";
    case ProjectionMode::StereoLeftRight:
        return "3D 360 - yan yana";
    case ProjectionMode::CubemapEac:
        return "Cubemap (EAC)";
    case ProjectionMode::Fisheye180:
        return "180 derece (mono)";
    case ProjectionMode::Fisheye180Sbs:
        return "180 derece SBS 3D";
    case ProjectionMode::Mono360:
    default:
        return "Mono 360";
    }
}


} // namespace dk2vr
