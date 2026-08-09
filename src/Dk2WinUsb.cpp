#include "Dk2WinUsb.hpp"

#include "Logger.hpp"

#include <Windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usbiodef.h>
#include <hidsdi.h>
#include <hidapi.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace dk2vr {
namespace {

// {A5DCBF10-6530-11D2-901F-00C04FB951ED} is GUID_DEVINTERFACE_USB_DEVICE
// (enumerates every USB interface, WinUSB or HID-class).
const GUID kUsbDeviceInterfaceGuid = {
    0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

constexpr std::uint16_t kOculusVendorId = 0x2833;
constexpr std::uint16_t kDk2ProductIds[] = {0x0021, 0x2021};
constexpr std::uint8_t kDk2ImuEndpoint = 0x81;

bool utf8FromWide(const std::wstring& source, std::string& destination)
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

std::string Dk2WinUsb::wideToUtf8(const std::wstring& source)
{
    std::string out;
    utf8FromWide(source, out);
    return out;
}

std::string Dk2WinUsb::narrowToUtf8(const char* source)
{
    if (source == nullptr) {
        return {};
    }
    return std::string(source);
}

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
    if (connectWinUsb()) {
        return true;
    }
    if (connectHidApi()) {
        return true;
    }
    return false;
}

bool Dk2WinUsb::connectWinUsb()
{
    devicePath_.clear();
    deviceHandle_ = nullptr;
    winUsbHandle_ = nullptr;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &kUsbDeviceInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        log::warning("Dk2WinUsb(WinUSB): SetupDiGetClassDevs basarisiz oldu, hata=" + std::to_string(err));
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData {};
    interfaceData.cbSize = sizeof(interfaceData);

    int enumeratedCount = 0;
    int dk2CandidateCount = 0;
    bool found = false;
    for (int deviceIndex = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &kUsbDeviceInterfaceGuid, deviceIndex, &interfaceData);
         ++deviceIndex) {
        ++enumeratedCount;
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

        log::info("Dk2WinUsb(WinUSB): USB aygit #" + std::to_string(deviceIndex)
            + " yol=" + wideToUtf8(detailData->DevicePath)
            + " VID=0x" + std::to_string(descriptor.idVendor)
            + " PID=0x" + std::to_string(descriptor.idProduct));

        const bool vendorMatch = descriptor.idVendor == kOculusVendorId;
        const bool productMatch = std::find(std::begin(kDk2ProductIds), std::end(kDk2ProductIds),
            descriptor.idProduct) != std::end(kDk2ProductIds);
        if (vendorMatch && productMatch) {
            deviceHandle_ = deviceHandle;
            winUsbHandle_ = winUsbHandle;
            devicePath_ = wideToUtf8(detailData->DevicePath);
            log::info("Dk2WinUsb(WinUSB): DK2 bulundu, VID=0x2833 PID=0x"
                + std::to_string(descriptor.idProduct) + " yol=" + devicePath_);
            found = true;
            ++dk2CandidateCount;
            break;
        }
        if (descriptor.idVendor == kOculusVendorId) {
            ++dk2CandidateCount;
        }

        WinUsb_Free(winUsbHandle);
        CloseHandle(deviceHandle);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (enumeratedCount == 0) {
        log::warning("Dk2WinUsb(WinUSB): SetupAPI HICBIR USB cihazi bulamadi. "
            "DK2 muhtemelen farkli bir aygit sinifinda (HIDClass, I2C, ozel sürücü). "
            "Windows Aygıt Yöneticisi'nde DK2'yi kontrol edin.");
    } else {
        log::info("Dk2WinUsb(WinUSB): Toplam " + std::to_string(enumeratedCount)
            + " USB cihaz listelendi, Oculus VID'li " + std::to_string(dk2CandidateCount) + " adet.");
    }
    if (!found) {
        log::info("Dk2WinUsb(WinUSB): DK2 bulunamadi; hidapi yolu denenecek.");
        return false;
    }

    activeBackend_ = Dk2Backend::WinUsb;
    connected_ = true;
    stopRequested_ = false;
    readerThread_ = std::thread(&Dk2WinUsb::readerLoop, this);
    return true;
}

bool Dk2WinUsb::connectHidApi()
{
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr) {
        log::warning("Dk2WinUsb(hidapi): hid_enumerate bos dondu.");
        return false;
    }

    hid_device_info* match = nullptr;
    for (hid_device_info* current = devices; current != nullptr; current = current->next) {
        if (current->vendor_id != kOculusVendorId) {
            continue;
        }
        if (std::find(std::begin(kDk2ProductIds), std::end(kDk2ProductIds),
            current->product_id) == std::end(kDk2ProductIds)) {
            continue;
        }
        match = current;
        break;
    }

    if (match == nullptr) {
        log::info("Dk2WinUsb(hidapi): Oculus/Rift/DK2 isimli HID aygiti bulunamadi.");
        hid_free_enumeration(devices);
        return false;
    }

    hid_device* handle = hid_open_path(match->path);
    hid_free_enumeration(devices);
    if (handle == nullptr) {
        const wchar_t* err = hid_error(nullptr);
        log::warning(std::string("Dk2WinUsb(hidapi): hid_open_path basarisiz oldu: ")
            + (err != nullptr ? wideToUtf8(err) : std::string {}));
        return false;
    }

    hidHandle_ = handle;
    devicePath_ = narrowToUtf8(match->path);
    log::info("Dk2WinUsb(hidapi): DK2 acildi, yol=" + devicePath_);
    activeBackend_ = Dk2Backend::HidApi;
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
    if (hidHandle_ != nullptr) {
        hid_close(static_cast<hid_device*>(hidHandle_));
        hidHandle_ = nullptr;
    }
    devicePath_.clear();
    connected_ = false;
    activeBackend_ = Dk2Backend::None;
}

bool Dk2WinUsb::isConnected() const noexcept
{
    return connected_;
}

const std::string& Dk2WinUsb::devicePath() const noexcept
{
    return devicePath_;
}

Dk2Backend Dk2WinUsb::backend() const noexcept
{
    return activeBackend_;
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
    std::uint8_t buffer[64] {};
    while (!stopRequested_) {
        if (activeBackend_ == Dk2Backend::WinUsb && winUsbHandle_ != nullptr) {
            unsigned long bytesRead = 0;
            const BOOL ok = WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(winUsbHandle_),
                kDk2ImuEndpoint, buffer, sizeof(buffer), &bytesRead, nullptr);
            if (!ok) {
                const DWORD error = GetLastError();
                if (error != ERROR_SEM_TIMEOUT && error != ERROR_IO_INCOMPLETE
                    && error != WAIT_TIMEOUT) {
                    log::warning("Dk2WinUsb: WinUsb_ReadPipe basarisiz oldu, hata="
                        + std::to_string(error));
                    break;
                }
            } else if (bytesRead > 0) {
                parseImuPacket(buffer, bytesRead);
            }
        } else if (activeBackend_ == Dk2Backend::HidApi && hidHandle_ != nullptr) {
            const int bytesRead = hid_read(static_cast<hid_device*>(hidHandle_), buffer, sizeof(buffer));
            if (bytesRead > 0) {
                parseImuPacket(buffer, static_cast<std::size_t>(bytesRead));
            } else if (bytesRead < 0) {
                const wchar_t* err = hid_error(static_cast<hid_device*>(hidHandle_));
                log::warning(std::string("Dk2WinUsb: hid_read basarisiz oldu: ")
                    + (err != nullptr ? wideToUtf8(err) : std::string {}));
                break;
            }
        } else {
            break;
        }
        Sleep(1);
    }
    log::info("Dk2WinUsb: okuyucu dongusu sona erdi.");
}

void Dk2WinUsb::parseImuPacket(const std::uint8_t* data, const std::size_t size)
{
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
