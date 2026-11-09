// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <version>

// 202211L adds the monadic operations (P2505R5) that the source and the
// examples use, such as transform and and_then. libstdc++ from GCC 13 and
// libc++ from LLVM 17 define it. GCC 12 defines only 202202L.
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
#error "embedded-h3-location needs C++23 std::expected with monadic operations (__cpp_lib_expected >= 202211L), such as GCC 13 or LLVM 17 libc++"
#endif

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

// Stateless helpers around the official Uber H3 C library. They hold no state,
// never allocate and never throw. Persist only the output of coarsen() and
// derive public coordinates from that cell. A cell center is not the original
// position, and H3 is not a country lookup.
namespace embedded_h3::location {

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
  H3Failure,         // H3 rejected input this library had already checked
  TooCoarse,         // cell is coarser than the resolution parent_at asks for
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

} // namespace embedded_h3::location
