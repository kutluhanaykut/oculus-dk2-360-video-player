#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <glm/gtc/quaternion.hpp>
#include <hidapi.h>

namespace dk2vr {

enum class Dk2Backend { None, WinUsb, HidApi };

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

    void recenter();

    glm::quat orientation() const;

private:
    bool connectWinUsb();
    bool connectHidApi();
    void readerLoop();
    void parseImuPacket(const std::uint8_t* data, std::size_t size);
    static std::string wideToUtf8(const std::wstring& source);
    static std::string narrowToUtf8(const char* source);

    void* deviceHandle_ {nullptr};
    void* winUsbHandle_ {nullptr};
    hid_device* hidHandle_ {nullptr};
    std::string devicePath_;
    Dk2Backend activeBackend_ {Dk2Backend::None};

    std::atomic<bool> connected_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::thread readerThread_;

    mutable std::mutex stateMutex_;
    glm::quat orientation_ {1.0F, 0.0F, 0.0F, 0.0F};
    glm::quat calibration_ {1.0F, 0.0F, 0.0F, 0.0F};
};

} // namespace dk2vr
