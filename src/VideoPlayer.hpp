#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct libvlc_instance_t;
struct libvlc_media_player_t;

namespace dk2vr {

enum class PlaybackState {
    Idle,
    Opening,
    Playing,
    Paused,
    Stopped,
    Ended,
    Error,
};

class VideoPlayer {
public:
    VideoPlayer() = default;
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    [[nodiscard]] bool initialize(const std::filesystem::path& pluginDirectory, std::string& error);
    void shutdown();

    [[nodiscard]] bool playFile(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool playNetwork(
        const std::string& videoUrl,
        const std::string& audioUrl,
        const std::map<std::string, std::string>& httpHeaders,
        std::string& error);
    void stop();
    void togglePause();
    void seek(std::int64_t milliseconds);
    void setVolume(int percent);

    [[nodiscard]] std::int64_t time() const;
    [[nodiscard]] std::int64_t duration() const;
    [[nodiscard]] int volume() const;
    [[nodiscard]] PlaybackState state() const;
    [[nodiscard]] std::string stateText() const;
    [[nodiscard]] bool initialized() const noexcept;

    // The callback runs on the render thread while the decoder frame mutex is
    // held. It must consume the pixels immediately and must not retain them.
    bool consumeLatestFrame(
        const std::function<void(const std::uint8_t*, unsigned, unsigned, unsigned)>& consumer);

private:
    [[nodiscard]] bool playMedia(void* media, std::string& error);

    static unsigned formatSetup(void** opaque, char* chroma, unsigned* width, unsigned* height,
        unsigned* pitches, unsigned* lines);
    static void formatCleanup(void* opaque);
    static void* lockVideo(void* opaque, void** planes);
    static void unlockVideo(void* opaque, void* picture, void* const* planes);
    static void displayVideo(void* opaque, void* picture);

    libvlc_instance_t* instance_ {nullptr};
    libvlc_media_player_t* player_ {nullptr};

    mutable std::mutex frameMutex_;
    std::vector<std::uint8_t> framePixels_;
    unsigned frameWidth_ {0};
    unsigned frameHeight_ {0};
    unsigned framePitch_ {0};
    std::uint64_t producedFrame_ {0};
    std::uint64_t consumedFrame_ {0};
};

} // namespace dk2vr
