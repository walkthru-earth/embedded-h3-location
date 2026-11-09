// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <version>

// The release this header belongs to. CMake checks at configure time that it
// matches project(VERSION) in CMakeLists.txt, version in idf_component.yml and
// the newest heading in CHANGELOG.md.
#define EMBEDDED_H3_LOCATION_VERSION_MAJOR 2
#define EMBEDDED_H3_LOCATION_VERSION_MINOR 0
#define EMBEDDED_H3_LOCATION_VERSION_PATCH 0
#define EMBEDDED_H3_LOCATION_VERSION_STRING "2.0.0"

// 202211L adds the monadic operations (P2505R5) that the source and the
// examples use, such as transform and and_then. libstdc++ from GCC 13 and
// libc++ from LLVM 17 define it. GCC 12 defines only 202202L.
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
#error "embedded-h3-location needs C++23 std::expected with monadic operations (__cpp_lib_expected >= 202211L), such as GCC 13 or LLVM 17 libc++"
#endif

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

// Stateless helpers around the official Uber H3 C library. They hold no state
// and never throw. Only cells_to_multipolygon allocates, inside H3, and it
// frees all of it before it returns. Persist only the output of coarsen() and
// derive public coordinates from that cell. A cell center is not the original
// position, and H3 is not a country lookup.
namespace embedded_h3::location {

// The same release as constants, for code that prefers them to macros.
inline constexpr int kVersionMajor = EMBEDDED_H3_LOCATION_VERSION_MAJOR;
inline constexpr int kVersionMinor = EMBEDDED_H3_LOCATION_VERSION_MINOR;
inline constexpr int kVersionPatch = EMBEDDED_H3_LOCATION_VERSION_PATCH;
inline constexpr std::string_view kVersion =
    EMBEDDED_H3_LOCATION_VERSION_STRING;

using Cell = std::uint64_t;

inline constexpr int kMinResolution = 0;
inline constexpr int kMaxResolution = 15;
// A valid cell prints as 15 hex digits. The buffer adds one byte for the NUL.
inline constexpr std::size_t kCellTextLength = 15;
inline constexpr std::size_t kCellTextBytes = kCellTextLength + 1;

// Why a call failed. There is no success value, std::expected carries that.
enum class [[nodiscard]] Error : std::uint8_t {
  InvalidCell = 1,   // not a valid H3 cell, including the absent cell 0
  InvalidResolution, // resolution argument outside 0..15
  OutOfRange,        // latitude outside -90..90 or longitude outside -180..180
  NonFinite,         // latitude or longitude is NaN or infinite
  InvalidText,       // cell text is not exactly 15 hex digits
  H3Failure,         // H3 rejected checked input or broke its output contract
  TooCoarse,         // cell is coarser than the resolution parent_at asks for
  NoCells,           // cells_to_multipolygon got an empty cell list
  MixedResolution,   // the cells differ in resolution (H3 E_RES_MISMATCH)
  DuplicateCell,     // a cell is listed twice (H3 E_DUPLICATE_INPUT)
  TooManyCells,      // a size would overflow (H3 E_MEMORY_BOUNDS or the capacity)
  OutOfMemory,       // an H3 allocation failed (H3 E_MEMORY_ALLOC)
  PointBufferFull,   // the caller's point buffer is too small
  RingBufferFull,    // the caller's ring buffer is too small
  PolygonBufferFull, // the caller's polygon buffer is too small
};

// Stable lowercase name for logs, such as "invalid_cell". Static storage,
// NUL terminated, safe for %s. An unknown value gives "unknown".
[[nodiscard]] const char *error_name(Error error) noexcept;

// WGS84 latitude and longitude in degrees.
struct Degrees {
  double latitude;
  double longitude;
};

// Degrees times 10^7, rounded half away from zero.
struct E7 {
  std::int64_t latitude;
  std::int64_t longitude;
};

// True for a valid H3 cell, from H3 isValidCell. False for 0 and for any
// directed edge, vertex or malformed index.
[[nodiscard]] bool valid_cell(Cell cell) noexcept;

// 0..15 for a valid cell, otherwise Error::InvalidCell.
[[nodiscard]] std::expected<int, Error> resolution(Cell cell) noexcept;

// Exactly 15 hex digits, either case, nothing before or after. The view does
// not need a NUL terminator. Returns InvalidText for any other shape and
// InvalidCell when the digits are not a valid H3 cell.
[[nodiscard]] std::expected<Cell, Error> parse_cell(std::string_view text) noexcept;

// Writes 15 lowercase hex digits and a NUL into out. A shorter buffer does not
// compile. On error out is left unchanged.
[[nodiscard]] std::expected<void, Error>
format_cell(Cell cell, std::span<char, kCellTextBytes> out) noexcept;

// Parent of input at min(maximum_resolution, resolution(input)), computed by
// H3 cellToParent. A coarser input comes back as it is, so raising the limit
// later never recovers finer location. The limit is the caller's policy and
// this library sets no default. Checks maximum_resolution first, then input.
[[nodiscard]] std::expected<Cell, Error> coarsen(Cell input,
                                                 int maximum_resolution) noexcept;

// Parent of input at exactly the given resolution, from H3 cellToParent. A
// cell at that resolution comes back as it is. A coarser cell gives TooCoarse,
// because finer location cannot be restored, so this never returns a child.
// Use it when every stored cell must sit at one resolution. The resolution is
// the caller's policy. Checks resolution first, then input, then TooCoarse.
[[nodiscard]] std::expected<Cell, Error> parent_at(Cell input,
                                                   int resolution) noexcept;

// WGS84 center of the cell, from H3 cellToLatLng. center_e7 gives the same
// center in E7 units.
[[nodiscard]] std::expected<Degrees, Error> center_degrees(Cell cell) noexcept;
[[nodiscard]] std::expected<E7, Error> center_e7(Cell cell) noexcept;

// Indexes a position straight at the given resolution with H3 latLngToCell.
// Checks run in this order. NonFinite for NaN or infinity, OutOfRange outside
// latitude -90..90 or longitude -180..180 (both ends allowed), then
// InvalidResolution.
[[nodiscard]] std::expected<Cell, Error>
from_degrees(double latitude, double longitude, int resolution) noexcept;

// True only for one of the 249 assigned ISO 3166-1 alpha-2 codes, uppercase,
// exactly two characters. The country is owner declared and never derived from
// a cell. Empty input is not a valid code.
[[nodiscard]] bool valid_country(std::string_view country) noexcept;

// ---- Cells to multipolygon ----
//
// The outline of a set of cells, the union of the cells, as GeoJSON style
// polygons written into buffers the caller owns. Coordinates are degrees,
// longitude first, converted as (radians * 180) / pi in that order, which is
// how h3-js 4.5.0 converts. Each ring is closed, its last point equal to its
// first. Every polygon has its outer ring first, then its holes. Outer rings
// run counterclockwise and holes clockwise, and polygons come largest outer
// ring first, all as H3 4.5.0 returns them.

// Longitude and latitude in degrees.
struct LngLat {
  double longitude;
  double latitude;
};

// Points [first, first + count) of the point buffer, closing point included.
struct Ring {
  std::uint32_t first;
  std::uint32_t count;
};

// Rings [first_ring, first_ring + ring_count) of the ring buffer. The first
// one is the outer ring.
struct Polygon {
  std::uint32_t first_ring;
  std::uint32_t ring_count;
};

// Views into the caller's buffers, valid while those buffers are.
struct MultiPolygon {
  std::span<const Polygon> polygons;
  std::span<const Ring> rings;
  std::span<const LngLat> points;
};

// Buffer sizes that always hold the outline of `cells` distinct cells.
struct MultiPolygonCapacity {
  std::size_t points;
  std::size_t rings;
  std::size_t polygons;
};

// Ring and Polygon index with 32 bits, so no buffer needs more entries.
inline constexpr std::size_t kMaxMultiPolygonEntries =
    std::numeric_limits<std::uint32_t>::max();

// Sizes for `cells` distinct cells at `resolution`. Each cell has at most 6
// edges and every outline edge adds one point, two at odd (Class III)
// resolutions, the only ones where an edge can carry a distortion vertex (H3
// faceijk.c). Every ring adds one closing point and every polygon holds at
// least one cell. Returns InvalidResolution outside 0..15 and
// TooManyCells when the point count would pass kMaxMultiPolygonEntries.
[[nodiscard]] constexpr std::expected<MultiPolygonCapacity, Error>
multipolygon_capacity(std::size_t cells, int resolution) noexcept {
  if (resolution < kMinResolution || resolution > kMaxResolution)
    return std::unexpected(Error::InvalidResolution);
  const std::size_t points_per_edge = (resolution % 2 != 0) ? 2 : 1;
  const std::size_t points_per_cell = 6 * points_per_edge + 2;
  if (cells > kMaxMultiPolygonEntries / points_per_cell)
    return std::unexpected(Error::TooManyCells);
  return MultiPolygonCapacity{.points = cells * points_per_cell,
                              .rings = 2 * cells,
                              .polygons = cells};
}

// The outline of `cells`, from H3 cellsToMultiPolygon. The cells must be
// valid, distinct and of one resolution. This sets no resolution limit, so
// coarsen first when the outline must not be finer than a policy allows. Pass
// the cells sorted ascending for a stable output, because H3 starts each ring
// at an edge that depends on input order. Buffers sized by
// multipolygon_capacity always fit. Only the first kMaxMultiPolygonEntries
// entries of a buffer are used.
//
// Returns views into `points`, `rings` and `polygons`. Errors are NoCells for
// an empty list, then what H3 reports, InvalidCell (0 included),
// MixedResolution, DuplicateCell, TooManyCells or OutOfMemory, then
// PolygonBufferFull, RingBufferFull or PointBufferFull. On any error the
// buffers hold no usable result.
//
// Heap. This is the only call in the library that allocates. H3 allocates
// while it traces the outline, through H3_MEMORY (vendor H3 alloc.h), and
// frees all of it before this returns, on every path. That is plain malloc,
// calloc, realloc and free unless the build sets an allocator prefix, see
// EMBEDDED_H3_LOCATION_ALLOC_PREFIX in the README. Where malloc(0) returns
// NULL, as on ESP-IDF 6.1, the whole globe needs hooks that round zero up
// with nonzero_alloc_size, or it returns OutOfMemory.
[[nodiscard]] std::expected<MultiPolygon, Error>
cells_to_multipolygon(std::span<const Cell> cells, std::span<LngLat> points,
                      std::span<Ring> rings,
                      std::span<Polygon> polygons) noexcept;

// The size an allocator hook should pass on, at least 1. H3 can ask for zero
// bytes. For the whole globe it calls
// H3_MEMORY(malloc)(sizeof(SortableLoop) * numLoops) with numLoops 0
// (cellsToMultiPoly.c line 388 in H3 v4.5.0) before it takes the globe path
// (line 525), and it reads NULL as OutOfMemory. C lets malloc return NULL for
// a zero size, and ESP-IDF 6.1 always does (heap_caps_base.c lines 113-116 at
// commit fff9895c, reached by malloc, calloc, realloc and heap_caps_*). Pass
// the size of a malloc or realloc hook and both calloc arguments through
// this, so a zero request gets a 1 byte block that free releases.
[[nodiscard]] constexpr std::size_t
nonzero_alloc_size(std::size_t size) noexcept {
  return size != 0 ? size : 1;
}

} // namespace embedded_h3::location
