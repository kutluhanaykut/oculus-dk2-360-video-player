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
    case ProjectionMode::Mono360:
    default:
        return "Mono 360";
    }
}


} // namespace dk2vr
