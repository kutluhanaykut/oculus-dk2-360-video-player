// Decoder for the Oculus DK1/DK2 tracker's interrupt-endpoint reports.
//
// Both generations pack three 21-bit signed values into 8 bytes, and send one
// or more accel+gyro sample pairs per report. The layouts are:
//
//   DK2 (report id 0x0B)
//     [0]      report id
//     [1..2]   last command id
//     [3]      sample count
//     [4..5]   samples since start
//     [6..7]   temperature
//     [8..11]  timestamp, 32-bit microseconds
//     [12..]   samples, 16 bytes each (accel 8 + gyro 8), at most 2
//     [..]     magnetometer, 3 x int16
//
//   DK1 (report id 0x01)
//     [0]      report id
//     [1]      sample count
//     [2..3]   timestamp, 16-bit milliseconds
//     [4..5]   last command id
//     [6..7]   temperature
//     [8..]    samples, 16 bytes each, at most 3
//     [..]     magnetometer, 3 x int16
//
// Pure decoding, no I/O: the unit tests feed it recorded byte arrays.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dk2vr {

// Raw values are scaled by 1e-4 to reach physical units, matching OpenHMD.
inline constexpr double kGyroScaleRadPerSecond = 0.0001;
inline constexpr double kAccelScaleMetersPerSecond2 = 0.0001;

// The DK2 tracker emits one sample per millisecond.
inline constexpr double kSampleIntervalSeconds = 0.001;

inline constexpr std::size_t kMaxSamplesPerPacket = 3;

struct ImuSample {
    std::int32_t accel[3] {0, 0, 0};
    std::int32_t gyro[3] {0, 0, 0};
};

struct ImuPacket {
    bool valid {false};
    bool isDk2Format {false};
    std::uint8_t sampleCount {0};
    // Timestamp of the most recent sample, in microseconds.
    std::uint64_t timestampMicros {0};
    std::uint16_t temperatureRaw {0};
    std::array<ImuSample, kMaxSamplesPerPacket> samples {};
};

// Unpacks the three 21-bit signed values the Rift packs into 8 bytes.
void decodeRiftVector(const std::uint8_t* buffer, std::int32_t out[3]);

// Returns a packet with valid == false when the report is not an IMU report,
// or when it is shorter than the samples it claims to carry. The sample count
// is clamped to what actually fits in `size`, which is what keeps this from
// reading past the end of a short transfer.
ImuPacket parseImuPacket(const std::uint8_t* data, std::size_t size);

} // namespace dk2vr
