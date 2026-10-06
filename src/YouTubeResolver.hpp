#pragma once

#include <cstdint>
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
    // Size of the selected video stream, for projection guessing.
    unsigned videoWidth {0};
    unsigned videoHeight {0};
    // yt-dlp's downloader_options.http_chunk_size: the server only serves
    // bounded ranges, so playback must go through RangeProxy. 0 = not needed.
    std::uint64_t httpChunkSize {0};
    // Cookies yt-dlp says the video stream needs ("a=1; b=2"); empty if none.
    std::string videoCookieHeader;
    // yt-dlp's protocol for the video stream: "https", "m3u8_native", ...
    std::string videoProtocol;
    std::string error;
};


struct ResolveOptions {
    // Caps the video stream height (e.g. 1080); 0 means the default.
    int maxHeight {0};
    // Netscape cookies.txt handed to yt-dlp (--cookies); empty for none.
    std::filesystem::path cookiesFile;
    // The browser's User-Agent; empty keeps yt-dlp's own.
    std::string userAgent;
};

class YouTubeResolver {
public:
    explicit YouTubeResolver(std::filesystem::path executable);

    // maxHeight caps the video stream (e.g. 1080); 0 means the default.
    // For sites behind Cloudflare's bot check, a failed attempt is retried
    // once with yt-dlp impersonating a browser's TLS fingerprint.
    [[nodiscard]] YouTubeMedia resolve(const std::string& pageUrl, const ResolveOptions& options = {}) const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] const std::filesystem::path& executable() const noexcept;

private:
    std::filesystem::path executable_;
};

[[nodiscard]] bool isLikelyYouTubeUrl(const std::string& value);

// yt-dlp format selector for the highest stream at or below maxHeight that
// libVLC 3 can decode smoothly: VP9 first, then H.264, AV1 only as a last
// resort (libVLC 3 decodes AV1 in software).
[[nodiscard]] std::string youtubeFormatSelector(int maxHeight);

inline constexpr int kDefaultYouTubeMaxHeight = 1080;

} // namespace dk2vr
