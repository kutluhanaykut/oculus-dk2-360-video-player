#include <GL/glew.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "Application.hpp"

#include "FileDialog.hpp"
#include "Logger.hpp"
#include "Process.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

} // namespace

Application::Application()
    : resolver_(ytDlpPath())
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

    const int displayCount = std::max(SDL_GetNumVideoDisplays(), 1);
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
        hmd_.update();
        video_.consumeLatestFrame([this](const std::uint8_t* pixels, const unsigned width,
                                      const unsigned height, const unsigned pitch) {
            renderer_.uploadVideoFrame(pixels, width, height, pitch);
        });
        renderFrame();
    }
    return 0;
}

void Application::shutdown()
{
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
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0
        && (vrMode_ || !keyboardCaptured || event.key.keysym.sym == SDLK_ESCAPE
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
            volume_ = std::min(volume_ + 5, 100);
            video_.setVolume(volume_);
            break;
        case SDLK_DOWN:
            volume_ = std::max(volume_ - 5, 0);
            video_.setVolume(volume_);
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

    const glm::quat orientation = viewOrientation();
    if (vrMode_) {
        renderer_.renderVr(width, height, orientation, renderSettings_);
    } else {
        renderer_.renderPreview(width, height, orientation, renderSettings_);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        renderUserInterface();
        ImGui::Render();
        glViewport(0, 0, width, height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    SDL_GL_SwapWindow(window_);
}

void Application::renderUserInterface()
{
    ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(540.0F, 0.0F), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("DK2 360 VR Player", nullptr, flags);
    ImGui::Text("Acik kaynak Windows 11 / Oculus Rift DK2 oynatici");
    ImGui::Separator();

    drawPlaybackControls();
    if (ImGui::CollapsingHeader("360 video ve lens ayarlari", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawSettings();
    }
    if (ImGui::CollapsingHeader("DK2 ve ekran", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawDevicePanel();
    }

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
    ImGui::TextDisabled("Kisayol: F11 VR | Space duraklat | R merkez | <-/-> 10 sn | 1/2/3 format");
    ImGui::End();
}

void Application::drawPlaybackControls()
{
    if (ImGui::Button("Yerel 360 video ac...", ImVec2(220.0F, 0.0F))) {
        if (const auto path = openVideoFileDialog()) {
            playLocalFile(*path);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Dosyayi pencereye de surukleyebilirsiniz");

    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##youtube", "https://www.youtube.com/watch?v=...",
        youtubeUrl_.data(), youtubeUrl_.size());
    ImGui::BeginDisabled(resolving_);
    if (ImGui::Button(resolving_ ? "YouTube cozuluyor..." : "YouTube 360 videoyu oynat",
            ImVec2(220.0F, 0.0F))) {
        startYouTubeResolution();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("yt-dlp: %s", resolver_.available() ? "hazir" : "bulunamadi");

    if (!currentTitle_.empty()) {
        ImGui::TextWrapped("Medya: %s", currentTitle_.c_str());
    }
    ImGui::Text("Durum: %s", video_.stateText().c_str());
    if (renderer_.hasVideoFrame()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%u x %u)", renderer_.videoWidth(), renderer_.videoHeight());
    }

    if (ImGui::Button("Oynat / duraklat")) {
        video_.togglePause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Durdur")) {
        video_.stop();
    }
    ImGui::SameLine();
    ImGui::Text("%s / %s", formatTime(video_.time()).c_str(), formatTime(video_.duration()).c_str());

    const std::int64_t duration = video_.duration();
    float seconds = static_cast<float>(video_.time()) / 1000.0F;
    const float durationSeconds = static_cast<float>(std::max<std::int64_t>(duration, 1)) / 1000.0F;
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderFloat("##timeline", &seconds, 0.0F, durationSeconds, "%.1f sn")) {
        video_.seek(static_cast<std::int64_t>(seconds * 1000.0F));
    }

    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::SliderInt("Ses", &volume_, 0, 100, "%d%%")) {
        video_.setVolume(volume_);
    }
}

void Application::drawSettings()
{
    int projection = static_cast<int>(renderSettings_.projection);
    constexpr const char* projectionNames[] {"Mono 360", "3D 360 ust/alt", "3D 360 yan yana"};
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::Combo("Video yerlesimi", &projection, projectionNames,
            static_cast<int>(std::size(projectionNames)))) {
        renderSettings_.projection = static_cast<ProjectionMode>(projection);
    }
    int previewMode = static_cast<int>(renderSettings_.previewMode);
    constexpr const char* previewModeNames[] {"Tek goz (monitor)", "Side-by-side 3D", "Anaglif kirmizi/cyan"};
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::Combo("Onizleme modu", &previewMode, previewModeNames,
            static_cast<int>(std::size(previewModeNames)))) {
        renderSettings_.previewMode = static_cast<PreviewMode>(previewMode);
    }
    ImGui::SetNextItemWidth(220.0F);
    ImGui::SliderFloat("Gorus alani", &renderSettings_.fovDegrees, 70.0F, 125.0F, "%.1f derece");
    ImGui::Checkbox("DK2 lens distorsiyon duzeltmesi", &renderSettings_.distortionEnabled);
    ImGui::Checkbox("Videoyu dikey cevir", &renderSettings_.flipVertical);
    if (renderSettings_.distortionEnabled) {
        ImGui::SetNextItemWidth(180.0F);
        ImGui::SliderFloat("K0 (scale)", &renderSettings_.distortion[0], 0.0F, 2.0F, "%.3f");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::SliderFloat("K1 (r2)", &renderSettings_.distortion[1], 0.0F, 1.0F, "%.3f");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::SliderFloat("K2 (r4)", &renderSettings_.distortion[2], 0.0F, 1.0F, "%.3f");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::SliderFloat("K3 (r6)", &renderSettings_.distortion[3], -0.2F, 0.2F, "%.4f");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::SliderFloat("Renk sapmasi", &renderSettings_.chromaticAberration, 0.0F, 0.03F, "%.4f");
    }
    if (ImGui::Button("DK2 varsayilan lens ayarlari")) {
        renderSettings_.fovDegrees = 100.0F;
        renderSettings_.distortion = {1.0F, 0.22F, 0.24F, 0.0F, 0.0F, 0.0F};
        renderSettings_.chromaticAberration = 0.008F;
        renderSettings_.screenWidthMeters = 0.12576F;
        renderSettings_.lensSeparationMeters = 0.0635F;
        renderSettings_.ipdMeters = 0.064F;
    }
}

void Application::drawDevicePanel()
{
    if (hmd_.connected()) {
        const HmdDeviceInfo* device = hmd_.activeDevice();
        const HmdDisplayInfo& display = hmd_.displayInfo();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38F, 0.95F, 0.55F, 1.0F));
        ImGui::Text("Dahili jiroskop: BAGLI (%s)", hmd_.activeBackend().c_str());
        ImGui::PopStyleColor();
        if (device != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", device->vendor.c_str(), device->product.c_str());
        }
        ImGui::TextDisabled("Panel bilgisi: %d x %d, FOV %.1f",
            display.horizontalResolution, display.verticalResolution, display.fovDegrees);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.62F, 0.20F, 1.0F));
        ImGui::Text("Dahili jiroskop: DK2 BULUNAMADI");
        ImGui::PopStyleColor();
        if (!hmd_.lastError().empty()) {
            ImGui::TextWrapped("%s", hmd_.lastError().c_str());
        }
        ImGui::TextDisabled("Cozum: DK2'nin USB kablosunun bagli oldugunu dogrulayin. "
            "Windows Aygit Yoneticisi'nde DK2 izleme cihazini bulun. "
            "Cihaz WinUSB veya libusb-win32 surucusune bagli olmalidir. "
            "Oculus 0.8 runtime kurun veya Zadig ile surucuyu degistirin. "
            "Detayli log icin DK2VRPlayer.log dosyasina bakin.");
    }
    if (ImGui::Button("DK2'yi yeniden tara")) {
        std::string hmdError;
        if (hmd_.initialize(hmdError)) {
            setStatus("DK2 ve dahili jiroskop baglandi.");
        } else {
            setError(hmdError);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Bakisi merkezle (R)")) {
        hmd_.recenter();
        mouseYaw_ = 0.0F;
        mousePitch_ = 0.0F;
    }

    const int displayCount = std::max(SDL_GetNumVideoDisplays(), 1);
    selectedDisplay_ = std::clamp(selectedDisplay_, 0, displayCount - 1);
    const char* selectedName = SDL_GetDisplayName(selectedDisplay_);
    std::string preview = selectedName != nullptr
        ? std::to_string(selectedDisplay_ + 1) + ": " + selectedName
        : "Ekran " + std::to_string(selectedDisplay_ + 1);
    ImGui::SetNextItemWidth(330.0F);
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

    if (ImGui::Button("DK2 ekraninda VR tam ekran (F11)", ImVec2(310.0F, 34.0F))) {
        enterVrMode();
    }
    ImGui::TextWrapped("DK2, Windows'ta genisletilmis ekran olarak 1920x1080 yatay ve 75 Hz ayarlanmalidir. Harici konum kamerasi kullanilmaz; yalnizca gozlukteki IMU/jiroskop okunur.");
    ImGui::TextWrapped("Windows 11 HDMI cikisini otomatik tanimiyorsa: NVIDIA/AMD kontrol panelinden 'Add display' deneyin; olmazsa spacedesk/Parsec/Sunlight gibi sanal ekran surucusu kurup masaustunu DK2'ye genisletin. Bu olmazsa 'Onizleme modu' kismindan 'Side-by-side 3D' veya 'Anaglif' secerek jiroskop takibiyle birlikte ana monitorden izleyebilirsiniz.");
}

void Application::startYouTubeResolution()
{
    if (resolving_) {
        return;
    }
    const std::string url(youtubeUrl_.data());
    if (!isLikelyYouTubeUrl(url)) {
        setError("Gecerli bir YouTube video URL'si girin.");
        return;
    }
    if (!resolver_.available()) {
        setError("yt-dlp.exe bulunamadi. scripts/bootstrap.ps1 calistirin.");
        return;
    }
    error_.clear();
    status_ = "YouTube video ve ses akis adresleri cozuluyor...";
    resolving_ = true;
    resolutionFuture_ = std::async(std::launch::async, [this, url] {
        return resolver_.resolve(url);
    });
}

void Application::playResolvedMedia(const YouTubeMedia& media)
{
    std::string playbackError;
    if (!video_.playNetwork(media.videoUrl, media.audioUrl, media.httpHeaders, playbackError)) {
        setError(playbackError);
        return;
    }
    currentTitle_ = media.title;
    setStatus("YouTube 360 video oynatiliyor: " + media.title);
}

void Application::playLocalFile(const std::filesystem::path& path)
{
    std::string playbackError;
    if (!video_.playFile(path, playbackError)) {
        setError(playbackError);
        return;
    }
    selectedFile_ = path;
    currentTitle_ = wideToUtf8(path.filename().wstring());
    setStatus("Yerel 360 video oynatiliyor: " + currentTitle_);
}

void Application::enterVrMode()
{
    if (vrMode_ || window_ == nullptr) {
        return;
    }
    SDL_GetWindowPosition(window_, &windowedX_, &windowedY_);
    SDL_GetWindowSize(window_, &windowedWidth_, &windowedHeight_);
    const int displayCount = std::max(SDL_GetNumVideoDisplays(), 1);
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
