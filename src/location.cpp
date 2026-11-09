// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#include <embedded_h3/location.h>

#include "h3_error.h"
#include "iso_alpha2.h"

#include <h3api.h>

#include <algorithm>
#include <array>
#include <cmath>

// The build pins C++23 (CMakeLists.txt). GCC 13 reports 202100L for C++23 and
// GCC 14 or later and clang report 202302L. C++26 modes report 202400L or more,
// so a silent SDK dialect change fails here.
static_assert(__cplusplus >= 202100L && __cplusplus < 202400L,
              "embedded-h3-location must build as C++23");

namespace embedded_h3::location {
namespace {

using std::unexpected;

[[nodiscard]] constexpr bool valid_resolution(int res) noexcept {
  return res >= kMinResolution && res <= kMaxResolution;
}

[[nodiscard]] constexpr bool is_hex(char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

} // namespace

namespace detail {
Error from_h3_error(std::uint32_t code) noexcept {
  switch (code) {
  case E_CELL_INVALID:
    return Error::InvalidCell;
  case E_RES_DOMAIN:
    return Error::InvalidResolution;
  case E_LATLNG_DOMAIN:
    return Error::OutOfRange;
  default:
    return Error::H3Failure;
  }
}
} // namespace detail

const char *error_name(Error error) noexcept {
  using enum Error;
  switch (error) {
  case InvalidCell:
    return "invalid_cell";
  case InvalidResolution:
    return "invalid_resolution";
  case OutOfRange:
    return "out_of_range";
  case NonFinite:
    return "non_finite";
  case InvalidText:
    return "invalid_text";
  case H3Failure:
    return "h3_failure";
  }
  return "unknown";
}

bool valid_cell(Cell cell) noexcept { return isValidCell(cell) != 0; }

std::expected<int, Error> resolution(Cell cell) noexcept {
  if (!valid_cell(cell))
    return unexpected(Error::InvalidCell);
  return getResolution(cell);
}

std::expected<Cell, Error> parse_cell(std::string_view text) noexcept {
  // H3 stringToH3 uses sscanf, which accepts prefixes, signs and trailing
  // text. Constrain the shape first, then hand H3 a NUL terminated copy,
  // because a string_view need not end in a NUL.
  if (text.size() != kCellTextLength || !std::ranges::all_of(text, is_hex))
    return unexpected(Error::InvalidText);
  std::array<char, kCellTextBytes> terminated{};
  std::ranges::copy(text, terminated.begin());
  H3Index parsed = 0;
  if (const H3Error status = stringToH3(terminated.data(), &parsed);
      status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  if (!valid_cell(parsed))
    return unexpected(Error::InvalidCell);
  return parsed;
}

std::expected<void, Error>
format_cell(Cell cell, std::span<char, kCellTextBytes> out) noexcept {
  if (!valid_cell(cell))
    return unexpected(Error::InvalidCell);
  // H3 asks for 17 bytes so any 64 bit index fits. A valid cell always has
  // mode 1 in bits 59..62 and bit 63 clear, so it prints as 15 digits.
  std::array<char, 17> canonical{};
  if (const H3Error status =
          h3ToString(cell, canonical.data(), canonical.size());
      status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  std::ranges::copy_n(canonical.begin(), kCellTextBytes, out.begin());
  return {};
}

std::expected<Cell, Error> coarsen(Cell input, int maximum_resolution) noexcept {
  if (!valid_resolution(maximum_resolution))
    return unexpected(Error::InvalidResolution);
  if (!valid_cell(input))
    return unexpected(Error::InvalidCell);
  H3Index parent = 0;
  if (const H3Error status = cellToParent(
          input, std::min(maximum_resolution, getResolution(input)), &parent);
      status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  return parent;
}

std::expected<Degrees, Error> center_degrees(Cell cell) noexcept {
  if (!valid_cell(cell))
    return unexpected(Error::InvalidCell);
  LatLng center{};
  if (const H3Error status = cellToLatLng(cell, &center); status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  return Degrees{.latitude = radsToDegs(center.lat),
                 .longitude = radsToDegs(center.lng)};
}

std::expected<E7, Error> center_e7(Cell cell) noexcept {
  return center_degrees(cell).transform([](const Degrees &center) noexcept {
    return E7{.latitude = std::llround(center.latitude * 1e7),
              .longitude = std::llround(center.longitude * 1e7)};
  });
}

std::expected<Cell, Error> from_degrees(double latitude, double longitude,
                                        int res) noexcept {
  if (!std::isfinite(latitude) || !std::isfinite(longitude))
    return unexpected(Error::NonFinite);
  if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
      longitude > 180.0)
    return unexpected(Error::OutOfRange);
  if (!valid_resolution(res))
    return unexpected(Error::InvalidResolution);
  const LatLng coordinates{.lat = degsToRads(latitude),
                           .lng = degsToRads(longitude)};
  H3Index indexed = 0;
  if (const H3Error status = latLngToCell(&coordinates, res, &indexed);
      status != E_SUCCESS)
    return unexpected(detail::from_h3_error(status));
  return indexed;
}

bool valid_country(std::string_view country) noexcept {
  // The table holds only two letter uppercase codes, so any other length or
  // case never matches.
  for (std::size_t i = 0; i < detail::kIsoAlpha2.size(); i += 2)
    if (detail::kIsoAlpha2.substr(i, 2) == country)
      return true;
  return false;
}

} // namespace embedded_h3::location
