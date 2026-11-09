// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
//
// Minimal ESP-IDF example for embedded-h3-location. Indexes a coordinate,
// coarsens it to this app's privacy resolution and logs the result.

#include <array>
#include <cinttypes>

#include <embedded_h3/location.h>

#include "esp_log.h"

namespace {

constexpr const char *kTag = "h3_basic";
// The privacy limit is app policy. The library has no default.
constexpr int kPublishResolution = 8;

} // namespace

extern "C" void app_main(void) {
  namespace loc = embedded_h3::location;

  const auto published =
      loc::from_degrees(37.7759387, -122.4179506, 9)
          .and_then([](loc::Cell fine) {
            return loc::coarsen(fine, kPublishResolution);
          });
  if (!published) {
    ESP_LOGE(kTag, "H3 location failed: %s",
             loc::error_name(published.error()));
    return;
  }

  std::array<char, loc::kCellTextBytes> text{};
  const auto formatted = loc::format_cell(*published, text);
  const auto center = loc::center_e7(*published);
  if (!formatted || !center) {
    ESP_LOGE(kTag, "H3 format failed");
    return;
  }

  ESP_LOGI(kTag, "cell=%s res=%d center_e7=(%" PRId64 ", %" PRId64 ") US=%d",
           text.data(), loc::resolution(*published).value_or(-1),
           center->latitude, center->longitude,
           loc::valid_country("US") ? 1 : 0);
}
