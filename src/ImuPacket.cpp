#include "ImuPacket.hpp"

#include <algorithm>

namespace dk2vr {
namespace {

constexpr std::uint8_t kReportDk1 = 0x01;
constexpr std::uint8_t kReportDk2 = 0x0B;

constexpr std::size_t kSampleSize = 16; // accel(8) + gyro(8)
constexpr std::size_t kDk2HeaderSize = 12;
constexpr std::size_t kDk1HeaderSize = 8;

} // namespace

void decodeRiftVector(const std::uint8_t* buffer, std::int32_t out[3])
{
    // Three 21-bit two's-complement values packed big-endian across 8 bytes.
    // Each is shifted up to the top of a 32-bit word and arithmetic-shifted
    // back down so the sign bit lands in the right place.
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

ImuPacket parseImuPacket(const std::uint8_t* data, const std::size_t size)
{
    ImuPacket packet;
    if (data == nullptr || size < 2) {
        return packet;
    }
    if (data[0] != kReportDk1 && data[0] != kReportDk2) {
        return packet;
    }

    std::size_t headerSize = 0;
    std::uint8_t declaredSamples = 0;
    std::uint8_t formatMaxSamples = 0;

    if (data[0] == kReportDk2) {
        if (size < kDk2HeaderSize) {
            return packet;
        }
        packet.isDk2Format = true;
        headerSize = kDk2HeaderSize;
        declaredSamples = data[3];
        formatMaxSamples = 2;
        packet.temperatureRaw = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[6])
            | (static_cast<std::uint16_t>(data[7]) << 8));
        packet.timestampMicros = static_cast<std::uint64_t>(data[8])
            | (static_cast<std::uint64_t>(data[9]) << 8)
            | (static_cast<std::uint64_t>(data[10]) << 16)
            | (static_cast<std::uint64_t>(data[11]) << 24);
    } else {
        if (size < kDk1HeaderSize) {
            return packet;
        }
        packet.isDk2Format = false;
        headerSize = kDk1HeaderSize;
        declaredSamples = data[1];
        formatMaxSamples = 3;
        packet.temperatureRaw = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[6])
            | (static_cast<std::uint16_t>(data[7]) << 8));
        // DK1 counts milliseconds; normalise to microseconds so callers do
        // not have to care which generation produced the packet.
        packet.timestampMicros = (static_cast<std::uint64_t>(data[2])
                                     | (static_cast<std::uint64_t>(data[3]) << 8))
            * 1000U;
    }

    // Clamp to the format's maximum, to what the report claims, and to what
    // the transfer actually delivered. The last clamp is the one that keeps a
    // short read from walking off the end of the buffer.
    const std::size_t bytesAvailable = size > headerSize ? size - headerSize : 0;
    const auto fitsInBuffer = static_cast<std::uint8_t>(
        (std::min)(bytesAvailable / kSampleSize, static_cast<std::size_t>(255)));

    packet.sampleCount = (std::min)({declaredSamples, formatMaxSamples, fitsInBuffer,
        static_cast<std::uint8_t>(kMaxSamplesPerPacket)});
    if (packet.sampleCount == 0) {
        return packet;
    }

    for (std::size_t index = 0; index < packet.sampleCount; ++index) {
        const std::uint8_t* base = data + headerSize + index * kSampleSize;
        decodeRiftVector(base, packet.samples[index].accel);
        decodeRiftVector(base + 8, packet.samples[index].gyro);
    }

    packet.valid = true;
    return packet;
}

} // namespace dk2vr
