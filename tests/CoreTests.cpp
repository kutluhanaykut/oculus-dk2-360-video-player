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

    require(dk2vr::projectionName(ProjectionMode::Mono360) == "Mono 360",
        "Projection label must remain stable");

    std::cout << "All DK2VR core tests passed.\n";
    return EXIT_SUCCESS;
}
