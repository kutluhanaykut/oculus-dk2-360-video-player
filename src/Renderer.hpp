#pragma once

#include "Projection.hpp"

#include <cstdint>
#include <string>

#include <glm/gtc/quaternion.hpp>

namespace dk2vr {

struct RenderSettings {
    ProjectionMode projection {ProjectionMode::Mono360};
    float fovDegrees {100.0F};
    float distortionK1 {0.22F};
    float distortionK2 {0.24F};
    float chromaticAberration {0.008F};
    float screenWidthMeters {0.12576F};
    float lensSeparationMeters {0.0635F};
    bool distortionEnabled {true};
    bool flipVertical {false};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool initialize(std::string& error);
    void shutdown();

    void uploadVideoFrame(
        const std::uint8_t* pixels, unsigned width, unsigned height, unsigned pitch);
    void renderPreview(
        int framebufferWidth,
        int framebufferHeight,
        const glm::quat& orientation,
        const RenderSettings& settings);
    void renderVr(
        int framebufferWidth,
        int framebufferHeight,
        const glm::quat& orientation,
        const RenderSettings& settings);

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool hasVideoFrame() const noexcept;
    [[nodiscard]] unsigned videoWidth() const noexcept;
    [[nodiscard]] unsigned videoHeight() const noexcept;

private:
    struct EyeTarget {
        unsigned framebuffer {0};
        unsigned colorTexture {0};
        unsigned depthBuffer {0};
    };

    [[nodiscard]] bool createPrograms(std::string& error);
    [[nodiscard]] bool createSphere(std::string& error);
    void createFullscreenTriangle();
    [[nodiscard]] bool ensureEyeTargets(int width, int height);
    void destroyEyeTargets();
    void renderSphereEye(
        int eye,
        int width,
        int height,
        const glm::quat& orientation,
        const RenderSettings& settings);

    unsigned sphereProgram_ {0};
    unsigned distortionProgram_ {0};
    unsigned sphereVao_ {0};
    unsigned sphereVbo_ {0};
    unsigned sphereEbo_ {0};
    unsigned sphereIndexCount_ {0};
    unsigned fullscreenVao_ {0};
    unsigned videoTexture_ {0};
    unsigned videoWidth_ {0};
    unsigned videoHeight_ {0};
    bool hasVideoFrame_ {false};
    bool oversizedFrameReported_ {false};
    EyeTarget eyes_[2];
    int eyeTargetWidth_ {0};
    int eyeTargetHeight_ {0};
};

} // namespace dk2vr
