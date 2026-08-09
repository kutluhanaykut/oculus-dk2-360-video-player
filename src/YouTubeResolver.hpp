#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace dk2vr {

struct YouTubeMedia {
    bool success {false};
    std::string title;
    std::string videoUrl;
    std::string audioUrl;
    std::map<std::string, std::string> httpHeaders;
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
