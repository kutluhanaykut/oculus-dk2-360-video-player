#include "Projection.hpp"

#include <algorithm>
#include <cctype>
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
            const std::uint8_t* row = bgra + static_cast<std::size_t>(y) * pitch;
            for (unsigned x = x0; x <= x1; ++x) {
                const std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4U;
                sum += 0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2];
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    };

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

    if (litCentres < 2) {
        return FrameLayoutGuess::Undecided; // too dark to judge
    }
    return blackCorners == 8 ? FrameLayoutGuess::Vr180SideBySide : FrameLayoutGuess::Other;
}

ProjectionMode guessProjection(
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

    // No tags: two stacked 2:1 equirect images give a 1:1 frame, which is the
    // standard 360 3D top/bottom layout. 3:2 is the YouTube EAC grid. A 2:1
    // frame is ambiguous (360 mono or VR180 SBS); mono 360 is the safer bet.
    if (squareAspect) {
        return ProjectionMode::StereoTopBottom;
    }
    if (aspect > 1.45F && aspect < 1.55F) {
        return ProjectionMode::CubemapEac;
    }
    return ProjectionMode::Mono360;
}


} // namespace dk2vr
