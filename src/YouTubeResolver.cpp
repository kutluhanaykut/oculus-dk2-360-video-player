#include "YouTubeResolver.hpp"

#include "BrowserIntegration.hpp"
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

// yt-dlp lists the cookies a format needs as Set-Cookie-like text:
// "name=value; Domain=.x.com; Path=/; Secure; Expires=...; name2=value2".
// Keep the name=value pairs, drop the attributes.
std::string cookieHeaderFromYtDlp(const std::string& cookies)
{
    static const char* const attributes[] {
        "domain", "path", "expires", "max-age", "secure", "httponly", "samesite", "version", "comment"};
    std::string header;
    std::size_t start = 0;
    while (start < cookies.size()) {
        std::size_t end = cookies.find(';', start);
        if (end == std::string::npos) {
            end = cookies.size();
        }
        std::string part = cookies.substr(start, end - start);
        start = end + 1;
        part.erase(0, part.find_first_not_of(' '));
        const std::size_t equals = part.find('=');
        const std::string key = lowerCase(part.substr(0, equals));
        if (part.empty() || equals == std::string::npos || equals == 0
            || std::find(std::begin(attributes), std::end(attributes), key) != std::end(attributes)) {
            continue;
        }
        if (!header.empty()) {
            header += "; ";
        }
        header += part;
    }
    return header;
}

// yt-dlp writes fields it does not know as null rather than leaving them
// out (YouTube happens to fill them all, other sites do not), and
// Json::value() throws on a null. Treat null like a missing field.
std::string stringField(const Json& object, const char* key, std::string fallback = {})
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : fallback;
}

unsigned unsignedField(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_number_unsigned() ? found->get<unsigned>() : 0U;
}

bool hasVideo(const Json& format)
{
    return stringField(format, "vcodec", "none") != "none";
}

bool hasAudio(const Json& format)
{
    return stringField(format, "acodec", "none") != "none";
}

void collectFormat(const Json& format, YouTubeMedia& media)
{
    const std::string url = stringField(format, "url");
    if (url.empty()) {
        return;
    }
    bool used = false;
    if (hasVideo(format) && media.videoUrl.empty()) {
        media.videoUrl = url;
        media.videoWidth = unsignedField(format, "width");
        media.videoHeight = unsignedField(format, "height");
        readHeaders(format, media.httpHeaders);
        media.videoProtocol = stringField(format, "protocol");
        media.videoCookieHeader = cookieHeaderFromYtDlp(stringField(format, "cookies"));
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

YouTubeMedia YouTubeResolver::resolve(const std::string& pageUrl, const ResolveOptions& options) const
{
    YouTubeMedia media;
    if (!available()) {
        media.error = "yt-dlp.exe bulunamadi: " + executable_.string();
        return media;
    }
    if (!isWebUrl(pageUrl)) {
        media.error = "Gecerli bir web video adresi girin (http:// veya https://).";
        return media;
    }

    const bool youtube = isLikelyYouTubeUrl(pageUrl);
    const auto buildArguments = [&](const bool impersonate) {
        std::vector<std::wstring> arguments {
            L"--no-playlist",
            L"--no-warnings",
            L"--quiet",
            L"--dump-single-json",
            L"--format",
            utf8ToWide(youtubeFormatSelector(options.maxHeight)),
        };
        if (!options.cookiesFile.empty()) {
            arguments.push_back(L"--cookies");
            arguments.push_back(options.cookiesFile.wstring());
        }
        if (!options.userAgent.empty()) {
            arguments.push_back(L"--user-agent");
            arguments.push_back(utf8ToWide(options.userAgent));
        }
        if (impersonate) {
            // Chrome's TLS fingerprint, for the generic extractor too.
            arguments.push_back(L"--impersonate");
            arguments.push_back(L"chrome");
            arguments.push_back(L"--extractor-args");
            arguments.push_back(L"generic:impersonate");
        }
        // End of options: the URL can come from a web page via dk2vr:
        // links, and must never be read as a yt-dlp option.
        arguments.push_back(L"--");
        arguments.push_back(utf8ToWide(pageUrl));
        return arguments;
    };

    log::info(std::string(youtube ? "YouTube" : "Web") + " medya adresi yt-dlp ile cozuluyor.");
    ProcessResult process = runProcess(executable_, buildArguments(false));
    // Cloudflare's bot check rejects yt-dlp's own TLS fingerprint; yt-dlp
    // says so and suggests impersonation. Not for YouTube, which works as is.
    if (process.started && process.exitCode != 0 && !youtube
        && (process.output.find("impersonat") != std::string::npos
            || process.output.find("Cloudflare") != std::string::npos)) {
        log::info("Site tarayici dogrulamasi istiyor; yt-dlp tarayici taklidiyle tekrar deniyor.");
        process = runProcess(executable_, buildArguments(true));
    }
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
        const Json document = Json::parse(process.output.substr(begin, end - begin + 1));
        // A page with several embedded videos comes back as a playlist; play
        // its first entry that has a stream.
        const Json* item = &document;
        if (stringField(document, "_type") == "playlist") {
            const auto entries = document.find("entries");
            if (entries != document.end() && entries->is_array()) {
                for (const Json& entry : *entries) {
                    if (entry.is_object() && (entry.contains("url") || entry.contains("requested_formats"))) {
                        item = &entry;
                        break;
                    }
                }
            }
        }
        const Json& root = *item;
        media.title = stringField(root, "title", stringField(document, "title", "Web video"));
        media.projection = stringField(root, "projection");
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
            media.error = "Bu sayfada oynatilabilir video bulunamadi.";
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
