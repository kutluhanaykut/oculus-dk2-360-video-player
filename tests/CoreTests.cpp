#include "BrowserIntegration.hpp"
#include "ImuPacket.hpp"
#include "OrientationFilter.hpp"
#include "Projection.hpp"
#include "SpatialMetadata.hpp"

#include <cstdint>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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

    // Kare yerlesimi: VR180, ust/alt, yan yana, EAC, mono, karanlik.
    {
        using dk2vr::FrameLayoutGuess;
        // Smooth, non-repeating value noise, so the halves of a mono frame
        // differ the way real footage does.
        const auto noise = [](const unsigned seed, const int x, const int y) {
            const auto lattice = [seed](const int cellX, const int cellY) {
                std::uint32_t hash = static_cast<std::uint32_t>(cellX) * 73856093U
                    ^ static_cast<std::uint32_t>(cellY) * 19349663U ^ seed * 83492791U;
                hash ^= hash >> 13;
                hash *= 0x5bd1e995U;
                hash ^= hash >> 15;
                return static_cast<float>(hash % 176U) + 40.0F;
            };
            const int cellX = static_cast<int>(std::floor(x / 16.0F));
            const int cellY = static_cast<int>(std::floor(y / 16.0F));
            const float fx = (x - cellX * 16) / 16.0F;
            const float fy = (y - cellY * 16) / 16.0F;
            const float top = lattice(cellX, cellY) * (1 - fx) + lattice(cellX + 1, cellY) * fx;
            const float bottom = lattice(cellX, cellY + 1) * (1 - fx) + lattice(cellX + 1, cellY + 1) * fx;
            return static_cast<std::uint8_t>(top * (1 - fy) + bottom * fy);
        };
        const auto classify = [](const unsigned width, const unsigned height, const auto& luma) {
            std::vector<std::uint8_t> frame(static_cast<std::size_t>(width) * height * 4U);
            for (unsigned y = 0; y < height; ++y) {
                for (unsigned x = 0; x < width; ++x) {
                    std::uint8_t* pixel = &frame[(static_cast<std::size_t>(y) * width + x) * 4U];
                    pixel[0] = pixel[1] = pixel[2] = luma(static_cast<int>(x), static_cast<int>(y));
                    pixel[3] = 255;
                }
            }
            return dk2vr::classifyFrameLayout(frame.data(), width, height, width * 4U);
        };

        require(classify(512, 256, [&](int x, int y) { return noise(1, x, y); })
                == FrameLayoutGuess::Monoscopic,
            "A continuous 2:1 picture must be mono");
        // Second eye = first eye with a little parallax.
        require(classify(256, 256, [&](int x, int y) {
            return y < 128 ? noise(1, x, y) : noise(1, x + 2, y - 128);
        }) == FrameLayoutGuess::TopBottom,
            "Nearly identical top and bottom halves must be top/bottom 3D");
        require(classify(1024, 256, [&](int x, int y) {
            return x < 512 ? noise(1, x, y) : noise(1, x - 512 + 2, y);
        }) == FrameLayoutGuess::SideBySide360,
            "Two 2:1 eyes side by side must be 360 side-by-side 3D");
        require(classify(512, 256, [&](int x, int y) {
            return x < 256 ? noise(1, x, y) : noise(1, x - 256 + 2, y);
        }) == FrameLayoutGuess::SideBySide180,
            "Two square eyes side by side must be VR180");
        require(classify(384, 256, [&](int x, int y) {
            return y < 128 ? noise(1, x, y) : noise(7, x, y);
        }) == FrameLayoutGuess::Cubemap,
            "Unrelated rows meeting at a hard middle seam must be an EAC grid");
        // Each half holds a lit disc on black, like a masked VR180 eye.
        require(classify(400, 200, [](int x, int y) {
            const float dx = static_cast<float>(x % 200) - 100.0F;
            const float dy = static_cast<float>(y) - 100.0F;
            return static_cast<std::uint8_t>(dx * dx + dy * dy < 95.0F * 95.0F ? 150 : 5);
        }) == FrameLayoutGuess::SideBySide180,
            "Masked side-by-side halves must be detected as VR180");
        require(classify(400, 200, [](int, int) { return std::uint8_t {2}; })
                == FrameLayoutGuess::Undecided,
            "A black frame must stay undecided");
        require(classify(400, 200, [](int, int) { return std::uint8_t {120}; })
                == FrameLayoutGuess::Undecided,
            "A flat frame must stay undecided");
    }

    // Dosya ici 360 bilgisi: MP4 sv3d/st3d, Matroska StereoMode/ProjectionType.
    {
        using dk2vr::SphericalProjection;
        using dk2vr::StereoLayout;
        // st3d (top-bottom) + sv3d/proj/equi with 0.25 cropped on each side.
        const std::uint8_t moov180[] {
            0, 0, 0, 13, 's', 't', '3', 'd', 0, 0, 0, 0, 1,
            0, 0, 0, 44, 's', 'v', '3', 'd',
            0, 0, 0, 36, 'p', 'r', 'o', 'j',
            0, 0, 0, 28, 'e', 'q', 'u', 'i', 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 0, 0x40, 0, 0, 0};
        const auto vr180 = dk2vr::parseMp4Moov(moov180, sizeof(moov180));
        require(vr180.projection == SphericalProjection::Equirectangular180
                && vr180.stereo == StereoLayout::TopBottom,
            "MP4 equi bounds and st3d must be read");
        const std::uint8_t moovCubemap[] {
            0, 0, 0, 36, 's', 'v', '3', 'd',
            0, 0, 0, 28, 'p', 'r', 'o', 'j',
            0, 0, 0, 20, 'c', 'b', 'm', 'p', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        require(dk2vr::parseMp4Moov(moovCubemap, sizeof(moovCubemap)).projection
                == SphericalProjection::Cubemap,
            "MP4 cbmp must be read as a cubemap");

        // StereoMode=1 and ProjectionType=1 before the first Cluster; a
        // look-alike inside the Cluster must be ignored.
        const std::uint8_t matroska[] {
            0x1A, 0x45, 0xDF, 0xA3, 0x84, 0x42, 0x86, 0x81, 0x01,
            0x53, 0xB8, 0x81, 0x01,
            0x76, 0x71, 0x81, 0x01,
            0x1F, 0x43, 0xB6, 0x75, 0x53, 0xB8, 0x81, 0x03};
        const auto mkv = dk2vr::parseMatroskaHeader(matroska, sizeof(matroska));
        require(mkv.stereo == StereoLayout::LeftRight
                && mkv.projection == SphericalProjection::Equirectangular,
            "Matroska StereoMode/ProjectionType must be read from the header only");

        const auto fromMetadata = [](SphericalProjection projection, StereoLayout stereo,
                                      unsigned width, unsigned height) {
            return dk2vr::projectionFromMetadata(dk2vr::SpatialMetadata {projection, stereo}, width, height);
        };
        require(fromMetadata(SphericalProjection::Unknown, StereoLayout::LeftRight, 1920, 1080)
                == ProjectionMode::Fisheye180Sbs,
            "Side-by-side with square eyes (YouTube VR180 WebM) must be VR180");
        require(fromMetadata(SphericalProjection::Equirectangular, StereoLayout::TopBottom, 4096, 4096)
                == ProjectionMode::StereoTopBottom,
            "Equirect top/bottom must be 3D 360 top/bottom");
        require(fromMetadata(SphericalProjection::Mesh, StereoLayout::Mono, 1920, 1080)
                == ProjectionMode::CubemapEac,
            "Mono mesh (YouTube 360) must be EAC");
        require(!fromMetadata(SphericalProjection::Unknown, StereoLayout::Mono, 1920, 1080).has_value(),
            "Mono without a projection must be left to the picture check");
    }

    // Tarayicidan gelen dk2vr: baglantilari.
    {
        const auto page = dk2vr::parseLaunchUrl(
            "dk2vr://open?url=https%3A%2F%2Fvimeo.com%2F123%3Fa%3D1%26b%3D2"
            "&video=https%3A%2F%2Fcdn.example.com%2Fclip%2520one.mp4");
        require(page && page->pageUrl == "https://vimeo.com/123?a=1&b=2"
                && page->videoUrl == "https://cdn.example.com/clip%20one.mp4",
            "dk2vr://open must decode the page and the video URL");
        const auto slashed = dk2vr::parseLaunchUrl("dk2vr://open/?url=https%3A%2F%2Fx.com%2Fv");
        require(slashed && slashed->pageUrl == "https://x.com/v",
            "A browser-added slash before '?' must be accepted");
        const auto blob = dk2vr::parseLaunchUrl(
            "dk2vr://open?url=https%3A%2F%2Fx.com%2Fv&video=blob%3Ahttps%3A%2F%2Fx.com%2Fabc");
        require(blob && blob->videoUrl.empty(), "blob: video sources must be dropped");
        require(dk2vr::parseLaunchUrl("dk2vr:https://x.com/a+b")->pageUrl == "https://x.com/a+b",
            "dk2vr:<url> must keep the URL, including '+'");
        require(dk2vr::parseLaunchUrl("https://www.youtube.com/watch?v=abc").has_value(),
            "A bare http(s) URL must be accepted");
        // A web page controls these links: nothing but http(s) may reach yt-dlp.
        require(!dk2vr::parseLaunchUrl("dk2vr://open?url=--exec%20calc").has_value(),
            "An option-looking 'URL' must be rejected");
        require(!dk2vr::parseLaunchUrl("dk2vr://open?url=file%3A%2F%2F%2FC%3A%2Fx.mp4").has_value(),
            "file: URLs must be rejected");
        require(!dk2vr::parseLaunchUrl("dk2vr://open?url=https%3A%2F%2Fx.com%2F%20--exec").has_value(),
            "URLs with spaces must be rejected");
        require(!dk2vr::parseLaunchUrl(R"(C:\Videos\clip.mp4)").has_value(),
            "Local paths are not launch URLs");
        require(dk2vr::bookmarkletUrl().rfind("javascript:", 0) == 0, "Bookmarklet must be a javascript: URL");

        // Cookies from the extension: only those for the stream's host,
        // path and scheme go into the Cookie header.
        const std::string jar =
            "# Netscape HTTP Cookie File\n"
            "#HttpOnly_.example.com\tTRUE\t/\tTRUE\t0\tsession\tabc\n"
            "cdn.example.com\tFALSE\t/media\tFALSE\t0\tcdn\t1\n"
            "other.com\tFALSE\t/\tFALSE\t0\tforeign\tx\n";
        require(dk2vr::cookieHeaderFor(jar, "https://cdn.example.com/media/a.mp4") == "session=abc; cdn=1",
            "Subdomain, host-only and path cookies must match");
        require(dk2vr::cookieHeaderFor(jar, "http://cdn.example.com/media/a.mp4") == "cdn=1",
            "Secure cookies must not go to http");
        require(dk2vr::cookieHeaderFor(jar, "https://notexample.com/").empty(),
            "A domain suffix that is not a subdomain must not match");
        const auto withCookies = dk2vr::parseLaunchUrl("dk2vr://open?url=https%3A%2F%2Fx.com%2Fv&cookies=a%09b");
        require(withCookies && withCookies->cookies == "a\tb", "The cookies parameter must be decoded");
        const auto withAgent = dk2vr::parseLaunchUrl(
            "dk2vr://open?url=https%3A%2F%2Fx.com%2Fv&ua=Mozilla%2F5.0%20(Windows%20NT%2010.0)");
        require(withAgent && withAgent->userAgent == "Mozilla/5.0 (Windows NT 10.0)",
            "The browser User-Agent must be passed on");
        const auto badAgent = dk2vr::parseLaunchUrl("dk2vr://open?url=https%3A%2F%2Fx.com%2Fv&ua=a%0D%0AX-Evil%3A%201");
        require(badAgent && badAgent->userAgent.empty(), "A User-Agent with line breaks must be dropped");
        const auto hinted = dk2vr::parseLaunchUrl("dk2vr://open?url=https%3A%2F%2Fx.com%2Fv&projection=STEREO_180_LR");
        require(hinted && hinted->projectionHint == "STEREO_180_LR", "The projection hint must be passed on");

        using dk2vr::projectionFromPlayerFormat;
        require(projectionFromPlayerFormat("STEREO_180_LR") == ProjectionMode::Fisheye180Sbs,
            "DL8 STEREO_180_LR must be VR180 side-by-side");
        require(projectionFromPlayerFormat("MONO_180") == ProjectionMode::Fisheye180, "DL8 MONO_180 must be 180 mono");
        require(projectionFromPlayerFormat("STEREO_360_TB") == ProjectionMode::StereoTopBottom,
            "DL8 STEREO_360_TB must be 360 top/bottom");
        require(projectionFromPlayerFormat("MONO_360") == ProjectionMode::Mono360, "DL8 MONO_360 must be mono 360");
        require(!projectionFromPlayerFormat("").has_value(), "No hint means no override");
    }

    // DK2 IMU paket cozucu.
    {
        std::uint8_t report[64] {};
        report[0] = 0x0B;
        report[3] = 2;
        report[8] = 0x40;
        report[9] = 0x42;
        report[10] = 0x0F; // 1000000 us
        for (std::size_t index = 12; index < 12 + 32; ++index) {
            report[index] = 0xFF; // every packed 21-bit value is -1
        }
        const dk2vr::ImuPacket packet = dk2vr::parseImuPacket(report, sizeof(report));
        require(packet.valid && packet.isDk2Format && packet.sampleCount == 2,
            "DK2 report with two samples must decode");
        require(packet.timestampMicros == 1000000U, "DK2 timestamp must be little-endian microseconds");
        require(packet.samples[1].gyro[2] == -1 && packet.samples[0].accel[0] == -1,
            "Packed 21-bit values must be sign-extended");
        require(dk2vr::parseImuPacket(report, 20).sampleCount == 0
                && !dk2vr::parseImuPacket(report, 20).valid,
            "Samples beyond a short transfer must not be read");
        report[0] = 0x02;
        require(!dk2vr::parseImuPacket(report, sizeof(report)).valid,
            "Non-IMU reports must be rejected");
    }

    // Yon filtresi: jiroskop entegrasyonu, yercekimi duzeltmesi, yalnizca yaw merkezleme.
    {
        using dk2vr::Vec3;
        dk2vr::OrientationFilter filter;
        for (int step = 0; step < 1000; ++step) {
            filter.integrateGyro(Vec3 {0.0, 1.0, 0.0}, 0.001);
        }
        require(std::abs(dk2vr::extractYaw(filter.orientation()) - 1.0) < 1e-3,
            "1 rad/s about +Y for 1 s must yield 1 rad of yaw");

        // Estimate tilted by 0.3 rad while the headset actually sits level:
        // gravity must pull the horizon back without changing yaw.
        filter.integrateGyro(Vec3 {0.3, 0.0, 0.0}, 1.0);
        for (int step = 0; step < 10000; ++step) {
            filter.applyGravity(Vec3 {0.0, 9.80665, 0.0}, 0.001);
        }
        const Vec3 up = dk2vr::rotate(filter.orientation(), Vec3 {0.0, 1.0, 0.0});
        require(up.y > 0.9999 && filter.hasGravityLock(), "Gravity must level the horizon");
        require(std::abs(dk2vr::extractYaw(filter.orientation()) - 1.0) < 1e-2,
            "Gravity correction must not change yaw");

        filter.integrateGyro(Vec3 {0.3, 0.0, 0.0}, 1.0);
        const double pitchBefore = dk2vr::rotate(filter.orientation(), Vec3 {0.0, 0.0, -1.0}).y;
        filter.recenter();
        const dk2vr::Quat centered = filter.orientation();
        require(std::abs(dk2vr::extractYaw(centered)) < 1e-6, "Recenter must cancel yaw");
        require(std::abs(dk2vr::rotate(centered, Vec3 {0.0, 0.0, -1.0}).y - pitchBefore) < 1e-6,
            "Recenter must keep pitch");
    }

    std::cout << "All DK2VR core tests passed.\n";
    return EXIT_SUCCESS;
}
