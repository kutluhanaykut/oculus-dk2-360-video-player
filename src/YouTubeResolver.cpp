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
    bool used = false;
    if (hasVideo(format) && media.videoUrl.empty()) {
        media.videoUrl = url;
        media.videoWidth = format.value("width", 0U);
        media.videoHeight = format.value("height", 0U);
        readHeaders(format, media.httpHeaders);
        used = true;
    }
    if (hasAudio(format) && !hasVideo(format) && media.audioUrl.empty()) {
        media.audioUrl = url;
        used = true;
    }
    if (used) {
        const auto options = format.find("downloader_options");
        if (options != format.end() && options->is_object()) {
            const auto chunk = options->find("http_chunk_size");
            if (chunk != options->end() && chunk->is_number_unsigned()) {
                media.httpChunkSize = (std::max)(media.httpChunkSize, chunk->get<std::uint64_t>());
            }
        }
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

YouTubeMedia YouTubeResolver::resolve(const std::string& pageUrl, const int maxHeight) const
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
        utf8ToWide(youtubeFormatSelector(maxHeight)),
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

        // yt-dlp rarely fills "projection", but the stream URL carries
        // YouTube's own tag (xtags=vproj=mesh). The VR clients serve 360
        // videos as an EAC cubemap and VR180 as side-by-side halves, both
        // tagged "mesh"; Application tells the two apart.
        if (media.projectionType == VideoProjection::Unknown) {
            const std::string urlLower = lowerCase(media.videoUrl);
            if (urlLower.find("vproj%3dmesh") != std::string::npos) {
                media.projectionType = VideoProjection::Mesh;
            } else if (urlLower.find("vproj%3deac") != std::string::npos
                || urlLower.find("vproj%3dcubemap") != std::string::npos) {
                media.projectionType = VideoProjection::CubemapEac;
            } else if (urlLower.find("vproj%3drectangular") != std::string::npos
                || urlLower.find("vproj%3dequirectangular") != std::string::npos) {
                media.projectionType = VideoProjection::Equirectangular;
            }
            if (media.projectionType != VideoProjection::Unknown) {
                log::info("YouTube akis projeksiyon etiketi bulundu (vproj).");
            }
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

std::string youtubeFormatSelector(const int maxHeight)
{
    const std::string cap = "[height<=" + std::to_string(maxHeight > 0 ? maxHeight : kDefaultYouTubeMaxHeight) + "]";
    return "bestvideo" + cap + "[vcodec^=vp]+bestaudio"
        + "/bestvideo" + cap + "[vcodec^=avc1]+bestaudio"
        + "/bestvideo" + cap + "[vcodec!^=av01]+bestaudio"
        + "/bestvideo" + cap + "+bestaudio"
        + "/best" + cap + "/best";
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
