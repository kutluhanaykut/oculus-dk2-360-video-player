#include "Dk2WinUsb.hpp"

#include "Logger.hpp"

#include <Windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usbiodef.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace dk2vr {
namespace {

// {A5DCBF10-6530-11D2-901F-00C04FB951ED} is GUID_DEVINTERFACE_USB_DEVICE.
const GUID kUsbDeviceInterfaceGuid = {
    0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

constexpr std::uint16_t kOculusVendorId = 0x2833;
constexpr std::uint16_t kDk2ProductIds[] = {0x0021, 0x2021};

constexpr std::uint8_t kDk2ImuEndpoint = 0x81;

bool wideToUtf8(const std::wstring& source, std::string& destination)
{
    if (source.empty()) {
        destination.clear();
        return true;
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, source.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return false;
    }
    destination.assign(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, source.c_str(), -1, destination.data(), length, nullptr, nullptr);
    return true;
}

} // namespace

Dk2WinUsb::Dk2WinUsb() = default;

Dk2WinUsb::~Dk2WinUsb()
{
    disconnect();
}

bool Dk2WinUsb::connect()
{
    if (connected_) {
        return true;
    }
    devicePath_.clear();
    deviceHandle_ = nullptr;
    winUsbHandle_ = nullptr;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &kUsbDeviceInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        log::warning("Dk2WinUsb: SetupDiGetClassDevs basarisiz oldu.");
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData {};
    interfaceData.cbSize = sizeof(interfaceData);

    bool found = false;
    for (int deviceIndex = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &kUsbDeviceInterfaceGuid, deviceIndex, &interfaceData);
         ++deviceIndex) {
        DWORD requiredSize = 0;
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr)
            || requiredSize == 0) {
            continue;
        }

        std::vector<std::uint8_t> buffer(requiredSize, 0);
        auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detailData->cbSize = sizeof(*detailData);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
            continue;
        }

        HANDLE deviceHandle = CreateFileW(detailData->DevicePath,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_WRITE | FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            continue;
        }

        WINUSB_INTERFACE_HANDLE winUsbHandle = nullptr;
        if (!WinUsb_Initialize(deviceHandle, &winUsbHandle)) {
            CloseHandle(deviceHandle);
            continue;
        }

        USB_DEVICE_DESCRIPTOR descriptor {};
        unsigned long lengthTransferred = 0;
        const bool descriptorOk = WinUsb_GetDescriptor(winUsbHandle, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
            reinterpret_cast<unsigned char*>(&descriptor), sizeof(descriptor), &lengthTransferred);
        if (!descriptorOk) {
            WinUsb_Free(winUsbHandle);
            CloseHandle(deviceHandle);
            continue;
        }

        const bool vendorMatch = descriptor.idVendor == kOculusVendorId;
        const bool productMatch = std::find(std::begin(kDk2ProductIds), std::end(kDk2ProductIds),
            descriptor.idProduct) != std::end(kDk2ProductIds);
        if (vendorMatch && productMatch) {
            deviceHandle_ = deviceHandle;
            winUsbHandle_ = winUsbHandle;
            wideToUtf8(detailData->DevicePath, devicePath_);
            log::info("Dk2WinUsb: DK2 bulundu, VID=0x2833 PID=0x" + std::to_string(descriptor.idProduct)
                + " yol=" + devicePath_);
            found = true;
            break;
        }

        WinUsb_Free(winUsbHandle);
        CloseHandle(deviceHandle);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (!found) {
        log::warning("Dk2WinUsb: SetupAPI DK2 bulamadi (WinUSB arayuzu bekleniyor).");
        return false;
    }

    connected_ = true;
    stopRequested_ = false;
    readerThread_ = std::thread(&Dk2WinUsb::readerLoop, this);
    return true;
}

void Dk2WinUsb::disconnect()
{
    stopRequested_ = true;
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (winUsbHandle_ != nullptr) {
        WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(winUsbHandle_));
        winUsbHandle_ = nullptr;
    }
    if (deviceHandle_ != nullptr && deviceHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(deviceHandle_));
        deviceHandle_ = nullptr;
    }
    devicePath_.clear();
    connected_ = false;
}

bool Dk2WinUsb::isConnected() const noexcept
{
    return connected_;
}

const std::string& Dk2WinUsb::devicePath() const noexcept
{
    return devicePath_;
}

void Dk2WinUsb::recenter()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    calibration_ = glm::inverse(orientation_);
}

glm::quat Dk2WinUsb::orientation() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return calibration_ * orientation_;
}

void Dk2WinUsb::readerLoop()
{
    while (!stopRequested_ && winUsbHandle_ != nullptr) {
        std::uint8_t buffer[64] {};
        unsigned long bytesRead = 0;
        const BOOL ok = WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(winUsbHandle_),
            kDk2ImuEndpoint, buffer, sizeof(buffer), &bytesRead, nullptr);
        if (!ok) {
            const DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT || error == ERROR_IO_INCOMPLETE
                || error == WAIT_TIMEOUT) {
                continue;
            }
            log::warning("Dk2WinUsb: WinUsb_ReadPipe basarisiz oldu, hata=" + std::to_string(error));
            break;
        }
        if (bytesRead > 0) {
            parseImuPacket(buffer, bytesRead);
        }
    }
    log::info("Dk2WinUsb: okuyucu dongusu sona erdi.");
}

void Dk2WinUsb::parseImuPacket(const std::uint8_t* data, const std::size_t size)
{
    // The DK2 IMU report is 26 bytes. Byte 0 is the report ID (0x0B for
    // sensors) and byte 1 is a sample counter. Bytes 2-7 carry the fused
    // yaw/pitch/roll (int16, scaled by 0.002 radians) which is what we use
    // for orientation. The remaining bytes carry gyro, accel, magnetometer
    // and temperature samples that OpenHMD can also consume.
    if (size < 26 || data[0] != 0x0B) {
        return;
    }
    const auto read16 = [](const std::uint8_t high, const std::uint8_t low) {
        return static_cast<std::int16_t>(
            (static_cast<std::int16_t>(high) << 8) | static_cast<std::uint16_t>(low));
    };
    const float yaw = static_cast<float>(read16(data[2], data[3])) * 0.002F;
    const float pitch = static_cast<float>(read16(data[4], data[5])) * 0.002F;
    const float roll = static_cast<float>(read16(data[6], data[7])) * 0.002F;

    const glm::quat qYaw = glm::angleAxis(yaw, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::quat qPitch = glm::angleAxis(pitch, glm::vec3(1.0F, 0.0F, 0.0F));
    const glm::quat qRoll = glm::angleAxis(roll, glm::vec3(0.0F, 0.0F, 1.0F));
    const glm::quat fused = qYaw * qPitch * qRoll;
    if (!std::isfinite(fused.w) || !std::isfinite(fused.x)
        || !std::isfinite(fused.y) || !std::isfinite(fused.z)) {
        return;
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    orientation_ = glm::normalize(fused);
}

} // namespace dk2vr
