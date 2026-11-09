// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <embedded_h3/location.h>

#include <h3api.h>

#include <expected>
#include <span>

namespace embedded_h3::location::detail {
// Copies an H3 GeoMultiPolygon into the caller's buffers, closing each ring
// and converting radians to degrees. A result that breaks H3's own contract,
// no polygons, a null array, a ring under 3 points or a negative hole count,
// gives H3Failure. Private, cells_to_multipolygon uses it and the tests feed
// it malformed input that H3 4.5.0 never returns.
[[nodiscard]] std::expected<MultiPolygon, Error>
copy_multipolygon(const GeoMultiPolygon &h3, std::span<LngLat> points,
                  std::span<Ring> rings, std::span<Polygon> polygons) noexcept;
} // namespace embedded_h3::location::detail
