#include "VideoPlayer.hpp"

#include "Logger.hpp"
#include "Process.hpp"
#include "VlcApi.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace dk2vr {
namespace {

std::string vlcError(const std::string& fallback)
{
    const char* message = libvlc_errmsg();
    return message != nullptr ? std::string(message) : fallback;
}

std::string findHeader(
    const std::map<std::string, std::string>& headers, const std::string& wantedName)
{
    for (const auto& [name, value] : headers) {
        if (_stricmp(name.c_str(), wantedName.c_str()) == 0) {
            return value;
        }
    }
    return {};
}

} // namespace

VideoPlayer::~VideoPlayer()
{
    shutdown();
}

bool VideoPlayer::initialize(const std::filesystem::path& pluginDirectory, std::string& error)
{
    if (instance_ != nullptr) {
        return true;
    }

    if (!pluginDirectory.empty()) {
        _wputenv_s(L"VLC_PLUGIN_PATH", pluginDirectory.wstring().c_str());
    }
    if (!vlcApi().load(pluginDirectory.parent_path(), error)) {
        return false;
    }

    const char* arguments[] {
        "--intf=dummy",
        "--no-video-title-show",
        "--no-osd",
        "--quiet",
        "--avcodec-hw=any",
    };
    instance_ = libvlc_new(static_cast<int>(std::size(arguments)), arguments);
    if (instance_ == nullptr) {
        error = vlcError("libVLC baslatilamadi.");
        vlcApi().unload();
        return false;
    }

    player_ = libvlc_media_player_new(instance_);
    if (player_ == nullptr) {
        error = vlcError("libVLC media player olusturulamadi.");
        libvlc_release(instance_);
        instance_ = nullptr;
        vlcApi().unload();
        return false;
    }

    libvlc_video_set_callbacks(player_, &VideoPlayer::lockVideo, &VideoPlayer::unlockVideo,
        &VideoPlayer::displayVideo, this);
    libvlc_video_set_format_callbacks(player_, &VideoPlayer::formatSetup, &VideoPlayer::formatCleanup);
    libvlc_audio_set_volume(player_, 100);
    log::info("libVLC video altyapisi baslatildi.");
    return true;
}

void VideoPlayer::shutdown()
{
    if (player_ != nullptr) {
        libvlc_media_player_stop(player_);
        libvlc_media_player_release(player_);
        player_ = nullptr;
    }
    // After VLC has dropped its connections, so the relays end promptly.
    rangeProxy_.stop();
    if (instance_ != nullptr) {
        libvlc_release(instance_);
        instance_ = nullptr;
    }
    vlcApi().unload();
    std::scoped_lock lock(frameMutex_);
    framePixels_.clear();
    frameWidth_ = 0;
    frameHeight_ = 0;
    framePitch_ = 0;
}

bool VideoPlayer::playFile(const std::filesystem::path& path, std::string& error)
{
    if (!initialized()) {
        error = "Video oynatici baslatilmadi.";
        return false;
    }
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError)) {
        error = "Video dosyasi bulunamadi.";
        return false;
    }

    const std::string utf8Path = wideToUtf8(path.wstring());
    libvlc_media_t* media = libvlc_media_new_path(instance_, utf8Path.c_str());
    if (media == nullptr) {
        error = vlcError("Yerel medya acilamadi.");
        return false;
    }
    libvlc_media_add_option(media, ":file-caching=300");
    const bool success = playMedia(media, error);
    libvlc_media_release(media);
    return success;
}

bool VideoPlayer::playNetwork(
    const std::string& videoUrl,
    const std::string& audioUrl,
    const std::map<std::string, std::string>& httpHeaders,
    const std::uint64_t httpChunkSize,
    std::string& error)
{
    if (!initialized()) {
        error = "Video oynatici baslatilmadi.";
        return false;
    }

    // Streams that only serve bounded ranges (YouTube) go through the local
    // proxy; VLC's own open-ended range request would get 403 Forbidden.
    std::string playVideoUrl = videoUrl;
    std::string playAudioUrl = audioUrl;
    if (httpChunkSize > 0) {
        if (!rangeProxy_.start(error)) {
            return false;
        }
        rangeProxy_.clear();
        playVideoUrl = rangeProxy_.serve(videoUrl, httpHeaders, httpChunkSize);
        if (!audioUrl.empty() && audioUrl != videoUrl) {
            playAudioUrl = rangeProxy_.serve(audioUrl, httpHeaders, httpChunkSize);
        }
    }

    libvlc_media_t* media = libvlc_media_new_location(instance_, playVideoUrl.c_str());
    if (media == nullptr) {
        error = vlcError("Ag medyasi acilamadi.");
        return false;
    }

    libvlc_media_add_option(media, ":network-caching=1500");
    libvlc_media_add_option(media, ":http-reconnect=true");
    const std::string userAgent = findHeader(httpHeaders, "User-Agent");
    const std::string referer = findHeader(httpHeaders, "Referer");
    if (!userAgent.empty()) {
        const std::string option = ":http-user-agent=" + userAgent;
        libvlc_media_add_option(media, option.c_str());
    }
    if (!referer.empty()) {
        const std::string option = ":http-referrer=" + referer;
        libvlc_media_add_option(media, option.c_str());
    }
    if (!audioUrl.empty() && audioUrl != videoUrl) {
        const int slaveResult = libvlc_media_slaves_add(
            media, libvlc_media_slave_type_audio, 0, playAudioUrl.c_str());
        if (slaveResult != 0) {
            log::warning("YouTube ses akisi media slave olarak eklenemedi.");
        }
    }

    const bool success = playMedia(media, error);
    libvlc_media_release(media);
    return success;
}

bool VideoPlayer::playMedia(void* mediaPointer, std::string& error)
{
    auto* media = static_cast<libvlc_media_t*>(mediaPointer);
    libvlc_media_player_set_media(player_, media);
    if (libvlc_media_player_play(player_) != 0) {
        error = vlcError("Video oynatma baslatilamadi.");
        return false;
    }
    return true;
}

void VideoPlayer::stop()
{
    if (player_ != nullptr) {
        libvlc_media_player_stop(player_);
    }
}

void VideoPlayer::togglePause()
{
    if (player_ == nullptr) {
        return;
    }
    const libvlc_state_t current = libvlc_media_player_get_state(player_);
    if (current == libvlc_Paused) {
        libvlc_media_player_set_pause(player_, 0);
    } else if (current == libvlc_Stopped || current == libvlc_Ended) {
        // Play again from the start after Stop or the end of the video.
        libvlc_media_player_play(player_);
    } else if (current == libvlc_Playing || current == libvlc_Buffering) {
        libvlc_media_player_set_pause(player_, 1);
    }
}

void VideoPlayer::seek(const std::int64_t milliseconds)
{
    if (player_ != nullptr && libvlc_media_player_is_seekable(player_) != 0) {
        libvlc_media_player_set_time(player_, std::max<std::int64_t>(milliseconds, 0));
    }
}

void VideoPlayer::setVolume(const int percent)
{
    if (player_ != nullptr) {
        libvlc_audio_set_volume(player_, std::clamp(percent, 0, 100));
    }
}

std::int64_t VideoPlayer::time() const
{
    return player_ != nullptr ? libvlc_media_player_get_time(player_) : 0;
}

std::int64_t VideoPlayer::duration() const
{
    return player_ != nullptr ? libvlc_media_player_get_length(player_) : 0;
}

int VideoPlayer::volume() const
{
    return player_ != nullptr ? libvlc_audio_get_volume(player_) : 0;
}

PlaybackState VideoPlayer::state() const
{
    if (player_ == nullptr) {
        return PlaybackState::Idle;
    }
    switch (libvlc_media_player_get_state(player_)) {
    case libvlc_Opening:
    case libvlc_Buffering:
        return PlaybackState::Opening;
    case libvlc_Playing:
        return PlaybackState::Playing;
    case libvlc_Paused:
        return PlaybackState::Paused;
    case libvlc_Stopped:
        return PlaybackState::Stopped;
    case libvlc_Ended:
        return PlaybackState::Ended;
    case libvlc_Error:
        return PlaybackState::Error;
    case libvlc_NothingSpecial:
    default:
        return PlaybackState::Idle;
    }
}

std::string VideoPlayer::stateText() const
{
    switch (state()) {
    case PlaybackState::Opening:
        return "Aciliyor / arabelleğe aliniyor";
    case PlaybackState::Playing:
        return "Oynatiliyor";
    case PlaybackState::Paused:
        return "Duraklatildi";
    case PlaybackState::Stopped:
        return "Durduruldu";
    case PlaybackState::Ended:
        return "Tamamlandi";
    case PlaybackState::Error:
        return "Oynatma hatasi";
    case PlaybackState::Idle:
    default:
        return "Hazir";
    }
}

bool VideoPlayer::initialized() const noexcept
{
    return instance_ != nullptr && player_ != nullptr;
}

bool VideoPlayer::consumeLatestFrame(
    const std::function<void(const std::uint8_t*, unsigned, unsigned, unsigned)>& consumer)
{
    // Ask libVLC for the visible size outside the frame lock: the decoder
    // thread holds that lock while it writes a picture.
    unsigned visibleWidth = visibleWidth_;
    unsigned visibleHeight = visibleHeight_;
    if ((visibleWidth == 0 || visibleHeight == 0) && player_ != nullptr) {
        unsigned width = 0;
        unsigned height = 0;
        if (libvlc_video_get_size(player_, 0, &width, &height) == 0 && width > 0 && height > 0) {
            visibleWidth_ = visibleWidth = width;
            visibleHeight_ = visibleHeight = height;
        }
    }

    std::scoped_lock lock(frameMutex_);
    if (producedFrame_ == consumedFrame_ || framePixels_.empty()) {
        return false;
    }
    // The picture sits in the top-left of the padded buffer; the unchanged
    // pitch lets the consumer skip the padding without copying.
    const unsigned width = visibleWidth > 0 && visibleWidth <= frameWidth_ ? visibleWidth : frameWidth_;
    const unsigned height = visibleHeight > 0 && visibleHeight <= frameHeight_ ? visibleHeight : frameHeight_;
    consumer(framePixels_.data(), width, height, framePitch_);
    consumedFrame_ = producedFrame_;
    return true;
}

unsigned VideoPlayer::formatSetup(void** opaque, char* chroma, unsigned* width, unsigned* height,
    unsigned* pitches, unsigned* lines)
{
    auto* self = static_cast<VideoPlayer*>(*opaque);
    if (self == nullptr || *width == 0 || *height == 0
        || *width > 16384 || *height > 16384) {
        return 0;
    }

    constexpr unsigned bytesPerPixel = 4;
    if (*width > std::numeric_limits<unsigned>::max() / bytesPerPixel) {
        return 0;
    }
    const unsigned pitch = *width * bytesPerPixel;
    const std::size_t byteCount = static_cast<std::size_t>(pitch) * *height;

    std::scoped_lock lock(self->frameMutex_);
    std::memcpy(chroma, "RV32", 4);
    pitches[0] = pitch;
    lines[0] = *height;
    self->frameWidth_ = *width;
    self->frameHeight_ = *height;
    self->framePitch_ = pitch;
    self->framePixels_.assign(byteCount, 0);
    self->producedFrame_ = 0;
    self->consumedFrame_ = 0;
    self->visibleWidth_ = 0;
    self->visibleHeight_ = 0;
    return 1;
}

void VideoPlayer::formatCleanup(void* /*opaque*/)
{
}

void* VideoPlayer::lockVideo(void* opaque, void** planes)
{
    auto* self = static_cast<VideoPlayer*>(opaque);
    self->frameMutex_.lock();
    *planes = self->framePixels_.data();
    return nullptr;
}

void VideoPlayer::unlockVideo(void* opaque, void* /*picture*/, void* const* /*planes*/)
{
    auto* self = static_cast<VideoPlayer*>(opaque);
    ++self->producedFrame_;
    self->frameMutex_.unlock();
}

void VideoPlayer::displayVideo(void* /*opaque*/, void* /*picture*/)
{
}

} // namespace dk2vr
