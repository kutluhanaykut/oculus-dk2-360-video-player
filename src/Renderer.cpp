#include "Renderer.hpp"

#include "Logger.hpp"

#include <GL/glew.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace dk2vr {
namespace {

constexpr char sphereVertexShader[] = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main()
{
    vUv = aUv;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)glsl";

constexpr char sphereFragmentShader[] = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uVideo;
uniform int uHasVideo;
uniform int uProjectionMode;
uniform int uEye;
uniform int uFlipVertical;

float lineAt(float value, float position, float width)
{
    return 1.0 - smoothstep(width, width * 2.0, abs(value - position));
}

vec3 skyGradient(float latitude)
{
    if (latitude < 0.5) {
        return mix(vec3(0.015, 0.025, 0.045), vec3(0.18, 0.32, 0.48),
            smoothstep(0.0, 0.5, latitude));
    }
    return mix(vec3(0.18, 0.32, 0.48), vec3(0.02, 0.04, 0.10),
        smoothstep(0.5, 1.0, latitude));
}

void main()
{
    vec2 uv = vUv;
    if (uProjectionMode == 1) {
        uv.y = uv.y * 0.5 + float(uEye) * 0.5;
    } else if (uProjectionMode == 2) {
        uv.x = uv.x * 0.5 + float(uEye) * 0.5;
    }
    if (uFlipVertical != 0) {
        uv.y = 1.0 - uv.y;
    }

    if (uHasVideo != 0) {
        outColor = vec4(texture(uVideo, uv).rgb, 1.0);
        return;
    }

    vec3 base = skyGradient(uv.y);
    float horizonDistance = abs(uv.y - 0.5) * 2.0;
    float depth = 1.0 - smoothstep(0.0, 0.35, horizonDistance);
    if (uv.y < 0.5 && depth > 0.0) {
        vec3 floorColor = mix(vec3(0.04, 0.07, 0.12), vec3(0.02, 0.04, 0.08), depth);
        float longitudeGrid = 1.0 - smoothstep(0.02, 0.05, abs(fract(uv.x * 16.0) - 0.5));
        float depthLine = 1.0 - smoothstep(0.02, 0.05, abs(fract(uv.y * 32.0) - 0.5));
        float gridIntensity = max(longitudeGrid, depthLine) * (0.45 + 0.55 * depth);
        floorColor += vec3(0.20, 0.45, 0.55) * gridIntensity;
        base = mix(base, floorColor, depth);
    }
    base += vec3(0.10, 0.18, 0.30) * (1.0 - smoothstep(0.0, 0.05, horizonDistance));
    base = mix(base, vec3(0.95, 0.20, 0.20), lineAt(uv.x, 0.50, 0.0025));
    base = mix(base, vec3(0.20, 0.85, 0.30), lineAt(uv.x, 0.25, 0.0025));
    base = mix(base, vec3(0.20, 0.45, 0.95), lineAt(uv.x, 0.75, 0.0025));
    base = mix(base, vec3(0.95, 0.78, 0.18), lineAt(uv.y, 0.50, 0.0025));
    base = mix(base, vec3(0.85, 0.85, 0.85), lineAt(uv.y, 0.25, 0.0035));
    base = mix(base, vec3(0.85, 0.85, 0.85), lineAt(uv.y, 0.75, 0.0035));

    float poleFade = smoothstep(0.0, 0.06, uv.y) * (1.0 - smoothstep(0.94, 1.0, uv.y));
    base *= mix(0.55, 1.0, poleFade);
    outColor = vec4(base, 1.0);
}
)glsl";

// Flat panorama shader
constexpr char flatVertexShader[] = R"glsl(
#version 330 core
in vec2 aPosition;
out vec2 vScreenPos;
void main()
{
    vScreenPos = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)glsl";

constexpr char flatFragmentShader[] = R"glsl(
#version 330 core
in vec2 vScreenPos;
out vec4 outColor;
uniform sampler2D uVideo;
uniform int uHasVideo;
uniform int uProjectionMode;
uniform int uEye;
uniform int uFlipVertical;
uniform vec2 uPan;
uniform vec2 uScale;

void main()
{
    vec2 ndc = vScreenPos * 2.0 - 1.0;
    vec2 uv = uPan + ndc * uScale;
    if (uFlipVertical != 0) {
        uv.y = 1.0 - uv.y;
    }
    if (uProjectionMode == 1) {
        uv.y = uv.y * 0.5 + float(uEye) * 0.5;
    } else if (uProjectionMode == 2) {
        uv.x = uv.x * 0.5 + float(uEye) * 0.5;
    }
    if (uHasVideo != 0) {
        outColor = vec4(texture(uVideo, uv).rgb, 1.0);
    } else {
        outColor = vec4(0.04, 0.05, 0.08, 1.0);
    }
}
)glsl";

constexpr char fullscreenVertexShader[] = R"glsl(
#version 330 core
out vec2 vUv;
void main()
{
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 position = positions[gl_VertexID];
    vUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";

constexpr char distortionFragmentShader[] = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 outColor;
uniform sampler2D uLeftEye;
uniform sampler2D uRightEye;
uniform float uK0;
uniform float uK1;
uniform float uK2;
uniform float uK3;
uniform float uK4;
uniform float uK5;
uniform float uChromatic;
uniform float uEyeAspect;
uniform float uLensRatio;
uniform int uDistortionEnabled;

vec3 sampleEye(int eye, vec2 coordinate, float radiusSquared)
{
    vec2 radial = coordinate - vec2(0.5);
    vec2 redUv = vec2(0.5) + radial * (1.0 + uChromatic * radiusSquared);
    vec2 blueUv = vec2(0.5) + radial * (1.0 - uChromatic * radiusSquared);
    float red;
    float green;
    float blue;
    if (eye == 0) {
        red = texture(uLeftEye, redUv).r;
        green = texture(uLeftEye, coordinate).g;
        blue = texture(uLeftEye, blueUv).b;
    } else {
        red = texture(uRightEye, redUv).r;
        green = texture(uRightEye, coordinate).g;
        blue = texture(uRightEye, blueUv).b;
    }
    return vec3(red, green, blue);
}

void main()
{
    int eye = vUv.x < 0.5 ? 0 : 1;
    vec2 localUv = vec2(fract(vUv.x * 2.0), vUv.y);
    vec2 sourceUv = localUv;
    float radiusSquared = 0.0;

    if (uDistortionEnabled != 0) {
        float lensCenterX = eye == 0 ? 1.0 - uLensRatio : uLensRatio;
        vec2 p = (localUv - vec2(lensCenterX, 0.5)) * 2.0;
        p.x *= uEyeAspect;
        radiusSquared = dot(p, p);
        float r2 = radiusSquared;
        float r4 = r2 * r2;
        float r6 = r4 * r2;
        float factor = uK0 + uK1 * r2 + uK2 * r4 + uK3 * r6
            + uK4 * r6 * r2 + uK5 * r6 * r4;
        float fitScale = max(factor, 0.25);
        vec2 warped = p * factor / fitScale;
        warped.x /= uEyeAspect;
        sourceUv = vec2(0.5) + warped * 0.5;
    }

    if (any(lessThan(sourceUv, vec2(0.0))) || any(greaterThan(sourceUv, vec2(1.0)))) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float edgeDistance = min(min(sourceUv.x, sourceUv.y), min(1.0 - sourceUv.x, 1.0 - sourceUv.y));
    float edgeFade = smoothstep(0.0, 0.012, edgeDistance);
    outColor = vec4(sampleEye(eye, sourceUv, radiusSquared) * edgeFade, 1.0);
}
)glsl";

struct SphereVertex {
    glm::vec3 position;
    glm::vec2 uv;
};

unsigned compileShader(const GLenum type, const char* source, std::string& error)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    error = "OpenGL shader derleme hatasi: " + log;
    return 0;
}

unsigned createProgram(const char* vertexSource, const char* fragmentSource, std::string& error)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, error);
    if (vertex == 0) {
        return 0;
    }
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }
    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    glDeleteProgram(program);
    error = "OpenGL program baglama hatasi: " + log;
    return 0;
}

glm::vec2 computeFlatPanUv(const glm::quat& orientation, float fovRadians, float aspect)
{
    const glm::vec3 euler = glm::eulerAngles(orientation);
    const float yaw = euler.y;
    const float pitch = euler.x;
    const float halfFovH = fovRadians * 0.5F;
    const float halfFovV = halfFovH / aspect;
    glm::vec2 pan;
    pan.x = 0.5F - yaw / (2.0F * static_cast<float>(std::numbers::pi)) - (halfFovH / (2.0F * static_cast<float>(std::numbers::pi)));
    pan.y = 0.5F + pitch / static_cast<float>(std::numbers::pi) - (halfFovV / (2.0F * static_cast<float>(std::numbers::pi)));
    const glm::vec2 scale(halfFovH / static_cast<float>(std::numbers::pi), halfFovV / static_cast<float>(std::numbers::pi));
    return pan;
}

} // namespace

Renderer::~Renderer()
{
    shutdown();
}

bool Renderer::initialize(std::string& error)
{
    shutdown();
    if (!createPrograms(error) || !createSphere(error)) {
        shutdown();
        return false;
    }
    createFullscreenTriangle();

    glGenTextures(1, &videoTexture_);
    glBindTexture(GL_TEXTURE_2D, videoTexture_);
    const std::array<std::uint8_t, 16> placeholder {
        10, 15, 25, 255, 18, 25, 40, 255,
        18, 25, 40, 255, 10, 15, 25, 255,
    };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
        placeholder.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    log::info("OpenGL 360 derece stereo renderer baslatildi.");
    return true;
}

void Renderer::shutdown()
{
    destroyEyeTargets();
    if (videoTexture_ != 0) {
        glDeleteTextures(1, &videoTexture_);
        videoTexture_ = 0;
    }
    if (fullscreenVao_ != 0) {
        glDeleteVertexArrays(1, &fullscreenVao_);
        fullscreenVao_ = 0;
    }
    if (sphereEbo_ != 0) {
        glDeleteBuffers(1, &sphereEbo_);
        sphereEbo_ = 0;
    }
    if (sphereVbo_ != 0) {
        glDeleteBuffers(1, &sphereVbo_);
        sphereVbo_ = 0;
    }
    if (sphereVao_ != 0) {
        glDeleteVertexArrays(1, &sphereVao_);
        sphereVao_ = 0;
    }
    if (distortionProgram_ != 0) {
        glDeleteProgram(distortionProgram_);
        distortionProgram_ = 0;
    }
    if (flatProgram_ != 0) {
        glDeleteProgram(flatProgram_);
        flatProgram_ = 0;
    }
    if (sphereProgram_ != 0) {
        glDeleteProgram(sphereProgram_);
        sphereProgram_ = 0;
    }
    sphereIndexCount_ = 0;
    videoWidth_ = 0;
    videoHeight_ = 0;
    hasVideoFrame_ = false;
}

bool Renderer::createPrograms(std::string& error)
{
    sphereProgram_ = createProgram(sphereVertexShader, sphereFragmentShader, error);
    if (sphereProgram_ == 0) {
        return false;
    }
    flatProgram_ = createProgram(flatVertexShader, flatFragmentShader, error);
    if (flatProgram_ == 0) {
        return false;
    }
    distortionProgram_ = createProgram(fullscreenVertexShader, distortionFragmentShader, error);
    return distortionProgram_ != 0;
}

bool Renderer::createSphere(std::string& /*error*/)
{
    constexpr unsigned longitudeSegments = 128;
    constexpr unsigned latitudeSegments = 64;
    constexpr float radius = 10.0F;
    std::vector<SphereVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve((longitudeSegments + 1) * (latitudeSegments + 1));
    indices.reserve(longitudeSegments * latitudeSegments * 6);

    for (unsigned latitude = 0; latitude <= latitudeSegments; ++latitude) {
        const float v = static_cast<float>(latitude) / static_cast<float>(latitudeSegments);
        const float phi = v * std::numbers::pi_v<float>;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (unsigned longitude = 0; longitude <= longitudeSegments; ++longitude) {
            const float u = static_cast<float>(longitude) / static_cast<float>(longitudeSegments);
            const float theta = (u - 0.5F) * 2.0F * std::numbers::pi_v<float>;
            const glm::vec3 position(
                radius * sinPhi * std::sin(theta),
                radius * cosPhi,
                -radius * sinPhi * std::cos(theta));
            vertices.push_back({position, glm::vec2(u, v)});
        }
    }

    const unsigned rowLength = longitudeSegments + 1;
    for (unsigned latitude = 0; latitude < latitudeSegments; ++latitude) {
        for (unsigned longitude = 0; longitude < longitudeSegments; ++longitude) {
            const std::uint32_t topLeft = latitude * rowLength + longitude;
            const std::uint32_t bottomLeft = (latitude + 1) * rowLength + longitude;
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topLeft + 1);
            indices.push_back(topLeft + 1);
            indices.push_back(bottomLeft);
            indices.push_back(bottomLeft + 1);
        }
    }

    glGenVertexArrays(1, &sphereVao_);
    glGenBuffers(1, &sphereVbo_);
    glGenBuffers(1, &sphereEbo_);
    glBindVertexArray(sphereVao_);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(SphereVertex)),
        vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex),
        reinterpret_cast<void*>(offsetof(SphereVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SphereVertex),
        reinterpret_cast<void*>(offsetof(SphereVertex, uv)));
    glBindVertexArray(0);
    sphereIndexCount_ = static_cast<unsigned>(indices.size());
    return true;
}

void Renderer::createFullscreenTriangle()
{
    glGenVertexArrays(1, &fullscreenVao_);
}

void Renderer::uploadVideoFrame(
    const std::uint8_t* pixels, const unsigned width, const unsigned height, const unsigned pitch)
{
    if (videoTexture_ == 0 || pixels == nullptr || width == 0 || height == 0 || pitch < width * 4U) {
        return;
    }
    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if (width > static_cast<unsigned>(maximumTextureSize)
        || height > static_cast<unsigned>(maximumTextureSize)) {
        if (!oversizedFrameReported_) {
            log::error("Video cozunurlugu GPU GL_MAX_TEXTURE_SIZE sinirini asiyor.");
            oversizedFrameReported_ = true;
        }
        return;
    }

    glBindTexture(GL_TEXTURE_2D, videoTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitch / 4U));
    if (videoWidth_ != width || videoHeight_ != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width),
            static_cast<GLsizei>(height), 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
        videoWidth_ = width;
        videoHeight_ = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width),
            static_cast<GLsizei>(height), GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    hasVideoFrame_ = true;
}

void Renderer::renderPreview(const int framebufferWidth, const int framebufferHeight,
    const glm::quat& orientation, const RenderSettings& settings)
{
    if (!initialized() || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }
    if (settings.previewMode == PreviewMode::AnaglyphRedCyan) {
        renderAnaglyph(framebufferWidth, framebufferHeight, orientation, settings);
        return;
    }
    if (settings.previewMode == PreviewMode::SideBySideStereo) {
        const int halfWidth = framebufferWidth / 2;
        renderSideBySide(framebufferWidth, framebufferHeight, orientation, settings,
            static_cast<float>(halfWidth), static_cast<float>(framebufferHeight));
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.008F, 0.012F, 0.02F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (settings.viewMode == ViewMode::PanoramaFlat
        || settings.viewMode == ViewMode::Fisheye180) {
        renderFlatPanorama(0, framebufferWidth, framebufferHeight, orientation, settings);
    } else {
        renderSphereEye(0, framebufferWidth, framebufferHeight, orientation, settings);
    }
}

void Renderer::renderVr(const int framebufferWidth, const int framebufferHeight,
    const glm::quat& orientation, const RenderSettings& settings)
{
    if (!initialized() || framebufferWidth < 2 || framebufferHeight <= 0) {
        return;
    }
    const int eyeWidth = framebufferWidth / 2;
    if (!ensureEyeTargets(eyeWidth, framebufferHeight)) {
        renderPreview(framebufferWidth, framebufferHeight, orientation, settings);
        return;
    }

    for (int eye = 0; eye < 2; ++eye) {
        glBindFramebuffer(GL_FRAMEBUFFER, eyes_[eye].framebuffer);
        glViewport(0, 0, eyeWidth, framebufferHeight);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (settings.viewMode == ViewMode::PanoramaFlat
            || settings.viewMode == ViewMode::Fisheye180) {
            renderFlatPanorama(eye, eyeWidth, framebufferHeight, orientation, settings);
        } else {
            renderSphereEye(eye, eyeWidth, framebufferHeight, orientation, settings);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(distortionProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, eyes_[0].colorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, eyes_[1].colorTexture);
    glUniform1i(glGetUniformLocation(distortionProgram_, "uLeftEye"), 0);
    glUniform1i(glGetUniformLocation(distortionProgram_, "uRightEye"), 1);
    const float* k = settings.distortion.data();
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK0"), k[0]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK1"), k[1]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK2"), k[2]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK3"), k[3]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK4"), k[4]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uK5"), k[5]);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uChromatic"),
        settings.chromaticAberration);
    glUniform1f(glGetUniformLocation(distortionProgram_, "uEyeAspect"),
        static_cast<float>(eyeWidth) / static_cast<float>(framebufferHeight));
    const float lensRatio = settings.screenWidthMeters > 0.001F
        ? std::clamp(settings.lensSeparationMeters / settings.screenWidthMeters, 0.4F, 0.6F)
        : 0.5F;
    glUniform1f(glGetUniformLocation(distortionProgram_, "uLensRatio"), lensRatio);
    glUniform1i(glGetUniformLocation(distortionProgram_, "uDistortionEnabled"),
        settings.distortionEnabled ? 1 : 0);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void Renderer::renderMirror(const int framebufferWidth, const int framebufferHeight,
    const glm::quat& orientation, const RenderSettings& settings, const int targetEye)
{
    if (!initialized() || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }
    if (targetEye < 0 || targetEye > 1) {
        return;
    }
    if (!ensureEyeTargets(framebufferWidth, framebufferHeight)) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, eyes_[targetEye].framebuffer);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (settings.viewMode == ViewMode::PanoramaFlat
        || settings.viewMode == ViewMode::Fisheye180) {
        renderFlatPanorama(targetEye, framebufferWidth, framebufferHeight, orientation, settings);
    } else {
        renderSphereEye(targetEye, framebufferWidth, framebufferHeight, orientation, settings);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    blitEye(eyes_[targetEye].colorTexture, 0, 0, framebufferWidth, framebufferHeight);
}

void Renderer::blitEye(const unsigned colorTexture, const int x, const int y,
    const int width, const int height)
{
    if (colorTexture == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    const float u0 = 0.0F;
    const float u1 = 1.0F;
    const float v0 = 1.0F;
    const float v1 = 0.0F;
    glTexCoord2f(u0, v0); glVertex2i(x, y);
    glTexCoord2f(u1, v0); glVertex2i(x + width, y);
    glTexCoord2f(u1, v1); glVertex2i(x + width, y + height);
    glTexCoord2f(u0, v1); glVertex2i(x, y + height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::renderSphereEye(const int eye, const int width, const int height,
    const glm::quat& orientation, const RenderSettings& settings)
{
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    const glm::mat4 projection = glm::perspective(
        glm::radians(std::clamp(settings.fovDegrees, 60.0F, 130.0F)), aspect, 0.01F, 50.0F);
    const glm::mat4 view = glm::mat4_cast(glm::conjugate(glm::normalize(orientation)));
    const glm::mat4 mvp = projection * view;

    glUseProgram(sphereProgram_);
    glUniformMatrix4fv(glGetUniformLocation(sphereProgram_, "uMvp"), 1, GL_FALSE,
        glm::value_ptr(mvp));
    glUniform1i(glGetUniformLocation(sphereProgram_, "uVideo"), 0);
    glUniform1i(glGetUniformLocation(sphereProgram_, "uHasVideo"), hasVideoFrame_ ? 1 : 0);
    glUniform1i(glGetUniformLocation(sphereProgram_, "uProjectionMode"),
        static_cast<int>(settings.projection));
    glUniform1i(glGetUniformLocation(sphereProgram_, "uEye"), eye);
    glUniform1i(glGetUniformLocation(sphereProgram_, "uFlipVertical"),
        settings.flipVertical ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, videoTexture_);
    glBindVertexArray(sphereVao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sphereIndexCount_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Renderer::renderFlatPanorama(const int eye, const int width, const int height,
    const glm::quat& orientation, const RenderSettings& settings)
{
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    const float fovRadians = glm::radians(std::clamp(settings.fovDegrees, 60.0F, 130.0F));
    const float halfFovH = fovRadians * 0.5F;
    const float halfFovV = std::atan(std::tan(halfFovH) / aspect);

    const glm::vec3 euler = glm::eulerAngles(orientation);
    const float yaw = euler.y;
    const float pitch = euler.x;
    constexpr float twoPi = 2.0F * static_cast<float>(std::numbers::pi);
    constexpr float onePi = static_cast<float>(std::numbers::pi);

    glm::vec2 pan;
    pan.x = 0.5F - yaw / twoPi - (halfFovH / twoPi);
    pan.y = 0.5F + pitch / onePi - (halfFovV / onePi);

    const glm::vec2 scale(std::tan(halfFovH) / onePi, std::tan(halfFovV) / onePi);

    glUseProgram(flatProgram_);
    glUniform1i(glGetUniformLocation(flatProgram_, "uVideo"), 0);
    glUniform1i(glGetUniformLocation(flatProgram_, "uHasVideo"), hasVideoFrame_ ? 1 : 0);
    glUniform1i(glGetUniformLocation(flatProgram_, "uProjectionMode"),
        static_cast<int>(settings.projection));
    glUniform1i(glGetUniformLocation(flatProgram_, "uEye"), eye);
    glUniform1i(glGetUniformLocation(flatProgram_, "uFlipVertical"),
        settings.flipVertical ? 1 : 0);
    glUniform2f(glGetUniformLocation(flatProgram_, "uPan"), pan.x, pan.y);
    glUniform2f(glGetUniformLocation(flatProgram_, "uScale"), scale.x, scale.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, videoTexture_);
    glBindVertexArray(fullscreenVao_);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Renderer::renderFisheye180(const int eye, const int width, const int height,
    const glm::quat& orientation, const RenderSettings& settings)
{
    renderFlatPanorama(eye, width, height, orientation, settings);
}

void Renderer::renderAnaglyph(const int width, const int height,
    const glm::quat& orientation, const RenderSettings& settings)
{
    if (!ensureEyeTargets(width, height)) {
        return;
    }
    for (int eye = 0; eye < 2; ++eye) {
        glBindFramebuffer(GL_FRAMEBUFFER, eyes_[eye].framebuffer);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderSphereEye(eye, width, height, orientation, settings);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindTexture(GL_TEXTURE_2D, eyes_[0].colorTexture);
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    blitEye(eyes_[0].colorTexture, 0, 0, width, height);
    glBindTexture(GL_TEXTURE_2D, eyes_[1].colorTexture);
    glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
    blitEye(eyes_[1].colorTexture, 0, 0, width, height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::renderSideBySide(const int width, const int height,
    const glm::quat& orientation, const RenderSettings& settings,
    const float halfWidth, const float halfHeight)
{
    (void)halfHeight;
    if (!ensureEyeTargets(static_cast<int>(halfWidth), height)) {
        return;
    }
    for (int eye = 0; eye < 2; ++eye) {
        glBindFramebuffer(GL_FRAMEBUFFER, eyes_[eye].framebuffer);
        glViewport(0, 0, static_cast<int>(halfWidth), height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderSphereEye(eye, static_cast<int>(halfWidth), height, orientation, settings);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    blitEye(eyes_[0].colorTexture, 0, 0, static_cast<int>(halfWidth), height);
    blitEye(eyes_[1].colorTexture, static_cast<int>(halfWidth), 0,
        width - static_cast<int>(halfWidth), height);
}

} // namespace dk2vr
