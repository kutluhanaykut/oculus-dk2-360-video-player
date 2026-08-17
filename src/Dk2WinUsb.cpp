#include "Dk2WinUsb.hpp"

#include "Logger.hpp"

#include <Windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <usbiodef.h>
#include <hidsdi.h>
#include <hidapi.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
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
    lastError_.clear();
    if (connectWinUsb()) {
        return true;
    }
    if (connectHidApi()) {
        return true;
    }
    if (connectLibusb()) {
        return true;
    }
    if (lastError_.empty()) {
        lastError_ = "DK2 USB izleme cihazi bulunamadi. DK2'nin USB kablosunun "
            "bagli oldugunu ve Windows Aygit Yoneticisi'nde gorundugunu dogrulayin. "
            "DK2 izleme cihazi WinUSB veya libusb-win32 surucusune bagli olmalidir "
            "(Oculus 0.8 runtime veya Zadig ile).";
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
    bool dk2FoundButNotWinUsb = false;
    std::string dk2DriverHint;
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

        // Try to open the device. If it fails, the device may not be bound to
        // WinUSB yet; we still want to identify it by VID/PID via the registry.
        HANDLE deviceHandle = CreateFileW(detailData->DevicePath,
            GENERIC_WRITE | GENERIC_READ,
            FILE_SHARE_WRITE | FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            // Device could not be opened. Try to read its VID/PID from the
            // device path (e.g. "USB#VID_2833&PID_0021#...").
            const std::wstring path = detailData->DevicePath;
            const std::string utf8Path = wideToUtf8(path);
            const std::size_t vidPos = utf8Path.find("VID_");
            const std::size_t pidPos = utf8Path.find("PID_");
            if (vidPos != std::string::npos && pidPos != std::string::npos) {
                const std::string vidStr = utf8Path.substr(vidPos + 4, 4);
                const std::string pidStr = utf8Path.substr(pidPos + 4, 4);
                try {
                    const std::uint16_t vid = static_cast<std::uint16_t>(
                        std::stoul(vidStr, nullptr, 16));
                    const std::uint16_t pid = static_cast<std::uint16_t>(
                        std::stoul(pidStr, nullptr, 16));
                    if (vid == kOculusVendorId) {
                        ++dk2CandidateCount;
                        const bool productMatch = std::find(std::begin(kDk2ProductIds),
                            std::end(kDk2ProductIds), pid) != std::end(kDk2ProductIds);
                        if (productMatch) {
                            dk2FoundButNotWinUsb = true;
                            dk2DriverHint = "DK2 USB aygiti bulundu (VID=0x2833 PID=0x"
                                + pidStr + ") ancak WinUSB surucusune bagli degil. "
                                "Windows Aygit Yoneticisi'nde DK2 izleme cihazini bulup "
                                "'Surucu guncelle > Bilgisayarimdan sec > WinUSB' secin "
                                "veya Zadig ile WinUSB/libusb-win32 surucusu yukleyin.";
                            log::warning("Dk2WinUsb(WinUSB): " + dk2DriverHint);
                        }
                    }
                } catch (const std::exception&) {
                    // Ignore malformed VID/PID strings.
                }
            }
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
            "DK2 muhtemelen farkli bir aygit sinifinda (HIDClass, I2C, ozel surucu). "
            "Windows Aygit Yoneticisi'nde DK2'yi kontrol edin.");
    } else {
        log::info("Dk2WinUsb(WinUSB): Toplam " + std::to_string(enumeratedCount)
            + " USB cihaz listelendi, Oculus VID'li " + std::to_string(dk2CandidateCount) + " adet.");
    }
    if (!found) {
        if (dk2FoundButNotWinUsb) {
            log::warning("Dk2WinUsb(WinUSB): DK2 bulundu ancak WinUSB surucusu yok. "
                "hidapi yolu denenecek.");
        } else {
            log::info("Dk2WinUsb(WinUSB): DK2 bulunamadi; hidapi yolu denenecek.");
        }
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

bool Dk2WinUsb::connectLibusb()
{
    libusb_context* context = nullptr;
    if (libusb_init(&context) != LIBUSB_SUCCESS) {
        log::warning("Dk2WinUsb(libusb): libusb_init basarisiz oldu.");
        return false;
    }

    libusb_device** deviceList = nullptr;
    const ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) {
        log::warning("Dk2WinUsb(libusb): libusb_get_device_list basarisiz oldu.");
        libusb_exit(context);
        return false;
    }

    libusb_device_handle* foundHandle = nullptr;
    libusb_device* foundDevice = nullptr;
    int foundInterface = -1;
    int oculusCount = 0;

    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device* device = deviceList[i];
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId) {
            continue;
        }
        ++oculusCount;
        const bool productMatch = std::find(std::begin(kDk2ProductIds), std::end(kDk2ProductIds),
            descriptor.idProduct) != std::end(kDk2ProductIds);
        if (!productMatch) {
            continue;
        }

        log::info("Dk2WinUsb(libusb): Oculus aygit bulundu VID=0x"
            + std::to_string(descriptor.idVendor) + " PID=0x"
            + std::to_string(descriptor.idProduct));

        // Try to open the device and claim the interface that exposes the IMU
        // bulk endpoint (0x81). The DK2 tracking device has a single interface.
        libusb_device_handle* handle = nullptr;
        const int openResult = libusb_open(device, &handle);
        if (openResult != LIBUSB_SUCCESS) {
            log::warning("Dk2WinUsb(libusb): libusb_open basarisiz oldu (PID=0x"
                + std::to_string(descriptor.idProduct) + "), hata="
                + std::to_string(openResult) + " ("
                + libusb_error_name(openResult) + ")");
            continue;
        }

        // Detach the kernel driver if present (libusb-win32 / WinUSB).
        libusb_set_auto_detach_kernel_driver(handle, 1);

        libusb_config_descriptor* config = nullptr;
        if (libusb_get_active_config_descriptor(device, &config) == LIBUSB_SUCCESS && config != nullptr) {
            for (std::uint8_t iface = 0; iface < config->bNumInterfaces; ++iface) {
                const libusb_interface& interface = config->interface[iface];
                if (interface.num_altsetting == 0) {
                    continue;
                }
                const libusb_interface_descriptor& alt = interface.altsetting[0];
                bool hasImuEndpoint = false;
                for (std::uint8_t ep = 0; ep < alt.bNumEndpoints; ++ep) {
                    if (alt.endpoint[ep].bEndpointAddress == kDk2ImuEndpoint) {
                        hasImuEndpoint = true;
                        break;
                    }
                }
                if (!hasImuEndpoint) {
                    continue;
                }
                if (libusb_claim_interface(handle, iface) == LIBUSB_SUCCESS) {
                    foundHandle = handle;
                    foundDevice = device;
                    foundInterface = static_cast<int>(iface);
                    break;
                }
            }
            libusb_free_config_descriptor(config);
        }


        if (foundHandle == nullptr) {
            libusb_close(handle);
        } else {
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);

    if (foundHandle == nullptr) {
        log::info("Dk2WinUsb(libusb): Oculus VID'li " + std::to_string(oculusCount)
            + " aygit bulundu ancak hicbiri acilamadi.");
        libusb_exit(context);
        return false;
    }

    libusbHandle_ = foundHandle;
    libusbContext_ = context;
    libusbInterface_ = foundInterface;
    devicePath_ = "libusb:" + std::to_string(foundInterface);
    log::info("Dk2WinUsb(libusb): DK2 acildi, arayuz=" + std::to_string(foundInterface));

    // Configure the DK2 sensor: set packet interval and keep-alive interval.
    // The DK2 IMU interface is a HID device; sensor config is sent as a HID
    // feature report (SET_REPORT, report type = feature, report id = 0x02).
    //   [0] report id (0x02)
    //   [1..2] command_id
    //   [3] flags (0x0C = use calibration | auto calibration)
    //   [4] packet_interval (0x00 = default)
    //   [5..6] keep_alive_interval (10000 ms)
    {
        std::uint8_t config[7] {0x02, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x27};
        const int result = libusb_control_transfer(libusbHandle_,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x0302, static_cast<std::uint16_t>(libusbInterface_),
            config, sizeof(config), 1000);
        if (result < 0) {
            log::warning("Dk2WinUsb(libusb): sensor config gonderilemedi, hata="
                + std::to_string(result) + " (" + libusb_error_name(result) + ")");
        } else {
            log::info("Dk2WinUsb(libusb): sensor config gonderildi (" + std::to_string(result) + " bayt)");
        }
    }

    // Enable the DK2 LEDs (output report, report id = 0x0C). This is required
    // for the DK2 to start streaming IMU data.
    {
        std::uint8_t enableLeds[17] {
            0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5E, 0x01,
            0x1A, 0x41, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00};
        const int result = libusb_control_transfer(libusbHandle_,
            static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
            0x09, 0x020C, static_cast<std::uint16_t>(libusbInterface_),
            enableLeds, sizeof(enableLeds), 1000);
        if (result < 0) {
            log::warning("Dk2WinUsb(libusb): LED etkinlestirme gonderilemedi, hata="
                + std::to_string(result) + " (" + libusb_error_name(result) + ")");
        } else {
            log::info("Dk2WinUsb(libusb): LED etkinlestirme gonderildi (" + std::to_string(result) + " bayt)");
        }
    }

    activeBackend_ = Dk2Backend::Libusb;
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
    if (libusbHandle_ != nullptr) {
        if (libusbInterface_ >= 0) {
            libusb_release_interface(libusbHandle_, static_cast<int>(libusbInterface_));
        }
        libusb_close(libusbHandle_);
        libusbHandle_ = nullptr;
    }
    if (libusbContext_ != nullptr) {
        libusb_exit(libusbContext_);
        libusbContext_ = nullptr;
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

const std::string& Dk2WinUsb::lastError() const noexcept
{
    return lastError_;
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
        // Send keep-alive to the DK2 sensor periodically (every 10 seconds).
        if (activeBackend_ == Dk2Backend::Libusb && libusbHandle_ != nullptr) {
            const auto nowMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (lastKeepAliveMs_ == 0 || (nowMs - lastKeepAliveMs_) >= 10000) {
                // Keep-alive feature report (RIFT_CMD_KEEP_ALIVE = 8):
                //   [0] report id (0x08)
                //   [1..2] command_id
                //   [3..4] keep_alive_interval (10000 ms)
                std::uint8_t keepAlive[5] {0x08, 0x00, 0x00, 0x10, 0x27};
                const int result = libusb_control_transfer(libusbHandle_,
                    static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                        | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
                        | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT),
                    0x09, 0x0308, static_cast<std::uint16_t>(libusbInterface_),
                    keepAlive, sizeof(keepAlive), 1000);
                if (result < 0) {
                    log::warning("Dk2WinUsb(libusb): keep-alive gonderilemedi, hata="
                        + std::to_string(result) + " (" + libusb_error_name(result) + ")");
                }
                lastKeepAliveMs_ = nowMs;
            }
        }

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
        } else if (activeBackend_ == Dk2Backend::Libusb && libusbHandle_ != nullptr) {
            int transferred = 0;
            // The DK2 IMU interface is a HID device; IMU data arrives via
            // interrupt transfers (HID input reports), not bulk transfers.
            const int result = libusb_interrupt_transfer(libusbHandle_, kDk2ImuEndpoint,
                buffer, static_cast<int>(sizeof(buffer)), &transferred, 100);
            if (result == LIBUSB_SUCCESS && transferred > 0) {
                parseImuPacket(buffer, static_cast<std::size_t>(transferred));
            } else if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT
                && result != LIBUSB_ERROR_INTERRUPTED) {
                log::warning("Dk2WinUsb: libusb_interrupt_transfer basarisiz oldu, hata="
                    + std::to_string(result) + " (" + libusb_error_name(result) + ")");
                break;
            }
        } else {
            break;
        }

        Sleep(1);
    }
    log::info("Dk2WinUsb: okuyucu dongusu sona erdi.");
}

namespace {
// Decode 3 tightly packed 21-bit signed values from 8 bytes (DK1/DK2 IMU sample).
void decodeRiftSample(const std::uint8_t* buffer, std::int32_t out[3])
{
    const std::int32_t x = (static_cast<std::int32_t>(buffer[0]) << 24)
        | (static_cast<std::int32_t>(buffer[1]) << 16)
        | ((static_cast<std::int32_t>(buffer[2]) & 0xF8) << 8);
    const std::int32_t y = ((static_cast<std::int32_t>(buffer[2]) & 0x07) << 29)
        | (static_cast<std::int32_t>(buffer[3]) << 21)
        | (static_cast<std::int32_t>(buffer[4]) << 13)
        | ((static_cast<std::int32_t>(buffer[5]) & 0xC0) << 5);
    const std::int32_t z = ((static_cast<std::int32_t>(buffer[5]) & 0x3F) << 26)
        | (static_cast<std::int32_t>(buffer[6]) << 18)
        | (static_cast<std::int32_t>(buffer[7]) << 10);
    out[0] = x >> 11;
    out[1] = y >> 11;
    out[2] = z >> 11;
}
} // namespace

void Dk2WinUsb::parseImuPacket(const std::uint8_t* data, const std::size_t size)
{
    // DK2 sends IMU data with report ID 0x01 (DK1 format) or 0x0B (DK2 format).
    if (size < 26 || (data[0] != 0x01 && data[0] != 0x0B)) {
        return;
    }

    std::uint8_t numSamples = 0;
    std::uint64_t timestamp = 0;
    const std::uint8_t* samplePtr = nullptr;

    if (data[0] == 0x0B) {
        // DK2 format:
        //   [0] report id (0x0B)
        //   [1..2] last_command_id
        //   [3] num_samples
        //   [4..5] unused (nb_samples_since_start)
        //   [6..7] temperature
        //   [8..11] timestamp (32-bit, microseconds)
        //   then samples: accel(8) + gyro(8) per sample, up to 2 samples
        //   then mag(3 x 16-bit)
        numSamples = data[3];
        timestamp = static_cast<std::uint64_t>(data[8])
            | (static_cast<std::uint64_t>(data[9]) << 8)
            | (static_cast<std::uint64_t>(data[10]) << 16)
            | (static_cast<std::uint64_t>(data[11]) << 24);
        samplePtr = data + 12;
        if (numSamples > 2) {
            numSamples = 2;
        }
    } else {
        // DK1 format:
        //   [0] report id (0x01)
        //   [1] num_samples
        //   [2..3] timestamp (16-bit, milliseconds)
        //   [4..5] last_command_id
        //   [6..7] temperature
        //   then samples: accel(8) + gyro(8) per sample, up to 3 samples
        //   then mag(3 x 16-bit)
        numSamples = data[1];
        timestamp = static_cast<std::uint64_t>(data[2])
            | (static_cast<std::uint64_t>(data[3]) << 8);
        timestamp *= 1000; // DK1 timestamps are in milliseconds -> microseconds
        samplePtr = data + 8;
        if (numSamples > 3) {
            numSamples = 3;
        }
    }

    if (numSamples == 0 || samplePtr == nullptr) {
        return;
    }

    // Use the first sample's gyro data for integration.
    std::int32_t accel[3] {0, 0, 0};
    std::int32_t gyro[3] {0, 0, 0};
    decodeRiftSample(samplePtr, accel);
    decodeRiftSample(samplePtr + 8, gyro);

    // Convert raw values to physical units. OpenHMD uses a 0.0001 scale and
    // treats the result directly as radians per second for the gyro.
    const glm::vec3 gyroRadPerSec(
        static_cast<float>(gyro[0]) * 0.0001F,
        static_cast<float>(gyro[1]) * 0.0001F,
        static_cast<float>(gyro[2]) * 0.0001F);

    std::lock_guard<std::mutex> lock(stateMutex_);

    // Integrate gyro angular velocity over time to update orientation.
    if (haveLastImuTimestamp_ && timestamp > lastImuTimestamp_) {
        const double dtSeconds = static_cast<double>(timestamp - lastImuTimestamp_) / 1000000.0;
        if (dtSeconds > 0.0 && dtSeconds < 0.5) {
            const glm::quat delta = glm::quat(1.0F,
                gyroRadPerSec.x * static_cast<float>(dtSeconds) * 0.5F,
                gyroRadPerSec.y * static_cast<float>(dtSeconds) * 0.5F,
                gyroRadPerSec.z * static_cast<float>(dtSeconds) * 0.5F);
            orientation_ = glm::normalize(orientation_ * delta);
        }
    }

    lastImuTimestamp_ = timestamp;
    haveLastImuTimestamp_ = true;

    // Periodic debug log (every ~100 packets) to confirm IMU data is flowing.
    static std::uint32_t packetCount = 0;
    if ((++packetCount % 100) == 0) {
        log::info("Dk2WinUsb: IMU paketleri isleniyor, gyro(rad/s)=("
            + std::to_string(gyroRadPerSec.x) + ", "
            + std::to_string(gyroRadPerSec.y) + ", "
            + std::to_string(gyroRadPerSec.z) + ")");
    }
}

} // namespace dk2vr
