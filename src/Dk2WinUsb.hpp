#pragma once

#include "OrientationFilter.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <glm/gtc/quaternion.hpp>
#include <hidapi.h>
#include <libusb.h>

namespace dk2vr {

enum class Dk2Backend { None, HidApi, Libusb };

// Direct DK2 tracker access for when OpenHMD cannot see it, which is the case
// whenever the tracker is bound to WinUSB (Zadig / the SteamVR dk2vr driver).
// libusb drives it through WinUSB; hidapi covers a tracker left on the
// Windows HID driver.
class Dk2WinUsb {
public:
    Dk2WinUsb();
    ~Dk2WinUsb();

    Dk2WinUsb(const Dk2WinUsb&) = delete;
    Dk2WinUsb& operator=(const Dk2WinUsb&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const noexcept;
    const std::string& devicePath() const noexcept;
    Dk2Backend backend() const noexcept;
    const std::string& lastError() const noexcept;

    // Cancels yaw only, so recentring while looking down keeps the horizon level.
    void recenter();

    glm::quat orientation() const;

private:
    bool connectHidApi();
    bool connectLibusb();
    void readerLoop();
    void handlePacket(const std::uint8_t* data, std::size_t size);
    static std::string wideToUtf8(const std::wstring& source);

    hid_device* hidHandle_ {nullptr};
    libusb_device_handle* libusbHandle_ {nullptr};
    libusb_context* libusbContext_ {nullptr};
    int libusbInterface_ {-1};
    std::string devicePath_;
    std::string lastError_;
    Dk2Backend activeBackend_ {Dk2Backend::None};

    std::atomic<bool> connected_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::thread readerThread_;

    // Gyro integration with gravity correction for pitch and roll.
    mutable std::mutex stateMutex_;
    OrientationFilter filter_;
    std::uint64_t lastImuTimestamp_ {0};
    bool haveLastImuTimestamp_ {false};
    std::uint32_t packetCount_ {0};

    // Keep-alive state for the DK2 sensor.
    std::uint64_t lastKeepAliveMs_ {0};
};

} // namespace dk2vr
