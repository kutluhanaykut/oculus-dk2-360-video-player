#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct libvlc_instance_t;
struct libvlc_media_t;
struct libvlc_media_player_t;

using libvlc_time_t = std::int64_t;

enum libvlc_state_t {
    libvlc_NothingSpecial = 0,
    libvlc_Opening = 1,
    libvlc_Buffering = 2,
    libvlc_Playing = 3,
    libvlc_Paused = 4,
    libvlc_Stopped = 5,
    libvlc_Ended = 6,
    libvlc_Error = 7,
};

enum libvlc_media_slave_type_t {
    libvlc_media_slave_type_subtitle = 0,
    libvlc_media_slave_type_audio = 1,
};

using libvlc_video_lock_cb = void* (*)(void*, void**);
using libvlc_video_unlock_cb = void (*)(void*, void*, void* const*);
using libvlc_video_display_cb = void (*)(void*, void*);
using libvlc_video_format_cb = unsigned (*)(
    void**, char*, unsigned*, unsigned*, unsigned*, unsigned*);
using libvlc_video_cleanup_cb = void (*)(void*);

namespace dk2vr {

class VlcApi {
public:
    [[nodiscard]] bool load(const std::filesystem::path& libraryDirectory, std::string& error);
    void unload();
    [[nodiscard]] bool loaded() const noexcept;

    const char* (*errmsg)() {nullptr};
    libvlc_instance_t* (*create)(int, const char* const*) {nullptr};
    void (*release)(libvlc_instance_t*) {nullptr};
    libvlc_media_player_t* (*mediaPlayerNew)(libvlc_instance_t*) {nullptr};
    void (*mediaPlayerRelease)(libvlc_media_player_t*) {nullptr};
    void (*mediaPlayerStop)(libvlc_media_player_t*) {nullptr};
    int (*mediaPlayerPlay)(libvlc_media_player_t*) {nullptr};
    void (*mediaPlayerSetMedia)(libvlc_media_player_t*, libvlc_media_t*) {nullptr};
    libvlc_state_t (*mediaPlayerGetState)(libvlc_media_player_t*) {nullptr};
    void (*mediaPlayerSetPause)(libvlc_media_player_t*, int) {nullptr};
    int (*mediaPlayerIsSeekable)(libvlc_media_player_t*) {nullptr};
    void (*mediaPlayerSetTime)(libvlc_media_player_t*, libvlc_time_t) {nullptr};
    libvlc_time_t (*mediaPlayerGetTime)(libvlc_media_player_t*) {nullptr};
    libvlc_time_t (*mediaPlayerGetLength)(libvlc_media_player_t*) {nullptr};
    libvlc_media_t* (*mediaNewPath)(libvlc_instance_t*, const char*) {nullptr};
    libvlc_media_t* (*mediaNewLocation)(libvlc_instance_t*, const char*) {nullptr};
    void (*mediaRelease)(libvlc_media_t*) {nullptr};
    void (*mediaAddOption)(libvlc_media_t*, const char*) {nullptr};
    int (*mediaSlavesAdd)(libvlc_media_t*, libvlc_media_slave_type_t, unsigned, const char*) {nullptr};
    void (*videoSetCallbacks)(libvlc_media_player_t*, libvlc_video_lock_cb,
        libvlc_video_unlock_cb, libvlc_video_display_cb, void*) {nullptr};
    void (*videoSetFormatCallbacks)(libvlc_media_player_t*, libvlc_video_format_cb,
        libvlc_video_cleanup_cb) {nullptr};
    int (*audioSetVolume)(libvlc_media_player_t*, int) {nullptr};
    int (*audioGetVolume)(libvlc_media_player_t*) {nullptr};

private:
    void* module_ {nullptr};
};

VlcApi& vlcApi();

} // namespace dk2vr

// Small forwarding layer keeps VideoPlayer independent from VLC's binary SDK.
// The signatures mirror libVLC 3.x's stable C API and resolve at runtime.
inline const char* libvlc_errmsg() { return dk2vr::vlcApi().errmsg(); }
inline libvlc_instance_t* libvlc_new(int count, const char* const* arguments)
{
    return dk2vr::vlcApi().create(count, arguments);
}
inline void libvlc_release(libvlc_instance_t* instance) { dk2vr::vlcApi().release(instance); }
inline libvlc_media_player_t* libvlc_media_player_new(libvlc_instance_t* instance)
{
    return dk2vr::vlcApi().mediaPlayerNew(instance);
}
inline void libvlc_media_player_release(libvlc_media_player_t* player)
{
    dk2vr::vlcApi().mediaPlayerRelease(player);
}
inline void libvlc_media_player_stop(libvlc_media_player_t* player)
{
    dk2vr::vlcApi().mediaPlayerStop(player);
}
inline int libvlc_media_player_play(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().mediaPlayerPlay(player);
}
inline void libvlc_media_player_set_media(libvlc_media_player_t* player, libvlc_media_t* media)
{
    dk2vr::vlcApi().mediaPlayerSetMedia(player, media);
}
inline libvlc_state_t libvlc_media_player_get_state(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().mediaPlayerGetState(player);
}
inline void libvlc_media_player_set_pause(libvlc_media_player_t* player, int paused)
{
    dk2vr::vlcApi().mediaPlayerSetPause(player, paused);
}
inline int libvlc_media_player_is_seekable(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().mediaPlayerIsSeekable(player);
}
inline void libvlc_media_player_set_time(libvlc_media_player_t* player, libvlc_time_t time)
{
    dk2vr::vlcApi().mediaPlayerSetTime(player, time);
}
inline libvlc_time_t libvlc_media_player_get_time(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().mediaPlayerGetTime(player);
}
inline libvlc_time_t libvlc_media_player_get_length(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().mediaPlayerGetLength(player);
}
inline libvlc_media_t* libvlc_media_new_path(libvlc_instance_t* instance, const char* path)
{
    return dk2vr::vlcApi().mediaNewPath(instance, path);
}
inline libvlc_media_t* libvlc_media_new_location(libvlc_instance_t* instance, const char* url)
{
    return dk2vr::vlcApi().mediaNewLocation(instance, url);
}
inline void libvlc_media_release(libvlc_media_t* media) { dk2vr::vlcApi().mediaRelease(media); }
inline void libvlc_media_add_option(libvlc_media_t* media, const char* option)
{
    dk2vr::vlcApi().mediaAddOption(media, option);
}
inline int libvlc_media_slaves_add(libvlc_media_t* media, libvlc_media_slave_type_t type,
    unsigned priority, const char* url)
{
    return dk2vr::vlcApi().mediaSlavesAdd(media, type, priority, url);
}
inline void libvlc_video_set_callbacks(libvlc_media_player_t* player,
    libvlc_video_lock_cb lock, libvlc_video_unlock_cb unlock, libvlc_video_display_cb display,
    void* opaque)
{
    dk2vr::vlcApi().videoSetCallbacks(player, lock, unlock, display, opaque);
}
inline void libvlc_video_set_format_callbacks(libvlc_media_player_t* player,
    libvlc_video_format_cb setup, libvlc_video_cleanup_cb cleanup)
{
    dk2vr::vlcApi().videoSetFormatCallbacks(player, setup, cleanup);
}
inline int libvlc_audio_set_volume(libvlc_media_player_t* player, int volume)
{
    return dk2vr::vlcApi().audioSetVolume(player, volume);
}
inline int libvlc_audio_get_volume(libvlc_media_player_t* player)
{
    return dk2vr::vlcApi().audioGetVolume(player);
}
