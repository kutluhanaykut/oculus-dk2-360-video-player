#include "Projection.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool approximatelyEqual(const float left, const float right)
{
    return std::abs(left - right) < 0.00001F;
}

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    using dk2vr::ProjectionMode;

    const glm::vec2 source(0.25F, 0.75F);
    const glm::vec2 mono = dk2vr::mapProjectionUv(source, ProjectionMode::Mono360, 0);
    require(approximatelyEqual(mono.x, 0.25F) && approximatelyEqual(mono.y, 0.75F),
        "Mono projection must preserve UV coordinates");

    const glm::vec2 top = dk2vr::mapProjectionUv(source, ProjectionMode::StereoTopBottom, 0);
    const glm::vec2 bottom = dk2vr::mapProjectionUv(source, ProjectionMode::StereoTopBottom, 1);
    require(approximatelyEqual(top.y, 0.375F), "Left eye must sample the top half");
    require(approximatelyEqual(bottom.y, 0.875F), "Right eye must sample the bottom half");

    const glm::vec2 left = dk2vr::mapProjectionUv(source, ProjectionMode::StereoLeftRight, 0);
    const glm::vec2 right = dk2vr::mapProjectionUv(source, ProjectionMode::StereoLeftRight, 1);
    require(approximatelyEqual(left.x, 0.125F), "Left eye must sample the left half");
    require(approximatelyEqual(right.x, 0.625F), "Right eye must sample the right half");

    // 180 derece mono: on yari kure (uv.x 0.25..0.75) 0..1 araligina olceklendirilir.
    const glm::vec2 fisheye = dk2vr::mapProjectionUv(source, ProjectionMode::Fisheye180, 0);
    require(approximatelyEqual(fisheye.x, 0.0F), "180 mono must map front hemisphere to 0..1");
    require(approximatelyEqual(fisheye.y, 0.75F), "180 mono must preserve vertical coordinate");

    // 180 derece SBS 3D: once on yari 0..1 araligina olceklendirilir, sonra goz secilir.
    const glm::vec2 fisheyeLeft = dk2vr::mapProjectionUv(source, ProjectionMode::Fisheye180Sbs, 0);
    const glm::vec2 fisheyeRight = dk2vr::mapProjectionUv(source, ProjectionMode::Fisheye180Sbs, 1);
    require(approximatelyEqual(fisheyeLeft.x, 0.0F), "180 SBS left eye must sample left half");
    require(approximatelyEqual(fisheyeRight.x, 0.5F), "180 SBS right eye must sample right half");

    require(dk2vr::projectionName(ProjectionMode::Mono360) == "Mono 360",
        "Projection label must remain stable");
    require(dk2vr::projectionName(ProjectionMode::Fisheye180) == "180 derece (mono)",
        "180 mono label must be correct");
    require(dk2vr::projectionName(ProjectionMode::Fisheye180Sbs) == "180 derece SBS 3D",
        "180 SBS label must be correct");

    // Otomatik projeksiyon tahmini: once dosya adi etiketleri, sonra en-boy orani.
    using dk2vr::guessProjection;
    require(guessProjection("trip.mp4", 3840, 1920) == ProjectionMode::Mono360,
        "Untagged 2:1 video must be mono 360");
    require(guessProjection("trip.mp4", 4096, 4096) == ProjectionMode::StereoTopBottom,
        "Untagged 1:1 video must be 360 top/bottom 3D");
    require(guessProjection("trip.mp4", 1536, 1024) == ProjectionMode::CubemapEac,
        "Untagged 3:2 video must be EAC cubemap");
    require(guessProjection("trip.mp4", 1280, 720) == ProjectionMode::Mono360,
        "Low resolution must not force a different mode");
    require(guessProjection("Concert_180_SBS.mp4", 5760, 2880) == ProjectionMode::Fisheye180Sbs,
        "_180_SBS tag must select 180 SBS");
    require(guessProjection("vr180 clip.mkv", 7680, 3840) == ProjectionMode::Fisheye180Sbs,
        "Untagged-layout VR180 2:1 must select 180 SBS");
    require(guessProjection("dome_180.mp4", 4096, 4096) == ProjectionMode::Fisheye180,
        "Square 180 video must be 180 mono");
    require(guessProjection("city_360_TB.mp4", 3840, 3840) == ProjectionMode::StereoTopBottom,
        "_TB tag must select top/bottom");
    require(guessProjection("city-top-bottom.mp4", 3840, 1920) == ProjectionMode::StereoTopBottom,
        "top-bottom phrase must select top/bottom");
    require(guessProjection("city_LR.mp4", 7680, 1920) == ProjectionMode::StereoLeftRight,
        "_LR tag must select side-by-side 360");
    require(guessProjection("yt_eac.webm", 3840, 2160) == ProjectionMode::CubemapEac,
        "_eac tag must select cubemap");
    require(guessProjection("1800p_trip.mp4", 3840, 1920) == ProjectionMode::Mono360,
        "Numbers that only contain 180 must not count as a 180 tag");

    std::cout << "All DK2VR core tests passed.\n";
    return EXIT_SUCCESS;
}
