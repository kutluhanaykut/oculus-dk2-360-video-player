#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

struct ohmd_context;
struct ohmd_device;

namespace dk2vr {

struct HmdDeviceInfo {
    int listIndex {-1};
    std::string vendor;
    std::string product;
    std::string path;
    int flags {0};
};

struct HmdDisplayInfo {
    int horizontalResolution {1920};
    int verticalResolution {1080};
    float horizontalSizeMeters {0.12576F};
    float verticalSizeMeters {0.07074F};
    float lensSeparationMeters {0.0635F};
    float fovDegrees {100.0F};
    float aspectRatio {0.888889F};
    float ipdMeters {0.064F};
    std::array<float, 6> distortion {1.0F, 0.22F, 0.24F, 0.0F, 0.0F, 0.0F};
};

class HmdManager {
public:
    HmdManager() = default;
    ~HmdManager();

    HmdManager(const HmdManager&) = delete;
    HmdManager& operator=(const HmdManager&) = delete;

    [[nodiscard]] bool initialize(std::string& error);
    void shutdown();
    void update();
    void recenter();

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool hasRotationalTracking() const noexcept;
    [[nodiscard]] const glm::quat& orientation() const noexcept;
    [[nodiscard]] const std::vector<HmdDeviceInfo>& devices() const noexcept;
    [[nodiscard]] const HmdDeviceInfo* activeDevice() const noexcept;
    [[nodiscard]] const HmdDisplayInfo& displayInfo() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    void readDisplayInfo();

    ohmd_context* context_ {nullptr};
    ohmd_device* device_ {nullptr};
    std::vector<HmdDeviceInfo> devices_;
    int activeDeviceVectorIndex_ {-1};
    HmdDisplayInfo displayInfo_;
    glm::quat rawOrientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat calibration_ {1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    bool recenterPending_ {true};
    std::string lastError_;
};

} // namespace dk2vr
