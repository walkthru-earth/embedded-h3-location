// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#pragma once

#include <embedded_h3/location.h>

namespace embedded_h3::location::detail {
// Degrees times 10^7, rounded half away from zero with std::llround. Private,
// center_e7 uses it and the tests use it to pin values exactly halfway between
// two E7 steps, which no H3 cell center lands on.
[[nodiscard]] E7 to_e7(Degrees degrees) noexcept;
} // namespace embedded_h3::location::detail
