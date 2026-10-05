#include <GL/glew.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "Application.hpp"

#include "FileDialog.hpp"
#include "Logger.hpp"
#include "Process.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace dk2vr {
namespace {

std::string formatTime(const std::int64_t milliseconds)
{
    const std::int64_t totalSeconds = std::max<std::int64_t>(milliseconds, 0) / 1000;
    const std::int64_t hours = totalSeconds / 3600;
    const std::int64_t minutes = (totalSeconds % 3600) / 60;
    const std::int64_t seconds = totalSeconds % 60;
    char buffer[32] {};
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld",
            static_cast<long long>(hours), static_cast<long long>(minutes),
            static_cast<long long>(seconds));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld",
            static_cast<long long>(minutes), static_cast<long long>(seconds));
    }
    return buffer;
}

bool fileExists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool directoryExists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

// About ten seconds of video in which to spot a VR180 frame (fade-ins from
// black stay undecided until the picture appears).
constexpr int kLayoutCheckFrames = 600;

// YouTube quality choices: the maximum video height handed to yt-dlp.
constexpr int kQualityHeights[] {480, 720, 1080, 1440, 2160};
constexpr const char* kQualityNames[] {"480p", "720p", "1080p (varsayilan)", "1440p", "2160p (4K)"};

} // namespace

Application::Application()
    : resolver_(ytDlpPath())
    , youtubeHistory_(youtubeHistoryPath())
{
}


Application::~Application()
{
    shutdown();
}

bool Application::initialize(std::string& error)
{
    const std::filesystem::path executableDir = executableDirectory();
    log::initialize(executableDir);
    log::info("DK2 360 VR Player baslatiliyor.");
    youtubeHistory_.load();
    loadSettings();


    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        error = std::string("SDL baslatilamadi: ") + SDL_GetError();
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);

    window_ = SDL_CreateWindow(
        "DK2 360 VR Player",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowedWidth_,
        windowedHeight_,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window_ == nullptr) {
        error = std::string("Pencere olusturulamadi: ") + SDL_GetError();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (glContext_ == nullptr) {
        error = std::string("OpenGL context olusturulamadi: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);

    glewExperimental = GL_TRUE;
    const GLenum glewResult = glewInit();
    glGetError(); // GLEW may cause one harmless GL_INVALID_ENUM in core profile.
    if (glewResult != GLEW_OK) {
        error = "GLEW baslatilamadi: "
            + std::string(reinterpret_cast<const char*>(glewGetErrorString(glewResult)));
        return false;
    }
    log::info("OpenGL: " + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 9.0F;
    style.FrameRounding = 5.0F;
    style.GrabRounding = 5.0F;
    style.Colors[ImGuiCol_WindowBg].w = 0.94F;

    const std::filesystem::path fontPath = L"C:\\Windows\\Fonts\\segoeui.ttf";
    if (fileExists(fontPath)) {
        const std::string fontUtf8 = wideToUtf8(fontPath.wstring());
        (void)io.Fonts->AddFontFromFileTTF(fontUtf8.c_str(), 18.0F, nullptr,
            io.Fonts->GetGlyphRangesDefault());
    }
    if (!ImGui_ImplSDL2_InitForOpenGL(window_, glContext_)
        || !ImGui_ImplOpenGL3_Init("#version 330 core")) {
        error = "Dear ImGui arayuzu baslatilamadi.";
        return false;
    }
    imguiInitialized_ = true;

    if (!renderer_.initialize(error)) {
        return false;
    }
    if (!video_.initialize(vlcPluginDirectory(), error)) {
        return false;
    }
    video_.setVolume(volume_);
    startYtDlpVersionQuery();

    std::string hmdError;
    if (hmd_.initialize(hmdError)) {
        const HmdDisplayInfo& display = hmd_.displayInfo();
        renderSettings_.fovDegrees = display.fovDegrees;
        renderSettings_.screenWidthMeters = display.horizontalSizeMeters;
        renderSettings_.lensSeparationMeters = display.lensSeparationMeters;
        renderSettings_.ipdMeters = display.ipdMeters;
        bool distortionValid = false;
        for (std::size_t index = 0; index < display.distortion.size(); ++index) {
            const float value = display.distortion[index];
            if (std::isfinite(value) && std::abs(value) > 1e-4F) {
                distortionValid = true;
            }
            renderSettings_.distortion[index] = value;
        }
        if (!distortionValid) {
            // OpenHMD returned a zeroed array; fall back to well-known DK2 numbers.
            renderSettings_.distortion = {1.0F, 0.22F, 0.24F, 0.0F, 0.0F, 0.0F};
        }
        setStatus("DK2 baglandi. Dahili jiroskop ile yon takibi hazir.");
    } else {
        setStatus("DK2 bulunamadi; masaustu onizleme/fare kontrolu kullaniliyor.");
        error_.clear();
    }

    const int displayCount = (std::max)(SDL_GetNumVideoDisplays(), 1);

    int bestScore = -1;
    for (int displayIndex = 0; displayIndex < displayCount; ++displayIndex) {
        int score = displayIndex == 0 ? 0 : 1;
        const char* displayName = SDL_GetDisplayName(displayIndex);
        if (displayName != nullptr) {
            const std::string name(displayName);
            if (name.find("Rift") != std::string::npos || name.find("DK2") != std::string::npos) {
                score += 100;
            }
        }
        SDL_DisplayMode mode {};
        if (SDL_GetCurrentDisplayMode(displayIndex, &mode) == 0
            && ((mode.w == 1920 && mode.h == 1080) || (mode.w == 1080 && mode.h == 1920))) {
            score += 20;
            if (mode.refresh_rate >= 70) {
                score += 10;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            selectedDisplay_ = displayIndex;
        }
    }

    running_ = true;
    return true;
}

int Application::run()
{
    while (running_) {
        SDL_Event event {};
        while (SDL_PollEvent(&event) != 0) {
            handleEvent(event);
        }
        updateAsyncResolution();
        pollBackgroundTasks();
        hmd_.update();
        video_.consumeLatestFrame([this](const std::uint8_t* pixels, const unsigned width,
                                      const unsigned height, const unsigned pitch) {
            if (layoutCheckFramesLeft_ > 0) {
                --layoutCheckFramesLeft_;
                const FrameLayoutGuess guess = classifyFrameLayout(pixels, width, height, pitch);
                if (guess == FrameLayoutGuess::Vr180SideBySide) {
                    renderSettings_.projection = ProjectionMode::Fisheye180Sbs;
                    setStatus("Goruntu VR180 yan yana olarak algilandi; 180 derece SBS 3D secildi.");
                }
                if (guess != FrameLayoutGuess::Undecided) {
                    layoutCheckFramesLeft_ = 0;
                }
            }
            renderer_.uploadVideoFrame(pixels, width, height, pitch);
        });
        renderFrame();
    }
    return 0;
}

void Application::shutdown()
{
    if (imguiInitialized_) {
        saveSettings();
    }
    running_ = false;
    if (vrMode_) {
        leaveVrMode();
    }
    video_.shutdown();
    hmd_.shutdown();
    if (glContext_ != nullptr) {
        SDL_GL_MakeCurrent(window_, glContext_);
        renderer_.shutdown();
    }
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (glContext_ != nullptr) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (SDL_WasInit(0) != 0) {
        SDL_Quit();
    }
    log::shutdown();
}

void Application::handleEvent(const SDL_Event& event)
{
    if (imguiInitialized_) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
    if (event.type == SDL_QUIT) {
        running_ = false;
        return;
    }
    if (event.type == SDL_DROPFILE && event.drop.file != nullptr) {
        const std::filesystem::path path(utf8ToWide(event.drop.file));
        SDL_free(event.drop.file);
        playLocalFile(path);
        return;
    }
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
        running_ = false;
        return;
    }

    const bool keyboardCaptured = imguiInitialized_ && ImGui::GetIO().WantCaptureKeyboard;
    const bool textInputActive = imguiInitialized_ && ImGui::GetIO().WantTextInput;
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0
        && (vrMode_ || !keyboardCaptured || !textInputActive
            || event.key.keysym.sym == SDLK_ESCAPE
            || event.key.keysym.sym == SDLK_F11)) {

        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
            if (vrMode_) {
                leaveVrMode();
            } else {
                running_ = false;
            }
            break;
        case SDLK_F11:
            toggleVrMode();
            break;
        case SDLK_SPACE:
            video_.togglePause();
            break;
        case SDLK_r:
            hmd_.recenter();
            mouseYaw_ = 0.0F;
            mousePitch_ = 0.0F;
            setStatus("Bakis merkezi sifirlandi.");
            break;
        case SDLK_LEFT:
            video_.seek(video_.time() - 10000);
            break;
        case SDLK_RIGHT:
            video_.seek(video_.time() + 10000);
            break;
        case SDLK_UP:
            setVolume(volume_ + 5);
            break;
        case SDLK_DOWN:
            setVolume(volume_ - 5);
            break;
        case SDLK_m:
            toggleMute();
            break;
        case SDLK_h:
            uiVisible_ = !uiVisible_;
            break;

        case SDLK_1:
            renderSettings_.projection = ProjectionMode::Mono360;
            break;
        case SDLK_2:
            renderSettings_.projection = ProjectionMode::StereoTopBottom;
            break;
        case SDLK_3:
            renderSettings_.projection = ProjectionMode::StereoLeftRight;
            break;
        case SDLK_4:
            renderSettings_.projection = ProjectionMode::CubemapEac;
            break;
        case SDLK_5:
            renderSettings_.projection = ProjectionMode::Fisheye180;
            break;
        case SDLK_6:
            renderSettings_.projection = ProjectionMode::Fisheye180Sbs;
            break;
        case SDLK_d:

            renderSettings_.distortionEnabled = !renderSettings_.distortionEnabled;
            break;
        default:
            break;
        }
    }

    const bool mouseCaptured = imguiInitialized_ && ImGui::GetIO().WantCaptureMouse;
    if (!vrMode_ && !mouseCaptured && event.type == SDL_MOUSEBUTTONDOWN
        && event.button.button == SDL_BUTTON_RIGHT) {
        mouseDragging_ = true;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
        mouseDragging_ = false;
    }
    if (!vrMode_ && mouseDragging_ && event.type == SDL_MOUSEMOTION) {
        mouseYaw_ -= static_cast<float>(event.motion.xrel) * 0.004F;
        mousePitch_ -= static_cast<float>(event.motion.yrel) * 0.004F;
        mousePitch_ = std::clamp(mousePitch_, -1.45F, 1.45F);
    }
}

void Application::updateAsyncResolution()
{
    if (!resolving_ || !resolutionFuture_.valid()) {
        return;
    }
    if (resolutionFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }
    resolving_ = false;
    const YouTubeMedia media = resolutionFuture_.get();
    if (!media.success) {
        pendingResumeMs_ = -1;
        setError(media.error);
        return;
    }
    playResolvedMedia(media);
}

void Application::renderFrame()
{
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        SDL_Delay(10);
        return;
    }

    // Yerel dosya acildiginda ilk kare gelince projeksiyon modunu dosya adi
    // etiketlerine (_TB, _SBS, _180 ...) ve en-boy oranina gore otomatik sec.
    if (autoProjectionPending_ && renderer_.hasVideoFrame()) {
        autoProjectionPending_ = false;
        renderSettings_.projection = guessProjection(
            wideToUtf8(selectedFile_.filename().wstring()),
            renderer_.videoWidth(), renderer_.videoHeight());
        setStatus("Projeksiyon otomatik secildi: "
            + std::string(projectionName(renderSettings_.projection))
            + " (yanlissa 1-6 tuslari ile degistirin).");
    }

    if (!firstFrameLogged_ && renderer_.hasVideoFrame()) {
        firstFrameLogged_ = true;
        log::info("Ilk video karesi alindi: " + std::to_string(renderer_.videoWidth()) + "x"
            + std::to_string(renderer_.videoHeight()));
        if (pendingResumeMs_ > 0) {
            video_.seek(pendingResumeMs_);
        }
        pendingResumeMs_ = -1;
    }

    const glm::quat orientation = viewOrientation();
    if (vrMode_) {
        renderer_.renderVr(width, height, orientation, renderSettings_);
    } else {
        renderer_.renderPreview(width, height, orientation, renderSettings_);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (uiVisible_) {
            renderUserInterface();
        } else {
            ImGui::SetNextWindowPos(ImVec2(12.0F, static_cast<float>(height) - 12.0F),
                ImGuiCond_Always, ImVec2(0.0F, 1.0F));
            ImGui::SetNextWindowBgAlpha(0.35F);
            ImGui::Begin("##hiddenHint", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
                    | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextDisabled("H: arayuzu goster");
            ImGui::End();
        }
        ImGui::Render();
        glViewport(0, 0, width, height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    SDL_GL_SwapWindow(window_);
}

void Application::renderUserInterface()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + 16.0F, viewport->WorkPos.y + 16.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(580.0F, (std::min)(viewport->WorkSize.y - 32.0F, 820.0F)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(440.0F, 320.0F), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("DK2 360 VR Player")) {
        ImGui::End();
        return;
    }

    drawNowPlaying();
    ImGui::Separator();

    // The tabs scroll on their own; the status line stays pinned below them.
    const float statusHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0F
        + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("##tabs", ImVec2(0.0F, -statusHeight))) {
        if (ImGui::BeginTabBar("##mainTabs")) {
            if (ImGui::BeginTabItem("Kaynak")) {
                drawSourceTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Goruntu")) {
                drawSettings();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("DK2")) {
                drawDevicePanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Kisayollar")) {
                drawShortcutsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();

    drawStatusLine();
    ImGui::End();
}

void Application::drawNowPlaying()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92F, 0.95F, 1.0F, 1.0F));
    ImGui::TextWrapped("%s", currentTitle_.empty() ? "Medya acilmadi" : currentTitle_.c_str());
    ImGui::PopStyleColor();

    std::string info = video_.stateText();
    if (resolving_) {
        info = "YouTube cozuluyor...";
    }
    if (renderer_.hasVideoFrame()) {
        info += "  |  " + std::to_string(renderer_.videoWidth()) + "x"
            + std::to_string(renderer_.videoHeight());
    }
    info += "  |  " + std::string(projectionName(renderSettings_.projection));
    if (playingYouTube_) {
        info += "  |  YouTube <= " + std::to_string(youtubeMaxHeight_) + "p";
    }
    ImGui::TextDisabled("%s", info.c_str());

    // Timeline: shows the dragged position while held and seeks once on
    // release, instead of flooding the decoder with a seek per frame.
    const std::int64_t duration = video_.duration();
    const float durationSeconds = static_cast<float>((std::max)(duration, std::int64_t {1})) / 1000.0F;
    float seconds = timelineDragging_ ? timelineDragSeconds_
                                      : static_cast<float>(video_.time()) / 1000.0F;
    const std::string overlay = formatTime(static_cast<std::int64_t>(seconds * 1000.0F)) + " / "
        + formatTime(duration);
    ImGui::BeginDisabled(duration <= 0);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("##timeline", &seconds, 0.0F, durationSeconds, overlay.c_str(),
        ImGuiSliderFlags_NoInput);
    if (ImGui::IsItemActive()) {
        timelineDragging_ = true;
        timelineDragSeconds_ = seconds;
    }
    if (ImGui::IsItemDeactivated() && timelineDragging_) {
        video_.seek(static_cast<std::int64_t>(timelineDragSeconds_ * 1000.0F));
        timelineDragging_ = false;
    }
    ImGui::EndDisabled();

    if (ImGui::Button("-10 sn")) {
        video_.seek(video_.time() - 10000);
    }
    ImGui::SameLine();
    const bool playing = video_.state() == PlaybackState::Playing;
    if (ImGui::Button(playing ? "Duraklat" : "Oynat", ImVec2(84.0F, 0.0F))) {
        video_.togglePause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Durdur")) {
        video_.stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("+10 sn")) {
        video_.seek(video_.time() + 10000);
    }
    ImGui::SameLine();
    if (ImGui::Button(muted_ ? "Sesi ac" : "Sessiz", ImVec2(64.0F, 0.0F))) {
        toggleMute();
    }
    ImGui::SameLine();
    int volume = muted_ ? 0 : volume_;
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "Ses %d%%")) {
        setVolume(volume);
    }

    if (ImGui::Button("DK2'de VR tam ekran (F11)")) {
        enterVrMode();
    }
    ImGui::SameLine();
    if (ImGui::Button("Bakisi merkezle (R)")) {
        hmd_.recenter();
        mouseYaw_ = 0.0F;
        mousePitch_ = 0.0F;
    }
    ImGui::SameLine();
    if (ImGui::Button("Arayuzu gizle (H)")) {
        uiVisible_ = false;
    }
}

void Application::drawSourceTab()
{
    if (ImGui::Button("Yerel video ac...")) {
        if (const auto path = openVideoFileDialog()) {
            playLocalFile(*path);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("veya dosyayi pencereye surukleyin");

    ImGui::Spacing();
    ImGui::SeparatorText("YouTube");

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pasteWidth = ImGui::CalcTextSize("Yapistir").x + style.FramePadding.x * 2.0F;
    const float playWidth = 96.0F;
    ImGui::SetNextItemWidth(-(pasteWidth + playWidth + style.ItemSpacing.x * 2.0F));
    const bool enterPressed = ImGui::InputTextWithHint("##youtube",
        "https://www.youtube.com/watch?v=...  (Enter ile oynat)", youtubeUrl_.data(),
        youtubeUrl_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Yapistir")) {
        if (const char* clipboard = ImGui::GetClipboardText()) {
            std::string text(clipboard);
            const auto notSpace = [](const unsigned char character) { return std::isspace(character) == 0; };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
            text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
            std::memset(youtubeUrl_.data(), 0, youtubeUrl_.size());
            std::strncpy(youtubeUrl_.data(), text.c_str(), youtubeUrl_.size() - 1);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(resolving_);
    if (ImGui::Button(resolving_ ? "Cozuluyor..." : "Oynat", ImVec2(playWidth, 0.0F)) || enterPressed) {
        startYouTubeResolution();
    }
    ImGui::EndDisabled();

    int qualityIndex = 2;
    for (int index = 0; index < static_cast<int>(std::size(kQualityHeights)); ++index) {
        if (kQualityHeights[index] == youtubeMaxHeight_) {
            qualityIndex = index;
        }
    }
    ImGui::SetNextItemWidth(200.0F);
    if (ImGui::Combo("Kalite", &qualityIndex, kQualityNames, static_cast<int>(std::size(kQualityNames)))) {
        changeYouTubeQuality(kQualityHeights[qualityIndex]);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Secilen yukseklige kadar en iyi VP9 / H.264 akisi secilir.\n"
                          "Oynayan bir YouTube videosunda degistirirsen ayni yerden\n"
                          "yeni kalitede devam eder. 1440p ve 4K takilabilir.");
    }

    const bool updating = ytDlpUpdateFuture_.valid();
    ImGui::TextDisabled("yt-dlp: %s", !resolver_.available() ? "bulunamadi"
            : ytDlpVersion_.empty()                          ? "hazir"
                                                             : ytDlpVersion_.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(updating || !resolver_.available());
    if (ImGui::SmallButton(updating ? "Guncelleniyor..." : "Guncelle")) {
        startYtDlpUpdate();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("YouTube videolari acilmazsa (or. HTTP 403) once yt-dlp'yi guncelleyin.");
    }

    ImGui::Spacing();
    drawYouTubeHistory();
}

void Application::drawYouTubeHistory()
{
    ImGui::SeparatorText("Gecmis");
    if (youtubeHistory_.empty()) {
        ImGui::TextDisabled("Henuz YouTube videosu acilmadi.");
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float clearWidth = ImGui::CalcTextSize("Tumunu temizle").x + style.FramePadding.x * 2.0F;
    ImGui::SetNextItemWidth(-(clearWidth + style.ItemSpacing.x));
    ImGui::InputTextWithHint("##historyFilter", "Gecmiste ara...", historyFilter_.data(),
        historyFilter_.size());
    ImGui::SameLine();
    if (ImGui::Button("Tumunu temizle")) {
        ImGui::OpenPopup("Gecmisi temizle");
    }
    if (ImGui::BeginPopupModal("Gecmisi temizle", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%zu kaydin tamami silinsin mi?", youtubeHistory_.size());
        if (ImGui::Button("Sil", ImVec2(100.0F, 0.0F))) {
            youtubeHistory_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Vazgec", ImVec2(100.0F, 0.0F))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Actions are collected and applied after the loop: removing an entry
    // while iterating the list would invalidate it.
    std::string playUrl;
    std::string removeUrl;
    const std::string filter = lowerAscii(historyFilter_.data());
    const float tableHeight = (std::max)(ImGui::GetContentRegionAvail().y, 140.0F);
    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##history", 2, tableFlags, ImVec2(0.0F, tableHeight))) {
        const float removeWidth = ImGui::CalcTextSize("Sil").x + style.FramePadding.x * 2.0F;
        ImGui::TableSetupColumn("Video", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, removeWidth);
        const auto& entries = youtubeHistory_.entries();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const YouTubeHistoryEntry& entry = entries[index];
            const std::string label = entry.title.empty() ? entry.url : entry.title;
            if (!filter.empty() && lowerAscii(label).find(filter) == std::string::npos
                && lowerAscii(entry.url).find(filter) == std::string::npos) {
                continue;
            }
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool current = playingYouTube_ && entry.url == currentYouTubeUrl_;
            if (ImGui::Selectable(label.c_str(), current)) {
                playUrl = entry.url;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nTiklayinca oynatilir.", entry.url.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            if (ImGui::SmallButton("Sil")) {
                removeUrl = entry.url;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!removeUrl.empty()) {
        youtubeHistory_.remove(removeUrl);
    }
    if (!playUrl.empty()) {
        playYouTubeUrl(playUrl);
    }
}

void Application::drawSettings()
{
    int projection = static_cast<int>(renderSettings_.projection);
    constexpr const char* projectionNames[] {
        "Mono 360  (1)", "3D 360 ust/alt  (2)", "3D 360 yan yana  (3)", "Cubemap (EAC)  (4)",
        "180 derece mono  (5)", "180 derece SBS 3D  (6)"};
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::Combo("Projeksiyon", &projection, projectionNames,
            static_cast<int>(std::size(projectionNames)))) {
        renderSettings_.projection = static_cast<ProjectionMode>(projection);
    }

    int previewMode = static_cast<int>(renderSettings_.previewMode);
    constexpr const char* previewModeNames[] {"Tek goz (monitor)", "Side-by-side 3D", "Anaglif kirmizi/cyan"};
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::Combo("Onizleme modu", &previewMode, previewModeNames,
            static_cast<int>(std::size(previewModeNames)))) {
        renderSettings_.previewMode = static_cast<PreviewMode>(previewMode);
    }
    ImGui::SetNextItemWidth(240.0F);
    ImGui::SliderFloat("Gorus alani", &renderSettings_.fovDegrees, 70.0F, 125.0F, "%.1f derece");
    ImGui::Checkbox("Videoyu dikey cevir", &renderSettings_.flipVertical);
    ImGui::Checkbox("DK2 lens distorsiyon duzeltmesi (D)", &renderSettings_.distortionEnabled);

    if (ImGui::TreeNode("Lens ayarlari (gelismis)")) {
        ImGui::BeginDisabled(!renderSettings_.distortionEnabled);
        ImGui::SetNextItemWidth(200.0F);
        ImGui::SliderFloat("K0 (olcek)", &renderSettings_.distortion[0], 0.0F, 2.0F, "%.3f");
        ImGui::SetNextItemWidth(200.0F);
        ImGui::SliderFloat("K1 (r2)", &renderSettings_.distortion[1], 0.0F, 1.0F, "%.3f");
        ImGui::SetNextItemWidth(200.0F);
        ImGui::SliderFloat("K2 (r4)", &renderSettings_.distortion[2], 0.0F, 1.0F, "%.3f");
        ImGui::SetNextItemWidth(200.0F);
        ImGui::SliderFloat("K3 (r6)", &renderSettings_.distortion[3], -0.2F, 0.2F, "%.4f");
        ImGui::SetNextItemWidth(200.0F);
        ImGui::SliderFloat("Renk sapmasi", &renderSettings_.chromaticAberration, 0.0F, 0.03F, "%.4f");
        ImGui::EndDisabled();
        if (ImGui::Button("DK2 varsayilan lens ayarlari")) {
            renderSettings_.fovDegrees = 100.0F;
            renderSettings_.distortion = {1.0F, 0.22F, 0.24F, 0.0F, 0.0F, 0.0F};
            renderSettings_.chromaticAberration = 0.008F;
            renderSettings_.screenWidthMeters = 0.12576F;
            renderSettings_.lensSeparationMeters = 0.0635F;
            renderSettings_.ipdMeters = 0.064F;
        }
        ImGui::TreePop();
    }
}

void Application::drawDevicePanel()
{
    if (hmd_.connected()) {
        const HmdDeviceInfo* device = hmd_.activeDevice();
        const HmdDisplayInfo& display = hmd_.displayInfo();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38F, 0.95F, 0.55F, 1.0F));
        ImGui::Text("Jiroskop: BAGLI (%s)", hmd_.activeBackend().c_str());
        ImGui::PopStyleColor();
        if (device != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", device->vendor.c_str(), device->product.c_str());
        }
        ImGui::TextDisabled("Panel: %d x %d, FOV %.1f", display.horizontalResolution,
            display.verticalResolution, display.fovDegrees);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.62F, 0.20F, 1.0F));
        ImGui::Text("Jiroskop: DK2 BULUNAMADI");
        ImGui::PopStyleColor();
        if (!hmd_.lastError().empty()) {
            ImGui::TextWrapped("%s", hmd_.lastError().c_str());
        }
    }
    if (ImGui::Button("DK2'yi yeniden tara")) {
        std::string hmdError;
        if (hmd_.initialize(hmdError)) {
            setStatus("DK2 ve dahili jiroskop baglandi.");
        } else {
            setError(hmdError);
        }
    }

    ImGui::SeparatorText("Ekran");
    const int displayCount = (std::max)(SDL_GetNumVideoDisplays(), 1);
    selectedDisplay_ = std::clamp(selectedDisplay_, 0, displayCount - 1);
    const char* selectedName = SDL_GetDisplayName(selectedDisplay_);
    const std::string preview = selectedName != nullptr
        ? std::to_string(selectedDisplay_ + 1) + ": " + selectedName
        : "Ekran " + std::to_string(selectedDisplay_ + 1);
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("DK2 HDMI ekrani").x - ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::BeginCombo("DK2 HDMI ekrani", preview.c_str())) {
        for (int index = 0; index < displayCount; ++index) {
            SDL_DisplayMode mode {};
            SDL_GetCurrentDisplayMode(index, &mode);
            const char* name = SDL_GetDisplayName(index);
            const std::string label = std::to_string(index + 1) + ": "
                + (name != nullptr ? name : "Ekran") + " - "
                + std::to_string(mode.w) + "x" + std::to_string(mode.h) + " @ "
                + std::to_string(mode.refresh_rate) + "Hz";
            if (ImGui::Selectable(label.c_str(), selectedDisplay_ == index)) {
                selectedDisplay_ = index;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("DK2 ekraninda VR tam ekran (F11)", ImVec2(-1.0F, 34.0F))) {
        enterVrMode();
    }

    if (ImGui::TreeNode("Sorun giderme")) {
        ImGui::TextWrapped("DK2, Windows'ta genisletilmis ekran olarak 1920x1080 yatay ve 75 Hz "
                           "ayarlanmalidir. Konum kamerasi kullanilmaz; yalnizca gozlukteki "
                           "IMU/jiroskop okunur.");
        ImGui::TextWrapped("Jiroskop bulunamazsa: USB kablosunu kontrol edin; Aygit Yoneticisi'nde "
                           "'Rift DK2' (2833:0021) WinUSB surucusunde olmali (Zadig). 2833:2021 "
                           "DK2'nin USB hub'idir, onun surucusunu degistirmeyin. Ayrintilar "
                           "DK2VRPlayer.log dosyasinda.");
        ImGui::TextWrapped("HDMI ekrani gorunmuyorsa 'Onizleme modu'nu 'Side-by-side 3D' veya "
                           "'Anaglif' yapip ana monitorden izleyebilirsiniz.");
        if (!hmd_.connected()) {
            if (ImGui::Button("DK2 WinUSB surucusunu kur (yonetici)")) {
                std::string driverError;
                if (driverInstaller_.installDk2WinUsbDriver(driverError)) {
                    setStatus(driverInstaller_.lastMessage());
                } else {
                    setError(driverError);
                }
            }
        }
        ImGui::TreePop();
    }
}

void Application::drawShortcutsTab()
{
    constexpr const char* shortcuts[][2] {
        {"F11", "DK2'de stereo tam ekran / pencere"},
        {"Esc", "VR modundan cik / uygulamayi kapat"},
        {"Space", "Oynat / duraklat"},
        {"Sol / Sag ok", "10 saniye geri / ileri"},
        {"Yukari / Asagi ok", "Ses +5 / -5"},
        {"M", "Sessiz"},
        {"R", "Bakisi merkezle"},
        {"H", "Arayuzu gizle / goster"},
        {"1 - 6", "Projeksiyon: Mono 360, 3D ust/alt, 3D yan yana, EAC, 180, 180 SBS"},
        {"D", "Lens distorsiyonu ac / kapa"},
        {"Sag tik + surukle", "Onizlemede etrafa bak (DK2 yokken)"},
    };
    if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Tus", ImGuiTableColumnFlags_WidthFixed, 140.0F);
        ImGui::TableSetupColumn("Islev", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& shortcut : shortcuts) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(shortcut[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", shortcut[1]);
        }
        ImGui::EndTable();
    }
}

void Application::drawStatusLine()
{
    ImGui::Separator();
    if (!error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.38F, 0.32F, 1.0F));
        ImGui::TextWrapped("Hata: %s", error_.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45F, 0.88F, 0.65F, 1.0F));
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
}

void Application::setVolume(const int percent)
{
    volume_ = std::clamp(percent, 0, 100);
    muted_ = false;
    video_.setVolume(volume_);
}

void Application::toggleMute()
{
    muted_ = !muted_;
    video_.setVolume(muted_ ? 0 : volume_);
}

void Application::changeYouTubeQuality(const int maxHeight)
{
    youtubeMaxHeight_ = maxHeight;
    saveSettings();
    if (!playingYouTube_ || currentYouTubeUrl_.empty()) {
        setStatus("YouTube kalitesi: en fazla " + std::to_string(maxHeight) + "p.");
        return;
    }
    // Re-open the current video at the new quality and continue from here.
    const PlaybackState state = video_.state();
    pendingResumeMs_ = state == PlaybackState::Playing || state == PlaybackState::Paused
        ? video_.time()
        : -1;
    playYouTubeUrl(currentYouTubeUrl_);
}

void Application::startYtDlpVersionQuery()
{
    if (!resolver_.available()) {
        return;
    }
    const std::filesystem::path executable = ytDlpPath();
    ytDlpVersionFuture_ = std::async(std::launch::async, [executable] {
        return runProcess(executable, {L"--version"});
    });
}

void Application::startYtDlpUpdate()
{
    if (ytDlpUpdateFuture_.valid() || !resolver_.available()) {
        return;
    }
    setStatus("yt-dlp guncelleniyor...");
    const std::filesystem::path executable = ytDlpPath();
    ytDlpUpdateFuture_ = std::async(std::launch::async, [executable] {
        return runProcess(executable, {L"-U"});
    });
}

void Application::pollBackgroundTasks()
{
    const auto ready = [](const std::future<ProcessResult>& future) {
        return future.valid()
            && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };
    const auto lastLine = [](std::string text) {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
        const std::size_t newline = text.find_last_of("\r\n");
        return newline == std::string::npos ? text : text.substr(newline + 1);
    };

    if (ready(ytDlpVersionFuture_)) {
        const ProcessResult result = ytDlpVersionFuture_.get();
        if (result.started && result.exitCode == 0) {
            ytDlpVersion_ = lastLine(result.output);
        }
    }
    if (ready(ytDlpUpdateFuture_)) {
        const ProcessResult result = ytDlpUpdateFuture_.get();
        if (!result.started) {
            setError("yt-dlp guncellemesi baslatilamadi: " + result.error);
        } else if (result.exitCode != 0) {
            setError("yt-dlp guncellenemedi: " + lastLine(result.output));
        } else {
            setStatus("yt-dlp: " + lastLine(result.output));
        }
        startYtDlpVersionQuery();
    }
}

std::filesystem::path Application::settingsPath() const
{
    return executableDirectory() / L"settings.json";
}

void Application::loadSettings()
{
    try {
        std::ifstream stream(settingsPath(), std::ios::binary);
        if (!stream) {
            return;
        }
        const nlohmann::json root = nlohmann::json::parse(stream, nullptr, false);
        if (!root.is_object()) {
            return;
        }
        const int height = root.value("youtubeMaxHeight", kDefaultYouTubeMaxHeight);
        if (std::find(std::begin(kQualityHeights), std::end(kQualityHeights), height)
            != std::end(kQualityHeights)) {
            youtubeMaxHeight_ = height;
        }
        volume_ = std::clamp(root.value("volume", volume_), 0, 100);
        const int previewMode = root.value("previewMode", static_cast<int>(renderSettings_.previewMode));
        if (previewMode >= 0 && previewMode <= 2) {
            renderSettings_.previewMode = static_cast<PreviewMode>(previewMode);
        }
    } catch (const std::exception& exception) {
        log::warning(std::string("Ayarlar okunamadi: ") + exception.what());
    }
}

void Application::saveSettings() const
{
    try {
        const nlohmann::json root {
            {"youtubeMaxHeight", youtubeMaxHeight_},
            {"volume", volume_},
            {"previewMode", static_cast<int>(renderSettings_.previewMode)},
        };
        std::ofstream stream(settingsPath(), std::ios::binary | std::ios::trunc);
        if (stream) {
            stream << root.dump(2);
        }
    } catch (const std::exception& exception) {
        log::warning(std::string("Ayarlar kaydedilemedi: ") + exception.what());
    }
}

void Application::startYouTubeResolution()
{
    playYouTubeUrl(std::string(youtubeUrl_.data()));
}

void Application::playResolvedMedia(const YouTubeMedia& media)
{
    std::string playbackError;
    if (!video_.playNetwork(media.videoUrl, media.audioUrl, media.httpHeaders,
            media.httpChunkSize, playbackError)) {
        setError(playbackError);
        return;
    }
    currentTitle_ = media.title;
    playingYouTube_ = true;
    layoutCheckFramesLeft_ = 0;
    renderer_.resetVideoFrame();
    firstFrameLogged_ = false;
    youtubeHistory_.add(media.title, currentYouTubeUrl_);

    // yt-dlp "projection" alanina gore projeksiyon modunu otomatik sec.

    // "equirectangular" -> 360 derece, "cubemap" -> EAC cubemap,
    // "flat" -> 2D video.
    switch (media.projectionType) {
    case VideoProjection::CubemapEac:
        renderSettings_.projection = ProjectionMode::CubemapEac;
        setStatus("YouTube Cubemap (EAC) 360 video oynatiliyor: " + media.title);
        break;
    case VideoProjection::Equirectangular:
        renderSettings_.projection = ProjectionMode::Mono360;
        setStatus("YouTube 360 video oynatiliyor: " + media.title);
        break;
    case VideoProjection::Flat:
        renderSettings_.projection = ProjectionMode::Mono360;
        setStatus("YouTube 2D video oynatiliyor: " + media.title);
        break;
    case VideoProjection::Mesh: {
        // YouTube's VR clients send 360 videos as an EAC cubemap and VR180 as
        // side-by-side halves, both tagged "mesh". Trust a "VR180"/"180"
        // title; otherwise assume EAC and let the first frames correct it.
        const ProjectionMode fromTitle = guessProjection(media.title, media.videoWidth, media.videoHeight);
        if (fromTitle == ProjectionMode::Fisheye180 || fromTitle == ProjectionMode::Fisheye180Sbs) {
            renderSettings_.projection = fromTitle;
        } else {
            renderSettings_.projection = ProjectionMode::CubemapEac;
            layoutCheckFramesLeft_ = kLayoutCheckFrames;
        }
        setStatus("YouTube video oynatiliyor (" + std::string(projectionName(renderSettings_.projection))
            + ", yanlissa 1-6): " + media.title);
        break;
    }
    case VideoProjection::Unknown:
    default:
        // VR180 ve 3D videolarda yt-dlp genellikle projeksiyon vermez (ya da
        // "mesh" der); basliktaki "VR180", "3D", "SBS" gibi etiketlerden ve
        // en-boy oranindan tahmin et.
        renderSettings_.projection = guessProjection(
            media.title, media.videoWidth, media.videoHeight);
        if (renderSettings_.projection != ProjectionMode::Fisheye180
            && renderSettings_.projection != ProjectionMode::Fisheye180Sbs) {
            layoutCheckFramesLeft_ = kLayoutCheckFrames;
        }
        setStatus("YouTube video oynatiliyor (" + std::string(projectionName(renderSettings_.projection))
            + ", yanlissa 1-6): " + media.title);
        break;
    }
}

void Application::openFromCommandLine(const std::wstring& argument)
{
    const std::string utf8 = wideToUtf8(argument);
    if (isLikelyYouTubeUrl(utf8)) {
        playYouTubeUrl(utf8);
    } else {
        playLocalFile(std::filesystem::path(argument));
    }
}

void Application::playLocalFile(const std::filesystem::path& path)
{
    std::string playbackError;

    if (!video_.playFile(path, playbackError)) {
        setError(playbackError);
        return;
    }
    selectedFile_ = path;
    playingYouTube_ = false;
    layoutCheckFramesLeft_ = 0;
    pendingResumeMs_ = -1;
    currentTitle_ = wideToUtf8(path.filename().wstring());
    renderer_.resetVideoFrame();
    firstFrameLogged_ = false;
    // Ilk kare gelince cozunurluge gore projeksiyon modunu otomatik sec.
    autoProjectionPending_ = true;
    setStatus("Yerel 360 video oynatiliyor: " + currentTitle_);
}

void Application::playYouTubeUrl(const std::string& url)
{
    if (resolving_) {
        return;
    }
    if (!isLikelyYouTubeUrl(url)) {
        setError("Gecerli bir YouTube video URL'si girin.");
        return;
    }
    if (!resolver_.available()) {
        setError("yt-dlp.exe bulunamadi. scripts/bootstrap.ps1 calistirin.");
        return;
    }
    // URL'yi giris kutusuna yaz ki kullanici guncel adresi gorsun.
    std::memset(youtubeUrl_.data(), 0, youtubeUrl_.size());
    std::strncpy(youtubeUrl_.data(), url.c_str(), youtubeUrl_.size() - 1);
    error_.clear();
    status_ = "YouTube video ve ses akis adresleri cozuluyor (en fazla "
        + std::to_string(youtubeMaxHeight_) + "p)...";
    currentYouTubeUrl_ = url;
    resolving_ = true;
    const int maxHeight = youtubeMaxHeight_;
    resolutionFuture_ = std::async(std::launch::async, [this, url, maxHeight] {
        return resolver_.resolve(url, maxHeight);
    });
}


void Application::enterVrMode()
{
    if (vrMode_ || window_ == nullptr) {
        return;
    }
    SDL_GetWindowPosition(window_, &windowedX_, &windowedY_);
    SDL_GetWindowSize(window_, &windowedWidth_, &windowedHeight_);
    const int displayCount = (std::max)(SDL_GetNumVideoDisplays(), 1);
    selectedDisplay_ = std::clamp(selectedDisplay_, 0, displayCount - 1);

    SDL_Rect bounds {};
    if (SDL_GetDisplayBounds(selectedDisplay_, &bounds) == 0) {
        SDL_SetWindowPosition(window_, bounds.x + bounds.w / 2, bounds.y + bounds.h / 2);
    }
    if (SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        setError(std::string("VR tam ekran acilamadi: ") + SDL_GetError());
        SDL_SetWindowPosition(window_, windowedX_, windowedY_);
        return;
    }
    SDL_SetWindowGrab(window_, SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    vrMode_ = true;
    hmd_.recenter();
    error_.clear();
    status_ = "VR modu acik. Cikmak icin Esc veya F11.";
    log::info("DK2 stereo tam ekran modu acildi.");
}

void Application::leaveVrMode()
{
    if (!vrMode_ || window_ == nullptr) {
        return;
    }
    SDL_SetWindowGrab(window_, SDL_FALSE);
    SDL_SetWindowFullscreen(window_, 0);
    SDL_SetWindowSize(window_, windowedWidth_, windowedHeight_);
    SDL_SetWindowPosition(window_, windowedX_, windowedY_);
    SDL_ShowCursor(SDL_ENABLE);
    vrMode_ = false;
    status_ = "Masaustu kontrol ekranina donuldu.";
    log::info("DK2 stereo tam ekran modu kapatildi.");
}

void Application::toggleVrMode()
{
    if (vrMode_) {
        leaveVrMode();
    } else {
        enterVrMode();
    }
}

void Application::updateMouseOrientation()
{
}

glm::quat Application::viewOrientation() const
{
    if (hmd_.connected()) {
        return hmd_.orientation();
    }
    const glm::quat yaw = glm::angleAxis(mouseYaw_, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::quat pitch = glm::angleAxis(mousePitch_, glm::vec3(1.0F, 0.0F, 0.0F));
    return glm::normalize(yaw * pitch);
}

std::filesystem::path Application::vlcPluginDirectory() const
{
    const std::filesystem::path packaged = executableDirectory() / L"plugins";
    if (directoryExists(packaged)) {
        return packaged;
    }
#ifdef DK2VR_DEV_VLC_PLUGIN_DIR
    const std::filesystem::path development(utf8ToWide(DK2VR_DEV_VLC_PLUGIN_DIR));
    if (directoryExists(development)) {
        return development;
    }
#endif
    return packaged;
}

std::filesystem::path Application::ytDlpPath() const
{
    return executableDirectory() / L"yt-dlp.exe";
}

std::filesystem::path Application::youtubeHistoryPath() const
{
    return executableDirectory() / L"youtube_history.json";
}


void Application::setStatus(std::string message)
{
    status_ = std::move(message);
    error_.clear();
    log::info(status_);
}

void Application::setError(std::string message)
{
    error_ = std::move(message);
    log::error(error_);
}

} // namespace dk2vr
