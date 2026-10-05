#include "OrientationFilter.hpp"

#include <algorithm>
#include <cmath>

namespace dk2vr {
namespace {

// Standard gravity, and the band around it within which an accelerometer
// reading is treated as pure gravity rather than head motion.
constexpr double kGravity = 9.80665;
constexpr double kGravityToleranceLow = 8.8;
constexpr double kGravityToleranceHigh = 10.8;

} // namespace

Quat multiply(const Quat& a, const Quat& b)
{
    return Quat {
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quat normalize(const Quat& q)
{
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(n > 0.0) || !std::isfinite(n)) {
        return Quat {};
    }
    return Quat {q.w / n, q.x / n, q.y / n, q.z / n};
}

Quat conjugate(const Quat& q)
{
    return Quat {q.w, -q.x, -q.y, -q.z};
}

Vec3 rotate(const Quat& q, const Vec3& v)
{
    // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    const Vec3 u {q.x, q.y, q.z};
    const Vec3 t {
        u.y * v.z - u.z * v.y + q.w * v.x,
        u.z * v.x - u.x * v.z + q.w * v.y,
        u.x * v.y - u.y * v.x + q.w * v.z};
    return Vec3 {
        v.x + 2.0 * (u.y * t.z - u.z * t.y),
        v.y + 2.0 * (u.z * t.x - u.x * t.z),
        v.z + 2.0 * (u.x * t.y - u.y * t.x)};
}

double length(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double extractYaw(const Quat& q)
{
    // Rotation about +Y, the world up axis in OpenVR's coordinate system.
    return std::atan2(2.0 * (q.w * q.y + q.x * q.z),
        1.0 - 2.0 * (q.y * q.y + q.x * q.x));
}

void OrientationFilter::setGravityGain(const double gainPerSecond) noexcept
{
    gravityGain_ = std::clamp(gainPerSecond, 0.0, 10.0);
}

void OrientationFilter::integrateGyro(const Vec3& gyroRadPerSecond, const double dtSeconds)
{
    if (!(dtSeconds > 0.0) || !std::isfinite(dtSeconds)) {
        return;
    }

    const double halfX = gyroRadPerSecond.x * dtSeconds * 0.5;
    const double halfY = gyroRadPerSecond.y * dtSeconds * 0.5;
    const double halfZ = gyroRadPerSecond.z * dtSeconds * 0.5;
    const double halfAngleSquared = halfX * halfX + halfY * halfY + halfZ * halfZ;

    Quat delta;
    if (halfAngleSquared > 1e-24) {
        const double halfAngle = std::sqrt(halfAngleSquared);
        const double s = std::sin(halfAngle) / halfAngle;
        delta = Quat {std::cos(halfAngle), halfX * s, halfY * s, halfZ * s};
    } else {
        delta = Quat {1.0, halfX, halfY, halfZ};
    }

    q_ = normalize(multiply(q_, delta));
}

void OrientationFilter::applyGravity(const Vec3& accelMetersPerSecond2, const double dtSeconds)
{
    if (!(gravityGain_ > 0.0) || !(dtSeconds > 0.0)) {
        return;
    }

    const double magnitude = length(accelMetersPerSecond2);
    if (!std::isfinite(magnitude) || magnitude < kGravityToleranceLow
        || magnitude > kGravityToleranceHigh) {
        // The head is accelerating; the reading is not gravity alone.
        return;
    }

    // At rest the accelerometer reads +g along the headset's up axis, so this
    // is the body-frame estimate of world up.
    const Vec3 bodyUp {
        accelMetersPerSecond2.x / magnitude,
        accelMetersPerSecond2.y / magnitude,
        accelMetersPerSecond2.z / magnitude};

    // Where the current estimate thinks that direction points in the world.
    const Vec3 worldUp = rotate(q_, bodyUp);

    // Rotation that would take worldUp onto +Y, i.e. cross(worldUp, +Y).
    // Its axis has no Y component by construction, so rotating about it
    // changes pitch and roll but never yaw.
    const Vec3 axis {-worldUp.z, 0.0, worldUp.x};
    const double axisLength = std::sqrt(axis.x * axis.x + axis.z * axis.z);
    if (!(axisLength > 1e-9)) {
        return; // already aligned, or pointing straight down
    }

    // atan2 rather than asin so an upside-down headset still gets the angle
    // on the correct side of 90 degrees.
    const double angle = std::atan2(axisLength, worldUp.y);
    const double correction = std::clamp(angle * gravityGain_ * dtSeconds, 0.0, angle);
    if (!std::isfinite(correction) || correction < 1e-12) {
        return;
    }

    const double half = correction * 0.5;
    const double s = std::sin(half) / axisLength;
    const Quat nudge {std::cos(half), axis.x * s, 0.0, axis.z * s};

    // Pre-multiply: the correction is expressed in the world frame.
    q_ = normalize(multiply(nudge, q_));
    gravityLocked_ = true;
}

void OrientationFilter::recenter()
{
    const double yaw = extractYaw(q_);
    const double half = -yaw * 0.5;
    calibration_ = Quat {std::cos(half), 0.0, std::sin(half), 0.0};
}

void OrientationFilter::setYawOffset(const double radians) noexcept
{
    if (std::isfinite(radians)) {
        yawOffset_ = radians;
    }
}

void OrientationFilter::reset()
{
    q_ = Quat {};
    calibration_ = Quat {};
    gravityLocked_ = false;
    // The manual offset deliberately survives a reset: it describes how the
    // headset sits relative to the game's idea of forward, which does not
    // change just because the tracker was reconnected.
}

Quat OrientationFilter::orientation() const
{
    const double half = yawOffset_ * 0.5;
    const Quat offset {std::cos(half), 0.0, std::sin(half), 0.0};
    // Both are world-frame rotations, so they pre-multiply the estimate.
    return normalize(multiply(offset, multiply(calibration_, q_)));
}

} // namespace dk2vr
