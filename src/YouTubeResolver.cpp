#include "YouTubeResolver.hpp"

#include "Logger.hpp"
#include "Process.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <nlohmann/json.hpp>
#include <vector>

namespace dk2vr {
namespace {

using Json = nlohmann::json;

std::string lowerCase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void readHeaders(const Json& source, std::map<std::string, std::string>& destination)
{
    const auto iterator = source.find("http_headers");
    if (iterator == source.end() || !iterator->is_object()) {
        return;
    }
    for (const auto& [name, value] : iterator->items()) {
        if (value.is_string()) {
            destination.insert_or_assign(name, value.get<std::string>());
        }
    }
}

bool hasVideo(const Json& format)
{
    return format.value("vcodec", std::string("none")) != "none";
}

bool hasAudio(const Json& format)
{
    return format.value("acodec", std::string("none")) != "none";
}

void collectFormat(const Json& format, YouTubeMedia& media)
{
    const std::string url = format.value("url", std::string {});
    if (url.empty()) {
        return;
    }
    if (hasVideo(format) && media.videoUrl.empty()) {
        media.videoUrl = url;
        readHeaders(format, media.httpHeaders);
    }
    if (hasAudio(format) && !hasVideo(format) && media.audioUrl.empty()) {
        media.audioUrl = url;
    }
    if (hasVideo(format) && hasAudio(format) && media.videoUrl.empty()) {
        media.videoUrl = url;
    }
}

std::string trimForError(std::string output)
{
    constexpr std::size_t maximum = 1200;
    if (output.size() > maximum) {
        output.resize(maximum);
        output += "...";
    }
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n')) {
        output.pop_back();
    }
    return output;
}

} // namespace

YouTubeResolver::YouTubeResolver(std::filesystem::path executable)
    : executable_(std::move(executable))
{
}

YouTubeMedia YouTubeResolver::resolve(const std::string& pageUrl) const
{
    YouTubeMedia media;
    if (!available()) {
        media.error = "yt-dlp.exe bulunamadi: " + executable_.string();
        return media;
    }
    if (!isLikelyYouTubeUrl(pageUrl)) {
        media.error = "Gecerli bir YouTube video adresi girin.";
        return media;
    }

    const std::vector<std::wstring> arguments {
        L"--no-playlist",
        L"--no-warnings",
        L"--quiet",
        L"--dump-single-json",
        L"--format",
        L"bestvideo[height<=4320]+bestaudio/best[height<=4320]/best",
        utf8ToWide(pageUrl),
    };

    log::info("YouTube medya adresi yt-dlp ile cozuluyor.");
    const ProcessResult process = runProcess(executable_, arguments);
    if (!process.started) {
        media.error = "yt-dlp baslatilamadi: " + process.error;
        return media;
    }
    if (process.exitCode != 0) {
        media.error = "yt-dlp hatasi: " + trimForError(process.output);
        return media;
    }

    try {
        // stderr is intentionally merged into stdout by Process. Quiet mode should
        // emit JSON only, but selecting the outer braces makes parsing resilient.
        const std::size_t begin = process.output.find('{');
        const std::size_t end = process.output.rfind('}');
        if (begin == std::string::npos || end == std::string::npos || end < begin) {
            media.error = "yt-dlp gecerli JSON dondurmedi.";
            return media;
        }
        const Json root = Json::parse(process.output.substr(begin, end - begin + 1));
        media.title = root.value("title", std::string("YouTube 360 video"));
        media.projection = root.value("projection", std::string {});
        readHeaders(root, media.httpHeaders);
        if (!media.projection.empty()) {
            log::info("YouTube video projeksiyonu: " + media.projection);
        }

        // yt-dlp "projection" alanini VideoProjection turune cevir.
        // "cubemap" -> EAC (Equi-Angular Cubemap), "equirectangular" -> 360,
        // "mesh" -> spherical mesh, "flat" -> 2D.
        const std::string projectionLower = lowerCase(media.projection);
        if (projectionLower == "cubemap" || projectionLower == "eac"
            || projectionLower == "equiangularcubemap") {
            media.projectionType = VideoProjection::CubemapEac;
        } else if (projectionLower == "equirectangular") {
            media.projectionType = VideoProjection::Equirectangular;
        } else if (projectionLower == "mesh") {
            media.projectionType = VideoProjection::Mesh;
        } else if (projectionLower == "flat" || projectionLower == "2d") {
            media.projectionType = VideoProjection::Flat;
        } else {
            media.projectionType = VideoProjection::Unknown;
        }


        const auto formats = root.find("requested_formats");
        if (formats != root.end() && formats->is_array()) {
            for (const auto& format : *formats) {
                collectFormat(format, media);
            }
        }
        const auto downloads = root.find("requested_downloads");
        if (downloads != root.end() && downloads->is_array()) {
            for (const auto& format : *downloads) {
                collectFormat(format, media);
            }
        }
        if (media.videoUrl.empty()) {
            collectFormat(root, media);
        }

        if (media.videoUrl.empty()) {
            media.error = "YouTube video akis adresi bulunamadi.";
            return media;
        }
        media.success = true;
        log::info("YouTube medya adresi basariyla cozuldu.");
    } catch (const std::exception& exception) {
        media.error = std::string("yt-dlp JSON okunamadi: ") + exception.what();
    }
    return media;
}

bool YouTubeResolver::available() const
{
    std::error_code error;
    return std::filesystem::is_regular_file(executable_, error);
}

const std::filesystem::path& YouTubeResolver::executable() const noexcept
{
    return executable_;
}

bool isLikelyYouTubeUrl(const std::string& value)
{
    const std::string url = lowerCase(value);
    const bool validScheme = url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
    return validScheme && (url.find("youtube.com/") != std::string::npos
        || url.find("youtu.be/") != std::string::npos
        || url.find("youtube-nocookie.com/") != std::string::npos);
}

} // namespace dk2vr
