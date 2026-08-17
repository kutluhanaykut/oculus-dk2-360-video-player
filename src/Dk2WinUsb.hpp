#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <glm/gtc/quaternion.hpp>
#include <hidapi.h>
#include <libusb.h>

namespace dk2vr {

enum class Dk2Backend { None, WinUsb, HidApi, Libusb };


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

    void recenter();

    glm::quat orientation() const;

private:
    bool connectWinUsb();
    bool connectHidApi();
    bool connectLibusb();
    void readerLoop();
    void parseImuPacket(const std::uint8_t* data, std::size_t size);
    static std::string wideToUtf8(const std::wstring& source);
    static std::string narrowToUtf8(const char* source);

    void* deviceHandle_ {nullptr};
    void* winUsbHandle_ {nullptr};
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

    mutable std::mutex stateMutex_;
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat calibration_ {1.0F, 0.0F, 0.0F, 0.0F};

    // Gyro integration state for computing orientation from raw IMU data.
    std::uint64_t lastImuTimestamp_ {0};
    bool haveLastImuTimestamp_ {false};

    // Keep-alive state for the DK2 sensor.
    std::uint64_t lastKeepAliveMs_ {0};
};

} // namespace dk2vr
