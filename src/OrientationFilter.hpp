// Orientation estimation from the DK2's gyroscope and accelerometer.
//
// Integrating the gyroscope alone drifts: within a couple of minutes the
// horizon visibly tilts. The accelerometer sees gravity whenever the head is
// roughly still, which pins down pitch and roll. Yaw has no such reference
// (the magnetometer is not read), so it keeps drifting slowly; recentring (R)
// resets it.
//
// Shared with the oculus-dk2-steam-plugin SteamVR driver (core/), which uses
// the same coordinate system: +Y up, -Z forward.
//
// Pure math, no I/O, so the unit tests can drive it with synthetic samples.
#pragma once

#include <cstdint>

namespace dk2vr {

struct Quat {
    double w {1.0};
    double x {0.0};
    double y {0.0};
    double z {0.0};
};

struct Vec3 {
    double x {0.0};
    double y {0.0};
    double z {0.0};
};

Quat multiply(const Quat& a, const Quat& b);
Quat normalize(const Quat& q);
Quat conjugate(const Quat& q);
Vec3 rotate(const Quat& q, const Vec3& v);
double length(const Vec3& v);

// Rotation about the world up axis, in radians.
double extractYaw(const Quat& q);

class OrientationFilter {
public:
    // How strongly gravity pulls the estimate back each second. 0 disables
    // the correction and leaves pure gyro integration.
    void setGravityGain(double gainPerSecond) noexcept;

    // Rotates the estimate by the measured angular velocity. Uses the
    // exponential map rather than a small-angle approximation so fast head
    // turns do not lose magnitude.
    void integrateGyro(const Vec3& gyroRadPerSecond, double dtSeconds);

    // Nudges pitch and roll so the measured acceleration lines up with world
    // up. Ignored while the headset is accelerating, because then the reading
    // is not gravity. Never touches yaw.
    void applyGravity(const Vec3& accelMetersPerSecond2, double dtSeconds);

    // Cancels the current yaw so the user faces forward. Pitch and roll are
    // left alone: recentring while looking down should not tilt the horizon.
    void recenter();

    // A fixed rotation applied after recentring, so "forward" can be aimed by
    // hand. Without a camera there is no absolute yaw reference, so when a
    // game decides its own forward the two can disagree by any angle; this is
    // the correction, and recentring returns to it rather than discarding it.
    void setYawOffset(double radians) noexcept;
    double yawOffset() const noexcept { return yawOffset_; }

    void reset();

    // Orientation with the recentring applied. This is what the HMD reports.
    Quat orientation() const;

    // Orientation before recentring, for diagnostics.
    Quat rawOrientation() const noexcept { return q_; }

    bool hasGravityLock() const noexcept { return gravityLocked_; }

private:
    Quat q_ {};
    Quat calibration_ {};
    double yawOffset_ {0.0};
    // Roughly a one second time constant for the pitch/roll correction.
    double gravityGain_ {1.0};
    bool gravityLocked_ {false};
};

} // namespace dk2vr
