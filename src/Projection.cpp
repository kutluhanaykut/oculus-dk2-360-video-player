#include "Projection.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace dk2vr {
namespace {

// Lower-case alphanumeric words of a file name, e.g. "Trip_360_TB.mp4" ->
// {"trip", "360", "tb", "mp4"}.
std::vector<std::string> fileNameTokens(const std::string_view fileName)
{
    std::vector<std::string> tokens;
    std::string current;
    for (const char character : fileName) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            current.push_back(static_cast<char>(std::tolower(byte)));
        } else if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

bool hasAnyToken(const std::vector<std::string>& tokens,
    std::initializer_list<std::string_view> wanted)
{
    return std::any_of(tokens.begin(), tokens.end(), [&](const std::string& token) {
        return std::find(wanted.begin(), wanted.end(), token) != wanted.end();
    });
}

// Words joined without separators so "top-bottom" and "side_by_side" match.
bool hasJoinedPhrase(const std::vector<std::string>& tokens,
    std::initializer_list<std::string_view> wanted)
{
    std::string joined;
    for (const std::string& token : tokens) {
        joined += token;
    }
    return std::any_of(wanted.begin(), wanted.end(), [&](const std::string_view phrase) {
        return joined.find(phrase) != std::string::npos;
    });
}

} // namespace

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

FrameLayoutGuess classifyFrameLayout(
    const std::uint8_t* bgra, const unsigned width, const unsigned height, const unsigned pitch)
{
    if (bgra == nullptr || width < 64 || height < 64 || pitch < width * 4U) {
        return FrameLayoutGuess::Undecided;
    }
    const auto luma = [&](const unsigned x, const unsigned y) {
        const std::uint8_t* pixel = bgra + static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 4U;
        return 0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2];
    };
    // Mean luma of a small square patch centred on (centerX, centerY).
    const auto patchLuma = [&](const unsigned centerX, const unsigned centerY) {
        const unsigned half = (std::max)(2U, (std::min)(width, height) / 100U);
        const unsigned x0 = centerX > half ? centerX - half : 0U;
        const unsigned y0 = centerY > half ? centerY - half : 0U;
        const unsigned x1 = (std::min)(centerX + half, width - 1U);
        const unsigned y1 = (std::min)(centerY + half, height - 1U);
        double sum = 0.0;
        unsigned count = 0;
        for (unsigned y = y0; y <= y1; ++y) {
            for (unsigned x = x0; x <= x1; ++x) {
                sum += luma(x, y);
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    };

    // 1. VR180: each half is a circular-masked eye, so all eight corners are
    //    black while both image centres are lit.
    {
        constexpr double kBlack = 24.0;
        constexpr double kLit = 35.0;
        const unsigned inset = (std::max)(3U, (std::min)(width, height) / 50U);
        const unsigned halfWidth = width / 2U;
        int blackCorners = 0;
        int litCentres = 0;
        for (unsigned eye = 0; eye < 2; ++eye) {
            const unsigned left = eye * halfWidth;
            const unsigned right = left + halfWidth - 1U;
            for (const unsigned x : {left + inset, right - inset}) {
                for (const unsigned y : {inset, height - 1U - inset}) {
                    if (patchLuma(x, y) < kBlack) {
                        ++blackCorners;
                    }
                }
            }
            if (patchLuma(left + halfWidth / 2U, height / 2U) > kLit) {
                ++litCentres;
            }
        }
        if (litCentres == 2 && blackCorners == 8) {
            return FrameLayoutGuess::SideBySide180;
        }
    }

    // A coarse luma grid for the half-against-half comparisons.
    constexpr unsigned kGrid = 64;
    std::vector<double> grid(kGrid * kGrid);
    double sum = 0.0;
    for (unsigned row = 0; row < kGrid; ++row) {
        for (unsigned column = 0; column < kGrid; ++column) {
            const double value = luma(static_cast<unsigned>((column + 0.5) * width / kGrid),
                static_cast<unsigned>((row + 0.5) * height / kGrid));
            grid[row * kGrid + column] = value;
            sum += value;
        }
    }
    const double mean = sum / static_cast<double>(grid.size());
    double deviation = 0.0;
    for (const double value : grid) {
        deviation += std::abs(value - mean);
    }
    deviation /= static_cast<double>(grid.size());
    if (mean < 20.0 || deviation < 6.0) {
        return FrameLayoutGuess::Undecided; // black, faded or a flat title card
    }

    const auto at = [&](const unsigned column, const unsigned row) { return grid[row * kGrid + column]; };
    // Unrelated parts of the same frame are the yardstick for "different".
    double baseline = 0.0;
    double topBottom = 0.0;
    double leftRight = 0.0;
    for (unsigned row = 0; row < kGrid; ++row) {
        for (unsigned column = 0; column < kGrid; ++column) {
            baseline += std::abs(at(column, row) - at((column + kGrid / 3) % kGrid, (row + kGrid / 3) % kGrid));
            if (row < kGrid / 2) {
                topBottom += std::abs(at(column, row) - at(column, row + kGrid / 2));
            }
            if (column < kGrid / 2) {
                leftRight += std::abs(at(column, row) - at(column + kGrid / 2, row));
            }
        }
    }
    baseline /= static_cast<double>(kGrid * kGrid);
    if (baseline < 1.0) {
        return FrameLayoutGuess::Undecided;
    }
    const double halfCount = static_cast<double>(kGrid * kGrid / 2);
    const double topBottomRatio = topBottom / halfCount / baseline;
    const double leftRightRatio = leftRight / halfCount / baseline;

    // 2. Stereo: the two eye images are nearly identical (parallax only).
    constexpr double kSameImage = 0.3;
    if (topBottomRatio < kSameImage && topBottomRatio < 0.5 * leftRightRatio) {
        return FrameLayoutGuess::TopBottom;
    }
    if (leftRightRatio < kSameImage && leftRightRatio < 0.5 * topBottomRatio) {
        const double eyeAspect = static_cast<double>(width) / 2.0 / static_cast<double>(height);
        return eyeAspect > 0.8 && eyeAspect < 1.25 ? FrameLayoutGuess::SideBySide180
                                                   : FrameLayoutGuess::SideBySide360;
    }

    // 3. EAC: the 3x2 face grid puts unrelated faces on either side of the
    //    middle row, a hard seam that a continuous equirect image lacks.
    const auto rowDifference = [&](const unsigned y) {
        double total = 0.0;
        unsigned count = 0;
        for (unsigned x = 0; x < width; x += 4) {
            total += std::abs(luma(x, y) - luma(x, y + 1));
            ++count;
        }
        return total / count;
    };
    const unsigned middle = height / 2U - 1U;
    std::vector<double> neighbours;
    for (const int offset : {-6, -5, -4, -3, -2, 2, 3, 4, 5, 6}) {
        neighbours.push_back(rowDifference(static_cast<unsigned>(static_cast<int>(middle) + offset)));
    }
    std::nth_element(neighbours.begin(), neighbours.begin() + 5, neighbours.end());
    if (rowDifference(middle) > 6.0 * (neighbours[5] + 1.0)) {
        return FrameLayoutGuess::Cubemap;
    }
    return FrameLayoutGuess::Monoscopic;
}

std::optional<ProjectionMode> projectionFromFrameLayout(const FrameLayoutGuess guess)
{
    switch (guess) {
    case FrameLayoutGuess::Monoscopic:
        return ProjectionMode::Mono360;
    case FrameLayoutGuess::TopBottom:
        return ProjectionMode::StereoTopBottom;
    case FrameLayoutGuess::SideBySide360:
        return ProjectionMode::StereoLeftRight;
    case FrameLayoutGuess::SideBySide180:
        return ProjectionMode::Fisheye180Sbs;
    case FrameLayoutGuess::Cubemap:
        return ProjectionMode::CubemapEac;
    case FrameLayoutGuess::Undecided:
    default:
        return std::nullopt;
    }
}

std::optional<ProjectionMode> projectionFromFileNameTags(
    const std::string_view fileName, const unsigned width, const unsigned height)
{
    const float aspect = height > 0
        ? static_cast<float>(width) / static_cast<float>(height)
        : 0.0F;
    const bool squareAspect = aspect > 0.9F && aspect < 1.1F;

    const std::vector<std::string> tokens = fileNameTokens(fileName);
    const bool topBottom = hasAnyToken(tokens, {"tb", "ou", "3dv", "topbottom", "overunder"})
        || hasJoinedPhrase(tokens, {"topbottom", "overunder"});
    const bool sideBySide = hasAnyToken(tokens, {"lr", "sbs", "3dh", "sidebyside"})
        || hasJoinedPhrase(tokens, {"sidebyside", "leftright"});
    const bool half = hasAnyToken(tokens, {"180", "vr180"});
    const bool cubemap = hasAnyToken(tokens, {"eac", "cubemap"});

    if (cubemap) {
        return ProjectionMode::CubemapEac;
    }
    if (half) {
        // VR180 is almost always side-by-side; a square 180 frame is mono.
        if (sideBySide || (!topBottom && !squareAspect)) {
            return ProjectionMode::Fisheye180Sbs;
        }
        return ProjectionMode::Fisheye180;
    }
    if (topBottom) {
        return ProjectionMode::StereoTopBottom;
    }
    if (sideBySide) {
        return ProjectionMode::StereoLeftRight;
    }
    return std::nullopt;
}

ProjectionMode guessProjection(
    const std::string_view fileName, const unsigned width, const unsigned height)
{
    if (const auto tagged = projectionFromFileNameTags(fileName, width, height)) {
        return *tagged;
    }
    const float aspect = height > 0
        ? static_cast<float>(width) / static_cast<float>(height)
        : 0.0F;

    // No tags: two stacked 2:1 equirect images give a 1:1 frame, which is the
    // standard 360 3D top/bottom layout. 3:2 is the YouTube EAC grid. A 2:1
    // frame is ambiguous (360 mono or VR180 SBS); mono 360 is the safer bet.
    if (aspect > 0.9F && aspect < 1.1F) {
        return ProjectionMode::StereoTopBottom;
    }
    if (aspect > 1.45F && aspect < 1.55F) {
        return ProjectionMode::CubemapEac;
    }
    return ProjectionMode::Mono360;
}

std::optional<ProjectionMode> projectionFromMetadata(
    const SpatialMetadata& metadata, const unsigned width, const unsigned height)
{
    // Per-eye aspect of a side-by-side frame: ~1 for VR180, ~2 for 360.
    const double eyeAspect = height > 0 ? static_cast<double>(width) / 2.0 / height : 0.0;
    const bool squareEyes = eyeAspect > 0.8 && eyeAspect < 1.25;

    switch (metadata.projection) {
    case SphericalProjection::Cubemap:
        return ProjectionMode::CubemapEac;
    case SphericalProjection::Mesh:
        // YouTube's mesh: VR180 when side-by-side, otherwise its EAC cubemap.
        return metadata.stereo == StereoLayout::LeftRight && squareEyes
            ? ProjectionMode::Fisheye180Sbs
            : ProjectionMode::CubemapEac;
    case SphericalProjection::Equirectangular180:
        return metadata.stereo == StereoLayout::LeftRight ? ProjectionMode::Fisheye180Sbs
                                                          : ProjectionMode::Fisheye180;
    case SphericalProjection::Equirectangular:
    case SphericalProjection::Flat:
    case SphericalProjection::Unknown:
    default:
        break;
    }

    switch (metadata.stereo) {
    case StereoLayout::TopBottom:
        return ProjectionMode::StereoTopBottom;
    case StereoLayout::LeftRight:
        return squareEyes ? ProjectionMode::Fisheye180Sbs : ProjectionMode::StereoLeftRight;
    case StereoLayout::Mono:
    case StereoLayout::Unknown:
    default:
        break;
    }
    // Mono: equirect is settled only if the file said so; otherwise the
    // frames still have to tell an equirect image from an EAC grid.
    if (metadata.projection == SphericalProjection::Equirectangular
        || metadata.projection == SphericalProjection::Flat) {
        return ProjectionMode::Mono360;
    }
    return std::nullopt;
}

} // namespace dk2vr
