#pragma once

#include "DriverInstaller.hpp"
#include "HmdManager.hpp"
#include "BrowserIntegration.hpp"
#include "Process.hpp"
#include "SpatialMetadata.hpp"
#include "Renderer.hpp"
#include "VideoPlayer.hpp"
#include "YouTubeHistory.hpp"
#include "YouTubeResolver.hpp"


#include <SDL.h>

#include <array>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_version;

namespace dk2vr {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool initialize(std::string& error);
    // Plays a YouTube URL or a local video file given on the command line.
    void openFromCommandLine(const std::wstring& argument);
    int run();

private:
    void shutdown();
    void handleEvent(const SDL_Event& event);
    void updateAsyncResolution();
    void renderFrame();
    void renderUserInterface();
    void drawNowPlaying();
    void drawSourceTab();
    void drawYouTubeHistory();
    void drawSettings();
    void drawDevicePanel();
    void drawShortcutsTab();
    void drawStatusLine();
    void setVolume(int percent);
    void toggleMute();
    void changeYouTubeQuality(int maxHeight);
    void startYtDlpVersionQuery();
    void startYtDlpUpdate();
    void pollBackgroundTasks();
    void startLayoutCheck(bool youtubeMesh);
    void handleLayoutCheckFrame(const std::uint8_t* pixels, unsigned width, unsigned height,
        unsigned pitch);
    void loadSettings();
    void saveSettings() const;
    [[nodiscard]] std::filesystem::path settingsPath() const;
    void startYouTubeResolution();
    void playResolvedMedia(const YouTubeMedia& media);
    void playLocalFile(const std::filesystem::path& path);
    // Any http(s) page: resolved with yt-dlp; when that fails and the
    // browser supplied the page's direct <video> source, that is played.
    void playWebUrl(const std::string& url, const std::string& fallbackVideoUrl = {},
        const std::string& cookies = {}, const std::string& userAgent = {},
        const std::string& projectionHint = {});
    void playDirectWebVideo(const std::string& videoUrl, const std::string& pageUrl);
    void installArgumentReceiver();
    void processForwardedArguments();
    void drawBrowserIntegration();
    void enterVrMode();
    void leaveVrMode();
    void toggleVrMode();
    void updateMouseOrientation();
    [[nodiscard]] glm::quat viewOrientation() const;
    [[nodiscard]] std::filesystem::path vlcPluginDirectory() const;
    [[nodiscard]] std::filesystem::path ytDlpPath() const;
    [[nodiscard]] std::filesystem::path youtubeHistoryPath() const;
    void setStatus(std::string message);
    void setError(std::string message);


    SDL_Window* window_ {nullptr};
    SDL_GLContext glContext_ {nullptr};
    bool running_ {false};
    bool vrMode_ {false};
    int selectedDisplay_ {0};
    int windowedX_ {SDL_WINDOWPOS_CENTERED};
    int windowedY_ {SDL_WINDOWPOS_CENTERED};
    int windowedWidth_ {1280};
    int windowedHeight_ {720};

    HmdManager hmd_;
    DriverInstaller driverInstaller_;
    Renderer renderer_;
    VideoPlayer video_;
    YouTubeResolver resolver_;
    YouTubeHistory youtubeHistory_;
    RenderSettings renderSettings_;


    std::array<char, 4096> youtubeUrl_ {};
    std::filesystem::path selectedFile_;
    std::string currentTitle_;
    std::string currentYouTubeUrl_;
    std::string status_ {"Hazir. Bir 360 video dosyasi acin veya YouTube adresi girin."};
    std::string error_;
    std::future<YouTubeMedia> resolutionFuture_;
    bool resolving_ {false};
    bool autoProjectionPending_ {false};
    bool firstFrameLogged_ {false};

    // Interface state.
    bool uiVisible_ {true};
    bool playingYouTube_ {false};
    // Direct <video> source sent along with the page by the bookmarklet.
    std::string fallbackVideoUrl_;
    // Site cookies (Netscape format) from the browser extension. Kept in
    // memory only and never logged; handed to yt-dlp through a temporary
    // file that is deleted once the page is resolved.
    std::string pageCookies_;
    std::filesystem::path cookiesFile_;
    // The browser's User-Agent that came with those cookies.
    std::string pageUserAgent_;
    // Projection the page's player declared; beats any guess.
    std::string pageProjectionHint_;
    bool protocolRegistered_ {false};
    // Browser the bookmark page opens in; Brave first when installed.
    std::vector<InstalledBrowser> browsers_;
    std::string bookmarkBrowser_;
    // Projection detection from the picture (see classifyFrameLayout): frames
    // left to look at, every Nth one is classified and the first few clear
    // answers vote. For a YouTube "mesh" stream only "EAC or VR180" is asked.
    int layoutCheckFramesLeft_ {0};
    int layoutCheckCounter_ {0};
    bool layoutCheckYouTubeMesh_ {false};
    std::vector<FrameLayoutGuess> layoutVotes_;
    // Spherical metadata of the open local file.
    SpatialMetadata localMetadata_;
    bool timelineDragging_ {false};
    float timelineDragSeconds_ {0.0F};
    bool muted_ {false};
    int youtubeMaxHeight_ {kDefaultYouTubeMaxHeight};
    // Position to restore once a re-opened stream shows its first frame
    // (quality change); -1 when nothing is pending.
    std::int64_t pendingResumeMs_ {-1};
    std::array<char, 256> historyFilter_ {};

    // yt-dlp version query and self-update, run off the UI thread.
    std::string ytDlpVersion_;
    std::future<ProcessResult> ytDlpVersionFuture_;
    std::future<ProcessResult> ytDlpUpdateFuture_;


    float mouseYaw_ {0.0F};
    float mousePitch_ {0.0F};
    bool mouseDragging_ {false};
    bool imguiInitialized_ {false};
    int volume_ {100};
};

} // namespace dk2vr
