#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace dk2vr {

// yt-dlp "projection" alanindan tespit edilen video projeksiyon turu.
enum class VideoProjection {
    Unknown,
    Equirectangular,
    CubemapEac,
    Mesh,
    Flat,
};

struct YouTubeMedia {
    bool success {false};
    std::string title;
    std::string videoUrl;
    std::string audioUrl;
    std::map<std::string, std::string> httpHeaders;
    // yt-dlp "projection" alani: "equirectangular" (360 derece),
    // "cubemap" (EAC), "mesh", "flat" (2D) veya bos.
    std::string projection;
    VideoProjection projectionType {VideoProjection::Unknown};
    std::string error;
};


class YouTubeResolver {
public:
    explicit YouTubeResolver(std::filesystem::path executable);

    [[nodiscard]] YouTubeMedia resolve(const std::string& pageUrl) const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::filesystem::path& executable() const noexcept;

private:
    std::filesystem::path executable_;
};

[[nodiscard]] bool isLikelyYouTubeUrl(const std::string& value);

} // namespace dk2vr
