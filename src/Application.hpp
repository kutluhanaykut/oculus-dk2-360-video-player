#pragma once

#include "DriverInstaller.hpp"
#include "HmdManager.hpp"
#include "Renderer.hpp"
#include "VideoPlayer.hpp"
#include "YouTubeHistory.hpp"
#include "YouTubeResolver.hpp"


#include <SDL.h>

#include <array>
#include <filesystem>
#include <future>
#include <string>

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
    int run();

private:
    void shutdown();
    void handleEvent(const SDL_Event& event);
    void updateAsyncResolution();
    void renderFrame();
    void renderUserInterface();
    void drawPlaybackControls();
    void drawSettings();
    void drawDevicePanel();
    void drawYouTubeHistory();
    void startYouTubeResolution();
    void playResolvedMedia(const YouTubeMedia& media);
    void playLocalFile(const std::filesystem::path& path);
    void playYouTubeUrl(const std::string& url);
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


    float mouseYaw_ {0.0F};
    float mousePitch_ {0.0F};
    bool mouseDragging_ {false};
    bool imguiInitialized_ {false};
    int volume_ {100};
};

} // namespace dk2vr
