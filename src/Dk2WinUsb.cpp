#include "Dk2WinUsb.hpp"

#include "ImuPacket.hpp"
#include "Logger.hpp"

#include <Windows.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace dk2vr {
namespace {

constexpr std::uint16_t kOculusVendorId = 0x2833;
// The DK2 tracker. PID 0x2021 on the same vendor id is the hub built into the
// headset, which must never be opened or have its driver replaced.
constexpr std::uint16_t kDk2TrackerProductId = 0x0021;
constexpr std::uint8_t kDk2ImuEndpoint = 0x81;
// Upper bound for one blocking read so the reader thread can notice
// stopRequested_ and exit promptly.
constexpr int kReadTimeoutMs = 100;
// The config report asks the tracker to keep streaming for 10 s after each
// keep-alive; refreshing every 3 s leaves plenty of margin.
constexpr std::uint64_t kKeepAliveIntervalMs = 3000;

// HID SET_REPORT over a control transfer. wValue = (report type << 8) | id,
// where type 2 is an output report and type 3 a feature report.
constexpr std::uint8_t kSetReportRequestType =
    static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
    | static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE)
    | static_cast<std::uint8_t>(LIBUSB_ENDPOINT_OUT);
constexpr std::uint8_t kSetReportRequest = 0x09;

// Feature 0x02: flags 0x0C (use + auto calibration), default packet
// interval, keep-alive interval 10000 ms.
constexpr std::uint8_t kConfigReport[7] {0x02, 0x00, 0x00, 0x0C, 0x00, 0x10, 0x27};
// Output 0x0C: tracking LEDs. The DK2 only streams IMU reports once enabled.
constexpr std::uint8_t kLedReport[17] {
    0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x5E, 0x01,
    0x1A, 0x41, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00};
// Feature 0x08: keep-alive, 10000 ms.
constexpr std::uint8_t kKeepAliveReport[5] {0x08, 0x00, 0x00, 0x10, 0x27};

std::uint64_t steadyMilliseconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string hex4(const unsigned value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << value;
    return stream.str();
}

} // namespace

std::string Dk2WinUsb::wideToUtf8(const std::wstring& source)
{
    if (source.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, source.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string destination(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, source.c_str(), -1, destination.data(), length, nullptr, nullptr);
    return destination;
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
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        filter_.reset();
        haveLastImuTimestamp_ = false;
        lastImuTimestamp_ = 0;
        packetCount_ = 0;
    }
    lastKeepAliveMs_ = 0;

    // libusb first: with the tracker on WinUSB (the setup the SteamVR dk2vr
    // driver also relies on) it is the only path that can open it.
    if (connectLibusb()) {
        return true;
    }
    if (connectHidApi()) {
        return true;
    }
    if (lastError_.empty()) {
        lastError_ = "DK2 USB izleme cihazi bulunamadi. DK2'nin USB kablosunun "
            "bagli oldugunu ve Windows Aygit Yoneticisi'nde 'Rift DK2' olarak "
            "gorundugunu dogrulayin.";
    }
    return false;
}

bool Dk2WinUsb::connectHidApi()
{
    hid_device_info* devices = hid_enumerate(kOculusVendorId, kDk2TrackerProductId);
    if (devices == nullptr) {
        log::info("Dk2WinUsb(hidapi): HID surucusunde DK2 izleme cihazi yok.");
        return false;
    }

    const std::string path = devices->path != nullptr ? devices->path : std::string {};
    hid_device* handle = hid_open_path(devices->path);
    hid_free_enumeration(devices);
    if (handle == nullptr) {
        const wchar_t* err = hid_error(nullptr);
        log::warning(std::string("Dk2WinUsb(hidapi): hid_open_path basarisiz oldu: ")
            + (err != nullptr ? wideToUtf8(err) : std::string {}));
        return false;
    }

    // hidapi expects the report id as the first byte, which these already are.
    if (hid_send_feature_report(handle, kConfigReport, sizeof(kConfigReport)) < 0) {
        log::warning("Dk2WinUsb(hidapi): sensor config gonderilemedi.");
    }
    if (hid_write(handle, kLedReport, sizeof(kLedReport)) < 0) {
        log::warning("Dk2WinUsb(hidapi): LED etkinlestirme gonderilemedi.");
    }

    hidHandle_ = handle;
    devicePath_ = path;
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
    int foundInterface = -1;
    bool sawTracker = false;

    for (ssize_t i = 0; i < deviceCount && foundHandle == nullptr; ++i) {
        libusb_device* device = deviceList[i];
        libusb_device_descriptor descriptor {};
        if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != kOculusVendorId
            || descriptor.idProduct != kDk2TrackerProductId) {
            continue;
        }
        sawTracker = true;
        log::info("Dk2WinUsb(libusb): DK2 izleme cihazi bulundu VID="
            + hex4(descriptor.idVendor) + " PID=" + hex4(descriptor.idProduct));

        libusb_device_handle* handle = nullptr;
        const int openResult = libusb_open(device, &handle);
        if (openResult != LIBUSB_SUCCESS) {
            log::warning("Dk2WinUsb(libusb): libusb_open basarisiz oldu, hata="
                + std::to_string(openResult) + " (" + libusb_error_name(openResult) + ")");
            continue;
        }

        libusb_set_auto_detach_kernel_driver(handle, 1);

        // Claim the interface that exposes the IMU interrupt endpoint (0x81).
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
                if (hasImuEndpoint && libusb_claim_interface(handle, iface) == LIBUSB_SUCCESS) {
                    foundHandle = handle;
                    foundInterface = static_cast<int>(iface);
                    break;
                }
            }
            libusb_free_config_descriptor(config);
        }

        if (foundHandle == nullptr) {
            libusb_close(handle);
        }
    }

    libusb_free_device_list(deviceList, 1);

    if (foundHandle == nullptr) {
        log::info(sawTracker
            ? "Dk2WinUsb(libusb): DK2 izleme cihazi acilamadi (WinUSB surucusunde olmayabilir)."
            : "Dk2WinUsb(libusb): DK2 izleme cihazi (PID 0x0021) bulunamadi.");
        libusb_exit(context);
        return false;
    }

    libusbHandle_ = foundHandle;
    libusbContext_ = context;
    libusbInterface_ = foundInterface;
    devicePath_ = "libusb:" + std::to_string(foundInterface);
    log::info("Dk2WinUsb(libusb): DK2 acildi, arayuz=" + std::to_string(foundInterface));

    const auto setReport = [this](const std::uint16_t value, const std::uint8_t* payload,
                               const std::uint16_t length, const char* what) {
        // libusb takes a mutable buffer even for OUT transfers.
        std::uint8_t buffer[32] {};
        std::memcpy(buffer, payload, length);
        const int result = libusb_control_transfer(libusbHandle_, kSetReportRequestType,
            kSetReportRequest, value, static_cast<std::uint16_t>(libusbInterface_),
            buffer, length, 1000);
        if (result < 0) {
            log::warning(std::string("Dk2WinUsb(libusb): ") + what + " gonderilemedi, hata="
                + std::to_string(result) + " (" + libusb_error_name(result) + ")");
        }
    };
    setReport(0x0302, kConfigReport, sizeof(kConfigReport), "sensor config");
    setReport(0x020C, kLedReport, sizeof(kLedReport), "LED etkinlestirme");

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
    if (hidHandle_ != nullptr) {
        hid_close(hidHandle_);
        hidHandle_ = nullptr;
    }
    if (libusbHandle_ != nullptr) {
        if (libusbInterface_ >= 0) {
            libusb_release_interface(libusbHandle_, libusbInterface_);
        }
        libusb_close(libusbHandle_);
        libusbHandle_ = nullptr;
    }
    libusbInterface_ = -1;
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
    filter_.recenter();
}

glm::quat Dk2WinUsb::orientation() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    const Quat q = filter_.orientation();
    return glm::quat(static_cast<float>(q.w), static_cast<float>(q.x),
        static_cast<float>(q.y), static_cast<float>(q.z));
}

void Dk2WinUsb::readerLoop()
{
    std::uint8_t buffer[64] {};
    while (!stopRequested_) {
        const std::uint64_t nowMs = steadyMilliseconds();
        const bool keepAliveDue = lastKeepAliveMs_ == 0
            || (nowMs - lastKeepAliveMs_) >= kKeepAliveIntervalMs;

        if (activeBackend_ == Dk2Backend::Libusb && libusbHandle_ != nullptr) {
            if (keepAliveDue) {
                std::uint8_t keepAlive[sizeof(kKeepAliveReport)] {};
                std::memcpy(keepAlive, kKeepAliveReport, sizeof(keepAlive));
                const int result = libusb_control_transfer(libusbHandle_, kSetReportRequestType,
                    kSetReportRequest, 0x0308, static_cast<std::uint16_t>(libusbInterface_),
                    keepAlive, sizeof(keepAlive), 1000);
                if (result < 0) {
                    log::warning("Dk2WinUsb(libusb): keep-alive gonderilemedi, hata="
                        + std::to_string(result) + " (" + libusb_error_name(result) + ")");
                }
                lastKeepAliveMs_ = nowMs;
            }

            // The DK2 IMU interface is a HID interface; reports arrive as
            // interrupt transfers.
            int transferred = 0;
            const int result = libusb_interrupt_transfer(libusbHandle_, kDk2ImuEndpoint,
                buffer, static_cast<int>(sizeof(buffer)), &transferred, kReadTimeoutMs);
            if (result == LIBUSB_SUCCESS && transferred > 0) {
                handlePacket(buffer, static_cast<std::size_t>(transferred));
            } else if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT
                && result != LIBUSB_ERROR_INTERRUPTED) {
                log::warning("Dk2WinUsb: libusb_interrupt_transfer basarisiz oldu, hata="
                    + std::to_string(result) + " (" + libusb_error_name(result) + ")");
                break;
            }
        } else if (activeBackend_ == Dk2Backend::HidApi && hidHandle_ != nullptr) {
            if (keepAliveDue) {
                if (hid_send_feature_report(hidHandle_, kKeepAliveReport,
                        sizeof(kKeepAliveReport)) < 0) {
                    log::warning("Dk2WinUsb(hidapi): keep-alive gonderilemedi.");
                }
                lastKeepAliveMs_ = nowMs;
            }

            // hid_read blocks indefinitely; a timeout keeps disconnect() responsive.
            const int bytesRead = hid_read_timeout(hidHandle_, buffer, sizeof(buffer), kReadTimeoutMs);
            if (bytesRead > 0) {
                handlePacket(buffer, static_cast<std::size_t>(bytesRead));
            } else if (bytesRead < 0) {
                const wchar_t* err = hid_error(hidHandle_);
                log::warning(std::string("Dk2WinUsb: hid_read basarisiz oldu: ")
                    + (err != nullptr ? wideToUtf8(err) : std::string {}));
                break;
            }
        } else {
            break;
        }
    }
    log::info("Dk2WinUsb: okuyucu dongusu sona erdi.");
}

void Dk2WinUsb::handlePacket(const std::uint8_t* data, const std::size_t size)
{
    const ImuPacket packet = parseImuPacket(data, size);
    if (!packet.valid) {
        return;
    }

    std::lock_guard<std::mutex> lock(stateMutex_);

    // The timestamp belongs to the last sample in the report, so the interval
    // covers all of them; split it evenly so every sample is integrated.
    double totalSeconds = 0.0;
    if (haveLastImuTimestamp_ && packet.timestampMicros > lastImuTimestamp_) {
        totalSeconds = static_cast<double>(packet.timestampMicros - lastImuTimestamp_) / 1000000.0;
    }
    lastImuTimestamp_ = packet.timestampMicros;
    haveLastImuTimestamp_ = true;

    // A wrapped counter, a dropped report or a stall shows up as an
    // implausible interval; fall back to the nominal 1 kHz sample rate.
    if (!(totalSeconds > 0.0) || totalSeconds > 0.25) {
        totalSeconds = kSampleIntervalSeconds * packet.sampleCount;
    }
    const double dtPerSample = totalSeconds / packet.sampleCount;

    for (std::size_t index = 0; index < packet.sampleCount; ++index) {
        const ImuSample& sample = packet.samples[index];
        const Vec3 gyro {
            static_cast<double>(sample.gyro[0]) * kGyroScaleRadPerSecond,
            static_cast<double>(sample.gyro[1]) * kGyroScaleRadPerSecond,
            static_cast<double>(sample.gyro[2]) * kGyroScaleRadPerSecond};
        const Vec3 accel {
            static_cast<double>(sample.accel[0]) * kAccelScaleMetersPerSecond2,
            static_cast<double>(sample.accel[1]) * kAccelScaleMetersPerSecond2,
            static_cast<double>(sample.accel[2]) * kAccelScaleMetersPerSecond2};
        filter_.integrateGyro(gyro, dtPerSample);
        filter_.applyGravity(accel, dtPerSample);
    }

    // Periodic log (every ~1000 reports) to confirm IMU data is flowing.
    if ((++packetCount_ % 1000) == 0) {
        log::info(std::string("Dk2WinUsb: IMU paketleri isleniyor, yercekimi kilidi ")
            + (filter_.hasGravityLock() ? "var." : "henuz yok."));
    }
}

} // namespace dk2vr
