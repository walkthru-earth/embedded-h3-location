// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <embedded_h3/location.h>

#include <cstdint>

namespace embedded_h3::location::detail {
// Maps an H3Error code to Error. E_CELL_INVALID and E_RES_DOMAIN keep their
// meaning, E_LATLNG_DOMAIN maps to OutOfRange, and the four codes H3
// cellsToMultiPolygon reports map to MixedResolution, DuplicateCell,
// TooManyCells and OutOfMemory. Every other code, including E_SUCCESS passed
// by mistake, maps to H3Failure. Private, the tests use it to reach codes that
// checked input cannot trigger in H3 4.5.0.
[[nodiscard]] Error from_h3_error(std::uint32_t code) noexcept;
} // namespace embedded_h3::location::detail
