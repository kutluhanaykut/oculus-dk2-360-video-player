#include "HmdManager.hpp"

#include "Logger.hpp"

#include <openhmd.h>

#include <algorithm>
#include <cmath>

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

HmdManager::~HmdManager()
{
    shutdown();
}

bool HmdManager::initialize(std::string& error)
{
    shutdown();
    context_ = ohmd_ctx_create();
    if (context_ == nullptr) {
        error = "OpenHMD context olusturulamadi.";
        lastError_ = error;
        return false;
    }

    const int count = ohmd_ctx_probe(context_);
    if (count < 0) {
        error = safeString(ohmd_ctx_get_error(context_));
        if (error.empty()) {
            error = "OpenHMD aygit taramasi basarisiz.";
        }
        lastError_ = error;
        return false;
    }

    int preferredVectorIndex = -1;
    for (int listIndex = 0; listIndex < count; ++listIndex) {
        int deviceClass = -1;
        int flags = 0;
        if (ohmd_list_geti(context_, listIndex, OHMD_DEVICE_CLASS, &deviceClass) != OHMD_S_OK
            || deviceClass != OHMD_DEVICE_CLASS_HMD) {
            continue;
        }
        ohmd_list_geti(context_, listIndex, OHMD_DEVICE_FLAGS, &flags);
        if ((flags & OHMD_DEVICE_FLAGS_NULL_DEVICE) != 0) {
            continue;
        }

        HmdDeviceInfo info;
        info.listIndex = listIndex;
        info.vendor = safeString(ohmd_list_gets(context_, listIndex, OHMD_VENDOR));
        info.product = safeString(ohmd_list_gets(context_, listIndex, OHMD_PRODUCT));
        info.path = safeString(ohmd_list_gets(context_, listIndex, OHMD_PATH));
        info.flags = flags;
        devices_.push_back(std::move(info));

        const std::string& name = devices_.back().product;
        if (preferredVectorIndex < 0 || name.find("Rift DK2") != std::string::npos
            || name.find("DK2") != std::string::npos) {
            preferredVectorIndex = static_cast<int>(devices_.size()) - 1;
        }
    }

    if (preferredVectorIndex < 0) {
        error = "Oculus Rift DK2 bulunamadi. USB baglantisini ve OpenHMD erisimini kontrol edin.";
        lastError_ = error;
        log::warning(error);
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
        return false;
    }

    readDisplayInfo();
    recenterPending_ = true;
    lastError_.clear();
    log::info("OpenHMD jiroskop takibi baglandi: " + devices_[preferredVectorIndex].product);
    return true;
}

void HmdManager::shutdown()
{
    if (device_ != nullptr) {
        ohmd_close_device(device_);
        device_ = nullptr;
    }
    if (context_ != nullptr) {
        ohmd_ctx_destroy(context_);
        context_ = nullptr;
    }
    devices_.clear();
    activeDeviceVectorIndex_ = -1;
    rawOrientation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    calibration_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    orientation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
}

void HmdManager::update()
{
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
    rawOrientation_ = glm::normalize(candidate);
    if (recenterPending_) {
        calibration_ = glm::inverse(rawOrientation_);
        recenterPending_ = false;
    }
    orientation_ = glm::normalize(calibration_ * rawOrientation_);
}

void HmdManager::recenter()
{
    recenterPending_ = true;
}

bool HmdManager::connected() const noexcept
{
    return device_ != nullptr;
}

bool HmdManager::hasRotationalTracking() const noexcept
{
    const HmdDeviceInfo* info = activeDevice();
    return info != nullptr && (info->flags & OHMD_DEVICE_FLAGS_ROTATIONAL_TRACKING) != 0;
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

void HmdManager::readDisplayInfo()
{
    if (device_ == nullptr) {
        return;
    }
    ohmd_device_geti(device_, OHMD_SCREEN_HORIZONTAL_RESOLUTION,
        &displayInfo_.horizontalResolution);
    ohmd_device_geti(device_, OHMD_SCREEN_VERTICAL_RESOLUTION,
        &displayInfo_.verticalResolution);
    ohmd_device_getf(device_, OHMD_SCREEN_HORIZONTAL_SIZE,
        &displayInfo_.horizontalSizeMeters);
    ohmd_device_getf(device_, OHMD_SCREEN_VERTICAL_SIZE,
        &displayInfo_.verticalSizeMeters);
    ohmd_device_getf(device_, OHMD_LENS_HORIZONTAL_SEPARATION,
        &displayInfo_.lensSeparationMeters);
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
}

} // namespace dk2vr
