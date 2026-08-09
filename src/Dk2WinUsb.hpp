#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <glm/gtc/quaternion.hpp>

namespace dk2vr {

// Direct WinUSB access for the Oculus Rift DK2 headset. This bypasses both
// hidapi and OpenHMD so that the IMU can be read even when the DK2 0.8 driver
// exposes the tracker as a WinUSB interface instead of a HID-class device.
class Dk2WinUsb {
public:
    Dk2WinUsb();
    ~Dk2WinUsb();

    Dk2WinUsb(const Dk2WinUsb&) = delete;
    Dk2WinUsb& operator=(const Dk2WinUsb&) = delete;

    [[nodiscard]] bool connect();
    void disconnect();
    [[nodiscard]] bool isConnected() const noexcept;
    [[nodiscard]] const std::string& devicePath() const noexcept;

    void recenter();

    // Snapshot the latest orientation provided by the DK2's onboard fusion.
    [[nodiscard]] glm::quat orientation() const;

private:
    void readerLoop();
    void parseImuPacket(const std::uint8_t* data, std::size_t size);

    void* deviceHandle_ {nullptr};
    void* winUsbHandle_ {nullptr};
    std::string devicePath_;

    std::atomic<bool> connected_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::thread readerThread_;

    mutable std::mutex stateMutex_;
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat calibration_ {1.0F, 0.0F, 0.0F, 0.0F};
};

} // namespace dk2vr
