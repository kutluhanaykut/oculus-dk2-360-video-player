#include "SpatialMetadata.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace dk2vr {
namespace {

constexpr std::size_t kMaxMoovBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMatroskaHeaderBytes = 8U * 1024U * 1024U;

std::uint32_t readBe32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16)
        | (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

// Offset of the first occurrence of needle in [data, data + size), or npos.
std::size_t find(const std::uint8_t* data, const std::size_t size, const std::string_view needle,
    const std::size_t from = 0)
{
    if (from >= size || needle.size() > size - from) {
        return std::string_view::npos;
    }
    const auto* begin = data + from;
    const auto* end = data + size;
    const auto* hit = std::search(begin, end, needle.begin(), needle.end(),
        [](const std::uint8_t a, const char b) { return a == static_cast<std::uint8_t>(b); });
    return hit == end ? std::string_view::npos : static_cast<std::size_t>(hit - data);
}

// Offset of an ISO-BMFF box of the given type whose 32-bit size field fits
// inside the buffer, so a fourcc that merely occurs inside other data is
// skipped. Returns the offset of the type field.
std::size_t findBox(const std::uint8_t* data, const std::size_t size, const std::string_view type,
    std::size_t from = 0)
{
    for (std::size_t at = find(data, size, type, from); at != std::string_view::npos;
         at = find(data, size, type, at + 1)) {
        if (at < 4) {
            continue;
        }
        const std::uint32_t boxSize = readBe32(data + at - 4);
        if (boxSize >= 8 && at - 4 + boxSize <= size) {
            return at;
        }
    }
    return std::string_view::npos;
}

// Text between <tag> and the next '<' in the GSpherical XML.
std::string xmlValue(const std::uint8_t* data, const std::size_t size, const std::string_view tag)
{
    const std::string open = "<" + std::string(tag) + ">";
    const std::size_t at = find(data, size, open);
    if (at == std::string_view::npos) {
        return {};
    }
    std::string value;
    for (std::size_t index = at + open.size(); index < size && data[index] != '<' && value.size() < 64; ++index) {
        value.push_back(static_cast<char>(data[index]));
    }
    return value;
}

// EBML variable-length integer with the length marker bit cleared, as used
// for element sizes.
bool readVint(const std::uint8_t* data, const std::size_t size, std::size_t& position,
    std::uint64_t& value)
{
    if (position >= size || data[position] == 0) {
        return false;
    }
    const std::uint8_t first = data[position];
    std::size_t length = 1;
    while ((first & (0x80U >> (length - 1))) == 0) {
        ++length;
    }
    if (position + length > size) {
        return false;
    }
    value = first & (0xFFU >> length);
    for (std::size_t index = 1; index < length; ++index) {
        value = (value << 8) | data[position + index];
    }
    position += length;
    return true;
}

// Value of the first unsigned-integer element with the given 2-byte ID, or -1.
long long matroskaUnsigned(const std::uint8_t* data, const std::size_t size, const std::uint8_t id0,
    const std::uint8_t id1)
{
    for (std::size_t at = 0; at + 3 < size; ++at) {
        if (data[at] != id0 || data[at + 1] != id1) {
            continue;
        }
        std::size_t position = at + 2;
        std::uint64_t length = 0;
        if (!readVint(data, size, position, length) || length == 0 || length > 8
            || position + length > size) {
            continue;
        }
        std::uint64_t value = 0;
        for (std::uint64_t index = 0; index < length; ++index) {
            value = (value << 8) | data[position + index];
        }
        return static_cast<long long>(value);
    }
    return -1;
}

// The equirectangular bounds (0.32 fixed point) in an 'equi' box payload or a
// Matroska ProjectionPrivate: a frame covering only the front half has
// roughly a quarter cropped away on each side.
bool boundsCoverHalfSphere(const std::uint8_t* payload, const std::size_t size)
{
    if (size < 20) {
        return false;
    }
    const std::uint64_t left = readBe32(payload + 12);
    const std::uint64_t right = readBe32(payload + 16);
    return left + right >= 0x60000000ULL;
}

} // namespace

SpatialMetadata parseMp4Moov(const std::uint8_t* data, const std::size_t size)
{
    SpatialMetadata metadata;
    if (data == nullptr || size < 8) {
        return metadata;
    }

    // Spherical Video V2: st3d and sv3d live in the visual sample entry.
    if (const std::size_t st3d = findBox(data, size, "st3d"); st3d != std::string_view::npos
        && st3d + 8 < size) {
        switch (data[st3d + 8]) { // after the type and version/flags
        case 0:
            metadata.stereo = StereoLayout::Mono;
            break;
        case 1:
            metadata.stereo = StereoLayout::TopBottom;
            break;
        case 2:
            metadata.stereo = StereoLayout::LeftRight;
            break;
        default:
            break;
        }
    }
    if (const std::size_t sv3d = findBox(data, size, "sv3d"); sv3d != std::string_view::npos) {
        const std::size_t sv3dEnd = sv3d - 4 + readBe32(data + sv3d - 4);
        if (const std::size_t equi = findBox(data, sv3dEnd, "equi", sv3d); equi != std::string_view::npos) {
            metadata.projection = boundsCoverHalfSphere(data + equi + 4, sv3dEnd - equi - 4)
                ? SphericalProjection::Equirectangular180
                : SphericalProjection::Equirectangular;
        } else if (findBox(data, sv3dEnd, "cbmp", sv3d) != std::string_view::npos) {
            metadata.projection = SphericalProjection::Cubemap;
        } else if (findBox(data, sv3dEnd, "mshp", sv3d) != std::string_view::npos) {
            metadata.projection = SphericalProjection::Mesh;
        }
    }

    // Spherical Video V1: GSpherical XML in a uuid box.
    if (metadata.projection == SphericalProjection::Unknown
        && xmlValue(data, size, "GSpherical:Spherical") == "true") {
        metadata.projection = SphericalProjection::Equirectangular;
        const std::string cropped = xmlValue(data, size, "GSpherical:CroppedAreaImageWidthPixels");
        const std::string full = xmlValue(data, size, "GSpherical:FullPanoWidthPixels");
        if (!cropped.empty() && !full.empty()) {
            const double ratio = std::strtod(cropped.c_str(), nullptr) / (std::max)(1.0, std::strtod(full.c_str(), nullptr));
            if (ratio > 0.4 && ratio < 0.6) {
                metadata.projection = SphericalProjection::Equirectangular180;
            }
        }
    }
    if (metadata.stereo == StereoLayout::Unknown) {
        const std::string mode = xmlValue(data, size, "GSpherical:StereoMode");
        if (mode == "mono") {
            metadata.stereo = StereoLayout::Mono;
        } else if (mode == "top-bottom") {
            metadata.stereo = StereoLayout::TopBottom;
        } else if (mode == "left-right") {
            metadata.stereo = StereoLayout::LeftRight;
        }
    }
    return metadata;
}

SpatialMetadata parseMatroskaHeader(const std::uint8_t* data, std::size_t size)
{
    SpatialMetadata metadata;
    if (data == nullptr || size < 4 || readBe32(data) != 0x1A45DFA3U) {
        return metadata;
    }
    // Only search the header: element IDs also occur by chance inside the
    // compressed frames that start with the first Cluster.
    const std::size_t cluster = find(data, size, std::string_view("\x1F\x43\xB6\x75", 4));
    if (cluster != std::string_view::npos) {
        size = cluster;
    }

    switch (matroskaUnsigned(data, size, 0x53, 0xB8)) { // StereoMode
    case 0:
        metadata.stereo = StereoLayout::Mono;
        break;
    case 1:  // side by side, left eye first
    case 11: // side by side, right eye first
        metadata.stereo = StereoLayout::LeftRight;
        break;
    case 2: // top-bottom, right eye first
    case 3: // top-bottom, left eye first
        metadata.stereo = StereoLayout::TopBottom;
        break;
    default:
        break;
    }

    switch (matroskaUnsigned(data, size, 0x76, 0x71)) { // ProjectionType
    case 0:
        metadata.projection = SphericalProjection::Flat;
        break;
    case 1: {
        metadata.projection = SphericalProjection::Equirectangular;
        // ProjectionPrivate (0x7672) carries the same bounds as an 'equi' box.
        const std::size_t privateAt = find(data, size, std::string_view("\x76\x72", 2));
        if (privateAt != std::string_view::npos) {
            std::size_t position = privateAt + 2;
            std::uint64_t length = 0;
            if (readVint(data, size, position, length) && position + length <= size
                && boundsCoverHalfSphere(data + position, static_cast<std::size_t>(length))) {
                metadata.projection = SphericalProjection::Equirectangular180;
            }
        }
        break;
    }
    case 2:
        metadata.projection = SphericalProjection::Cubemap;
        break;
    case 3:
        metadata.projection = SphericalProjection::Mesh;
        break;
    default:
        break;
    }
    return metadata;
}

SpatialMetadata readSpatialMetadata(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::uint8_t head[12] {};
    stream.read(reinterpret_cast<char*>(head), sizeof(head));
    if (stream.gcount() < static_cast<std::streamsize>(sizeof(head))) {
        return {};
    }
    stream.clear();

    if (readBe32(head) == 0x1A45DFA3U) {
        std::vector<std::uint8_t> buffer(kMatroskaHeaderBytes);
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        buffer.resize(static_cast<std::size_t>(stream.gcount()));
        return parseMatroskaHeader(buffer.data(), buffer.size());
    }

    const std::string_view firstType(reinterpret_cast<const char*>(head + 4), 4);
    if (firstType != "ftyp" && firstType != "moov" && firstType != "free" && firstType != "wide"
        && firstType != "mdat" && firstType != "skip") {
        return {};
    }

    // Walk the top-level boxes to find moov, which may sit after mdat.
    std::error_code error;
    const std::uint64_t fileSize = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    std::uint64_t position = 0;
    while (position + 8 <= fileSize) {
        std::uint8_t header[16] {};
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(position));
        stream.read(reinterpret_cast<char*>(header), 8);
        if (stream.gcount() != 8) {
            break;
        }
        std::uint64_t boxSize = readBe32(header);
        std::uint64_t headerSize = 8;
        if (boxSize == 1) {
            stream.read(reinterpret_cast<char*>(header + 8), 8);
            if (stream.gcount() != 8) {
                break;
            }
            boxSize = (static_cast<std::uint64_t>(readBe32(header + 8)) << 32) | readBe32(header + 12);
            headerSize = 16;
        } else if (boxSize == 0) {
            boxSize = fileSize - position;
        }
        if (boxSize < headerSize) {
            break;
        }
        if (std::memcmp(header + 4, "moov", 4) == 0) {
            const std::size_t payload = static_cast<std::size_t>(
                (std::min)(boxSize - headerSize, static_cast<std::uint64_t>(kMaxMoovBytes)));
            std::vector<std::uint8_t> buffer(payload);
            stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(payload));
            buffer.resize(static_cast<std::size_t>(stream.gcount()));
            return parseMp4Moov(buffer.data(), buffer.size());
        }
        position += boxSize;
    }
    return {};
}

std::string describe(const SpatialMetadata& metadata)
{
    std::string projection;
    switch (metadata.projection) {
    case SphericalProjection::Flat:
        projection = "duz";
        break;
    case SphericalProjection::Equirectangular:
        projection = "equirectangular";
        break;
    case SphericalProjection::Equirectangular180:
        projection = "equirectangular 180";
        break;
    case SphericalProjection::Cubemap:
        projection = "cubemap";
        break;
    case SphericalProjection::Mesh:
        projection = "mesh";
        break;
    case SphericalProjection::Unknown:
    default:
        projection = "yok";
        break;
    }
    std::string stereo;
    switch (metadata.stereo) {
    case StereoLayout::Mono:
        stereo = "mono";
        break;
    case StereoLayout::TopBottom:
        stereo = "ust/alt";
        break;
    case StereoLayout::LeftRight:
        stereo = "yan yana";
        break;
    case StereoLayout::Unknown:
    default:
        stereo = "yok";
        break;
    }
    return "projeksiyon=" + projection + ", stereo=" + stereo;
}

} // namespace dk2vr
