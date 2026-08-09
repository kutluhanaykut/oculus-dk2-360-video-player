#include "VlcApi.hpp"

#include "Process.hpp"

#include <Windows.h>

namespace dk2vr {
namespace {

std::string windowsLoadError(const DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    std::string message = "Windows hata kodu " + std::to_string(code);
    if (length != 0 && buffer != nullptr) {
        message += ": " + wideToUtf8(std::wstring(buffer, length));
        LocalFree(buffer);
    }
    return message;
}

} // namespace

bool VlcApi::load(const std::filesystem::path& libraryDirectory, std::string& error)
{
    if (loaded()) {
        return true;
    }
    const std::filesystem::path libraryPath = libraryDirectory / L"libvlc.dll";
    module_ = LoadLibraryExW(libraryPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (module_ == nullptr) {
        error = "libvlc.dll yuklenemedi (" + libraryPath.string() + "): "
            + windowsLoadError(GetLastError());
        return false;
    }

#define DK2VR_LOAD_VLC(member, exportName)                                                   \
    member = reinterpret_cast<decltype(member)>(                                             \
        GetProcAddress(static_cast<HMODULE>(module_), exportName));                           \
    if (member == nullptr) {                                                                  \
        error = std::string("libVLC exportu bulunamadi: ") + exportName;                    \
        unload();                                                                             \
        return false;                                                                         \
    }

    DK2VR_LOAD_VLC(errmsg, "libvlc_errmsg");
    DK2VR_LOAD_VLC(create, "libvlc_new");
    DK2VR_LOAD_VLC(release, "libvlc_release");
    DK2VR_LOAD_VLC(mediaPlayerNew, "libvlc_media_player_new");
    DK2VR_LOAD_VLC(mediaPlayerRelease, "libvlc_media_player_release");
    DK2VR_LOAD_VLC(mediaPlayerStop, "libvlc_media_player_stop");
    DK2VR_LOAD_VLC(mediaPlayerPlay, "libvlc_media_player_play");
    DK2VR_LOAD_VLC(mediaPlayerSetMedia, "libvlc_media_player_set_media");
    DK2VR_LOAD_VLC(mediaPlayerGetState, "libvlc_media_player_get_state");
    DK2VR_LOAD_VLC(mediaPlayerSetPause, "libvlc_media_player_set_pause");
    DK2VR_LOAD_VLC(mediaPlayerIsSeekable, "libvlc_media_player_is_seekable");
    DK2VR_LOAD_VLC(mediaPlayerSetTime, "libvlc_media_player_set_time");
    DK2VR_LOAD_VLC(mediaPlayerGetTime, "libvlc_media_player_get_time");
    DK2VR_LOAD_VLC(mediaPlayerGetLength, "libvlc_media_player_get_length");
    DK2VR_LOAD_VLC(mediaNewPath, "libvlc_media_new_path");
    DK2VR_LOAD_VLC(mediaNewLocation, "libvlc_media_new_location");
    DK2VR_LOAD_VLC(mediaRelease, "libvlc_media_release");
    DK2VR_LOAD_VLC(mediaAddOption, "libvlc_media_add_option");
    DK2VR_LOAD_VLC(mediaSlavesAdd, "libvlc_media_slaves_add");
    DK2VR_LOAD_VLC(videoSetCallbacks, "libvlc_video_set_callbacks");
    DK2VR_LOAD_VLC(videoSetFormatCallbacks, "libvlc_video_set_format_callbacks");
    DK2VR_LOAD_VLC(audioSetVolume, "libvlc_audio_set_volume");
    DK2VR_LOAD_VLC(audioGetVolume, "libvlc_audio_get_volume");
#undef DK2VR_LOAD_VLC

    return true;
}

void VlcApi::unload()
{
    if (module_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
    }
    errmsg = nullptr;
    create = nullptr;
    release = nullptr;
    mediaPlayerNew = nullptr;
    mediaPlayerRelease = nullptr;
    mediaPlayerStop = nullptr;
    mediaPlayerPlay = nullptr;
    mediaPlayerSetMedia = nullptr;
    mediaPlayerGetState = nullptr;
    mediaPlayerSetPause = nullptr;
    mediaPlayerIsSeekable = nullptr;
    mediaPlayerSetTime = nullptr;
    mediaPlayerGetTime = nullptr;
    mediaPlayerGetLength = nullptr;
    mediaNewPath = nullptr;
    mediaNewLocation = nullptr;
    mediaRelease = nullptr;
    mediaAddOption = nullptr;
    mediaSlavesAdd = nullptr;
    videoSetCallbacks = nullptr;
    videoSetFormatCallbacks = nullptr;
    audioSetVolume = nullptr;
    audioGetVolume = nullptr;
}

bool VlcApi::loaded() const noexcept
{
    return module_ != nullptr;
}

VlcApi& vlcApi()
{
    static VlcApi instance;
    return instance;
}

} // namespace dk2vr
