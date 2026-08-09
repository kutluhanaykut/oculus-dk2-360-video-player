#pragma once

#include "Dk2WinUsb.hpp"
#include "Projection.hpp"

#include <array>
#include <memory>
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
    HmdManager();
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
    [[nodiscard]] const std::string& activeBackend() const noexcept;

private:
    bool initializeOpenHmd(std::string& error);
    void shutdownOpenHmd();
    void applyDefaults(HmdDisplayInfo& display) const;

    std::unique_ptr<Dk2WinUsb> winUsbDk2_;
    ohmd_context* context_ {nullptr};
    ohmd_device* device_ {nullptr};
    std::vector<HmdDeviceInfo> devices_;
    int activeDeviceVectorIndex_ {-1};
    HmdDisplayInfo displayInfo_;
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    bool recenterPending_ {true};
    bool openHmdActive_ {false};
    std::string lastError_;
    std::string activeBackend_ {"Yok"};
};

} // namespace dk2vr
