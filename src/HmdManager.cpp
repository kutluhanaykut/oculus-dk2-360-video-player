#include "HmdManager.hpp"

#include "Dk2WinUsb.hpp"
#include "Logger.hpp"

#include <openhmd.h>
#include <hidapi.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ios>
#include <sstream>

namespace dk2vr {
namespace {

std::string safeString(const char* value)
{
    return value != nullptr ? std::string(value) : std::string {};
}

bool finiteQuaternion(const glm::quat& value)
{
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z)
        && glm::length(value) > 0.01F;
}

} // namespace

HmdManager::HmdManager()
    : winUsbDk2_(std::make_unique<Dk2WinUsb>())
{
}

HmdManager::~HmdManager()
{
    shutdown();
}

bool HmdManager::initialize(std::string& error)
{
    shutdown();
    displayInfo_ = HmdDisplayInfo {};
    applyDefaults(displayInfo_);

    // OpenHMD implements the full DK2 protocol (feature reports, keep-alive,
    // sensor fusion) and is therefore preferred. Dk2WinUsb is a fallback that
    // only works when the DK2 is already streaming IMU data on its own.
    if (initializeOpenHmd(error)) {
        activeBackend_ = "OpenHMD";
        orientation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        recenterPending_ = true;
        lastError_.clear();
        return true;
    }

    if (winUsbDk2_->connect()) {
        switch (winUsbDk2_->backend()) {
        case Dk2Backend::WinUsb:
            activeBackend_ = "WinUSB (SetupAPI)";
            break;
        case Dk2Backend::HidApi:
            activeBackend_ = "hidapi";
            break;
        case Dk2Backend::Libusb:
            activeBackend_ = "libusb";
            break;
        default:
            activeBackend_ = "Bilinmeyen";
            break;
        }
        orientation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        recenterPending_ = true;
        lastError_.clear();
        log::info("DK2 jiroskopu " + activeBackend_ + " uzerinden acildi.");
        return true;
    }

    const std::string winUsbError = winUsbDk2_->lastError();

    activeBackend_ = "Yok";
    if (error.empty()) {
        error = winUsbError.empty()
            ? "DK2 USB izleme cihazi bulunamadi. DK2'nin USB kablosunun bagli "
              "oldugunu ve Windows Aygit Yoneticisi'nde gorundugunu dogrulayin. "
              "DK2 izleme cihazi WinUSB veya libusb-win32 surucusune bagli olmalidir "
              "(Oculus 0.8 runtime veya Zadig ile)."
            : winUsbError;
    }
    lastError_ = error;
    return false;
}

void HmdManager::shutdown()
{
    shutdownOpenHmd();
    if (winUsbDk2_ != nullptr) {
        winUsbDk2_->disconnect();
    }
    devices_.clear();
    activeDeviceVectorIndex_ = -1;
    orientation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    calibration_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    activeBackend_ = "Yok";
}

void HmdManager::applyDefaults(HmdDisplayInfo& display) const
{
    // DK2 published optics; the 0.8 driver does not change panel geometry.
    display.horizontalResolution = 1920;
    display.verticalResolution = 1080;
    display.horizontalSizeMeters = 0.12576F;
    display.verticalSizeMeters = 0.07074F;
    display.lensSeparationMeters = 0.0635F;
    display.fovDegrees = 100.0F;
    display.aspectRatio = 0.888889F;
    display.ipdMeters = 0.064F;
    display.distortion = {1.0F, 0.22F, 0.24F, 0.0F, 0.0F, 0.0F};
}

bool HmdManager::initializeOpenHmd(std::string& error)
{
    const int hidInitResult = hid_init();
    if (hidInitResult != 0) {
        log::warning("hid_init() sifir dondurmedi; OpenHMD tarama yine denenecek.");
    }
    context_ = ohmd_ctx_create();
    if (context_ == nullptr) {
        error = "OpenHMD context olusturulamadi.";
        lastError_ = error;
        return false;
    }

    int count = ohmd_ctx_probe(context_);
    if (count < 0) {
        error = safeString(ohmd_ctx_get_error(context_));
        if (error.empty()) {
            error = "OpenHMD aygit taramasi basarisiz.";
        }
        lastError_ = error;
        ohmd_ctx_destroy(context_);
        context_ = nullptr;
        return false;
    }
    if (count == 0) {
        SDL_Delay(200);
        count = ohmd_ctx_probe(context_);
    }
    log::info("OpenHMD ilk tarama tamamlandi: " + std::to_string(count) + " aygit.");

    int preferredVectorIndex = -1;
    for (int listIndex = 0; listIndex < count; ++listIndex) {
        int deviceClass = -1;
        int flags = 0;
        ohmd_list_geti(context_, listIndex, OHMD_DEVICE_CLASS, &deviceClass);
        ohmd_list_geti(context_, listIndex, OHMD_DEVICE_FLAGS, &flags);
        const std::string vendor = safeString(ohmd_list_gets(context_, listIndex, OHMD_VENDOR));
        const std::string product = safeString(ohmd_list_gets(context_, listIndex, OHMD_PRODUCT));
        const std::string path = safeString(ohmd_list_gets(context_, listIndex, OHMD_PATH));
        std::ostringstream deviceLine;
        deviceLine << "OpenHMD aygit #" << listIndex
                   << " sinif=" << deviceClass
                   << " bayrak=0x" << std::hex << std::setw(2)
                   << std::setfill('0') << (flags & 0xFFFF) << std::dec
                   << " uretici='" << vendor << "'"
                   << " urun='" << product << "'";
        log::info(deviceLine.str());

        const bool isHmd = deviceClass == OHMD_DEVICE_CLASS_HMD;
        const bool isRiftFamily = product.find("Rift") != std::string::npos
            || product.find("DK2") != std::string::npos
            || product.find("DK1") != std::string::npos
            || vendor.find("Oculus") != std::string::npos
            || vendor.find("Oculus VR") != std::string::npos;
        if (!isHmd && !isRiftFamily) {
            continue;
        }
        if ((flags & OHMD_DEVICE_FLAGS_NULL_DEVICE) != 0) {
            continue;
        }

        HmdDeviceInfo info;
        info.listIndex = listIndex;
        info.vendor = vendor;
        info.product = product;
        info.path = path;
        info.flags = flags;
        devices_.push_back(std::move(info));

        if (preferredVectorIndex < 0
            || product.find("DK2") != std::string::npos
            || product.find("Rift DK2") != std::string::npos
            || vendor.find("Oculus") != std::string::npos) {
            preferredVectorIndex = static_cast<int>(devices_.size()) - 1;
        }
    }

    if (preferredVectorIndex < 0) {
        std::string detail = "OpenHMD " + std::to_string(count)
            + " aygit gordu; hicbirinde Oculus/Rift/DK2 ismi yok. "
              "Windows'un DK2 USB aygitini WinUSB veya libusb-win32 ile eslediginden "
              "emin olun (hidapi Windows HID surucusu DK2 izleme cihazini acamayabilir).";
        lastError_ = detail;
        log::warning(detail);
        ohmd_ctx_destroy(context_);
        context_ = nullptr;
        return false;
    }

    activeDeviceVectorIndex_ = preferredVectorIndex;
    device_ = ohmd_list_open_device(context_, devices_[preferredVectorIndex].listIndex);
    if (device_ == nullptr) {
        error = safeString(ohmd_ctx_get_error(context_));
        if (error.empty()) {
            error = "DK2 OpenHMD ile acilamadi.";
        }
        lastError_ = error;
        ohmd_ctx_destroy(context_);
        context_ = nullptr;
        return false;
    }

    int horizontalResolution = 0;
    int verticalResolution = 0;
    ohmd_device_geti(device_, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &horizontalResolution);
    ohmd_device_geti(device_, OHMD_SCREEN_VERTICAL_RESOLUTION, &verticalResolution);
    if (horizontalResolution > 0 && verticalResolution > 0) {
        displayInfo_.horizontalResolution = horizontalResolution;
        displayInfo_.verticalResolution = verticalResolution;
    }
    ohmd_device_getf(device_, OHMD_SCREEN_HORIZONTAL_SIZE, &displayInfo_.horizontalSizeMeters);
    ohmd_device_getf(device_, OHMD_SCREEN_VERTICAL_SIZE, &displayInfo_.verticalSizeMeters);
    ohmd_device_getf(device_, OHMD_LENS_HORIZONTAL_SEPARATION, &displayInfo_.lensSeparationMeters);
    ohmd_device_getf(device_, OHMD_LEFT_EYE_FOV, &displayInfo_.fovDegrees);
    ohmd_device_getf(device_, OHMD_LEFT_EYE_ASPECT_RATIO, &displayInfo_.aspectRatio);
    ohmd_device_getf(device_, OHMD_EYE_IPD, &displayInfo_.ipdMeters);
    ohmd_device_getf(device_, OHMD_DISTORTION_K, displayInfo_.distortion.data());

    if (displayInfo_.horizontalResolution <= 0 || displayInfo_.verticalResolution <= 0) {
        displayInfo_.horizontalResolution = 1920;
        displayInfo_.verticalResolution = 1080;
    }
    if (!std::isfinite(displayInfo_.fovDegrees) || displayInfo_.fovDegrees < 60.0F
        || displayInfo_.fovDegrees > 140.0F) {
        displayInfo_.fovDegrees = 100.0F;
    }
    if (!std::isfinite(displayInfo_.ipdMeters) || displayInfo_.ipdMeters < 0.04F
        || displayInfo_.ipdMeters > 0.09F) {
        displayInfo_.ipdMeters = 0.064F;
    }

    openHmdActive_ = true;
    log::info("OpenHMD jiroskop takibi baglandi: " + devices_[preferredVectorIndex].product);
    return true;
}

void HmdManager::shutdownOpenHmd()
{
    if (device_ != nullptr) {
        ohmd_close_device(device_);
        device_ = nullptr;
    }
    if (context_ != nullptr) {
        ohmd_ctx_destroy(context_);
        context_ = nullptr;
    }
    hid_exit();
    openHmdActive_ = false;
}

void HmdManager::update()
{
    if (winUsbDk2_ != nullptr && winUsbDk2_->isConnected()) {
        orientation_ = winUsbDk2_->orientation();
        return;
    }
    if (context_ == nullptr || device_ == nullptr) {
        return;
    }
    ohmd_ctx_update(context_);

    float values[4] {0.0F, 0.0F, 0.0F, 1.0F};
    if (ohmd_device_getf(device_, OHMD_ROTATION_QUAT, values) != OHMD_S_OK) {
        lastError_ = safeString(ohmd_ctx_get_error(context_));
        return;
    }

    const glm::quat candidate(values[3], values[0], values[1], values[2]);
    if (!finiteQuaternion(candidate)) {
        return;
    }
    const glm::quat normalized = glm::normalize(candidate);
    if (recenterPending_) {
        calibration_ = glm::inverse(normalized);
        recenterPending_ = false;
    }
    orientation_ = glm::normalize(calibration_ * normalized);
}

void HmdManager::recenter()
{
    recenterPending_ = true;
    if (winUsbDk2_ != nullptr && winUsbDk2_->isConnected()) {
        winUsbDk2_->recenter();
        recenterPending_ = false;
    }
}

bool HmdManager::connected() const noexcept
{
    if (winUsbDk2_ != nullptr && winUsbDk2_->isConnected()) {
        return true;
    }
    return device_ != nullptr;
}

bool HmdManager::hasRotationalTracking() const noexcept
{
    return connected();
}

const glm::quat& HmdManager::orientation() const noexcept
{
    return orientation_;
}

const std::vector<HmdDeviceInfo>& HmdManager::devices() const noexcept
{
    return devices_;
}

const HmdDeviceInfo* HmdManager::activeDevice() const noexcept
{
    if (activeDeviceVectorIndex_ < 0
        || activeDeviceVectorIndex_ >= static_cast<int>(devices_.size())) {
        return nullptr;
    }
    return &devices_[static_cast<std::size_t>(activeDeviceVectorIndex_)];
}

const HmdDisplayInfo& HmdManager::displayInfo() const noexcept
{
    return displayInfo_;
}

const std::string& HmdManager::lastError() const noexcept
{
    return lastError_;
}

const std::string& HmdManager::activeBackend() const noexcept
{
    return activeBackend_;
}

} // namespace dk2vr
