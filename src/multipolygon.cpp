// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#include "multipolygon.h"

#include "h3_error.h"

#include <h3api.h>

// cellsToMultiPolygon and destroyGeoMultiPolygon are internal in H3 4.5.0.
// cellsToMultiPoly.h declares them with "TODO: This will be moved to
// h3api.h.in when it is ready to release". Including that header lets the
// compiler check the declarations against the vendored source. Its inline
// overflow check compares int64_t with uint64_t, which C++ -Wsign-compare
// flags, so that warning is off for this header alone.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
extern "C" {
#include "cellsToMultiPoly.h"
}
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace embedded_h3::location {
namespace {

using std::unexpected;

// H3 takes the cell count as int64_t. A span of Cell holds at most
// SIZE_MAX / sizeof(Cell) elements, which fits int64_t on 32 and 64 bit
// targets, so the conversion below needs no runtime check.
static_assert(std::numeric_limits<std::size_t>::max() / sizeof(Cell) <=
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));

// Frees what a successful cellsToMultiPolygon returned. H3 frees its own
// partial work before it returns an error, so this is armed only on success.
class OwnedMultiPolygon {
public:
  GeoMultiPolygon value{};
  bool armed = false;
  OwnedMultiPolygon() = default;
  OwnedMultiPolygon(const OwnedMultiPolygon &) = delete;
  OwnedMultiPolygon &operator=(const OwnedMultiPolygon &) = delete;
  ~OwnedMultiPolygon() {
    if (armed)
      destroyGeoMultiPolygon(&value);
  }
};

// h3-js 4.5.0 converts with (rad * 180) / Math.PI, left to right. H3's own
// radsToDegs multiplies by a rounded 180 / pi constant, which can differ in
// the last bit, so it is not used here.
[[nodiscard]] constexpr double degrees(double radians) noexcept {
  return radians * 180.0 / std::numbers::pi;
}

// Ring and Polygon hold 32 bit indexes, so only that many entries are used.
template <class T>
[[nodiscard]] std::span<T> indexable(std::span<T> buffer) noexcept {
  return buffer.first(std::min(buffer.size(), kMaxMultiPolygonEntries));
}

} // namespace

namespace detail {
std::expected<MultiPolygon, Error>
copy_multipolygon(const GeoMultiPolygon &h3, std::span<LngLat> all_points,
                  std::span<Ring> all_rings,
                  std::span<Polygon> polygons) noexcept {
  const std::span<LngLat> points = indexable(all_points);
  const std::span<Ring> rings = indexable(all_rings);
  if (h3.numPolygons <= 0 || h3.polygons == nullptr)
    return unexpected(Error::H3Failure);

  std::size_t point_count = 0;
  std::size_t ring_count = 0;
  std::size_t polygon_count = 0;
  // Copies one loop and closes it. Both bounds are checked before any write,
  // so a full buffer never takes a partial ring.
  const auto add_ring = [&](const GeoLoop &loop) -> std::expected<void, Error> {
    // H3 geoMultiPolygonToLinkedGeoPolygon, which h3-js reads, also needs 3.
    if (loop.numVerts < 3 || loop.verts == nullptr)
      return unexpected(Error::H3Failure);
    const auto vertices = static_cast<std::size_t>(loop.numVerts);
    if (ring_count == rings.size())
      return unexpected(Error::RingBufferFull);
    if (vertices + 1 > points.size() - point_count)
      return unexpected(Error::PointBufferFull);
    rings[ring_count++] = Ring{.first = static_cast<std::uint32_t>(point_count),
                               .count = static_cast<std::uint32_t>(vertices + 1)};
    const std::size_t first = point_count;
    for (std::size_t i = 0; i < vertices; ++i)
      points[point_count++] = LngLat{.longitude = degrees(loop.verts[i].lng),
                                     .latitude = degrees(loop.verts[i].lat)};
    points[point_count++] = points[first];
    return {};
  };
  for (int p = 0; p < h3.numPolygons; ++p) {
    const GeoPolygon &polygon = h3.polygons[p];
    if (polygon.numHoles < 0 || (polygon.numHoles > 0 && polygon.holes == nullptr))
      return unexpected(Error::H3Failure);
    if (polygon_count == polygons.size())
      return unexpected(Error::PolygonBufferFull);
    const std::size_t first_ring = ring_count;
    if (const auto added = add_ring(polygon.geoloop); !added)
      return unexpected(added.error());
    for (int h = 0; h < polygon.numHoles; ++h)
      if (const auto added = add_ring(polygon.holes[h]); !added)
        return unexpected(added.error());
    polygons[polygon_count++] =
        Polygon{.first_ring = static_cast<std::uint32_t>(first_ring),
                .ring_count = static_cast<std::uint32_t>(ring_count - first_ring)};
  }
  return MultiPolygon{.polygons = polygons.first(polygon_count),
                      .rings = rings.first(ring_count),
                      .points = points.first(point_count)};
}
} // namespace detail

std::expected<MultiPolygon, Error>
cells_to_multipolygon(std::span<const Cell> cells, std::span<LngLat> points,
                      std::span<Ring> rings,
                      std::span<Polygon> polygons) noexcept {
  // H3 returns success and no polygons for zero cells, which is no outline.
  if (cells.empty())
    return unexpected(Error::NoCells);
  OwnedMultiPolygon result;
  if (const H3Error status = cellsToMultiPolygon(
          cells.data(), static_cast<std::int64_t>(cells.size()), &result.value);
      status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  result.armed = true;
  return detail::copy_multipolygon(result.value, points, rings, polygons);
}

} // namespace embedded_h3::location
