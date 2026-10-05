#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace dk2vr {

// Spherical video metadata stored in the container, as written by cameras,
// the Spatial Media Metadata Injector and YouTube/yt-dlp downloads:
//   MP4:  moov/.../sv3d (proj: equi | cbmp | mshp) and st3d (stereo mode),
//         or the older Google "GSpherical" XML in a uuid box.
//   MKV/WebM: Video/StereoMode and Video/Projection/ProjectionType.
enum class SphericalProjection { Unknown, Flat, Equirectangular, Equirectangular180, Cubemap, Mesh };
enum class StereoLayout { Unknown, Mono, TopBottom, LeftRight };

struct SpatialMetadata {
    SphericalProjection projection {SphericalProjection::Unknown};
    StereoLayout stereo {StereoLayout::Unknown};

    [[nodiscard]] bool empty() const noexcept
    {
        return projection == SphericalProjection::Unknown && stereo == StereoLayout::Unknown;
    }
};

// Reads the container header of a local file. Never throws; returns an empty
// result for unknown formats or files without spherical metadata.
[[nodiscard]] SpatialMetadata readSpatialMetadata(const std::filesystem::path& path);

// The parsers behind readSpatialMetadata, exposed for the unit tests.
// parseMp4Moov takes the payload of the moov box; parseMatroskaHeader takes
// the start of the file up to (at least) the first Cluster.
[[nodiscard]] SpatialMetadata parseMp4Moov(const std::uint8_t* data, std::size_t size);
[[nodiscard]] SpatialMetadata parseMatroskaHeader(const std::uint8_t* data, std::size_t size);

[[nodiscard]] std::string describe(const SpatialMetadata& metadata);

} // namespace dk2vr
