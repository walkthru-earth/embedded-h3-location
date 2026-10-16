// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
//
// Minimal ESP-IDF example for embedded-h3-location. Indexes a coordinate,
// coarsens it to the default privacy resolution and logs the result.

#include <cinttypes>
#include <cstdint>

#include "aq_location.h"
#include "esp_log.h"

static const char *TAG = "h3_basic";

extern "C" void app_main(void) {
  using namespace aq::location;

  Cell fine = 0;
  Cell published = 0;
  char hex[kCellTextBytes]{};
  std::int64_t lat_e7 = 0;
  std::int64_t lon_e7 = 0;

  if (!from_degrees(37.7759387, -122.4179506, 9, fine) ||
      !coarsen(fine, kDefaultResolution, published) ||
      !format_cell(published, hex, sizeof(hex)) ||
      !center_e7(published, lat_e7, lon_e7)) {
    ESP_LOGE(TAG, "H3 location pipeline failed");
    return;
  }

  ESP_LOGI(TAG, "cell=%s res=%d center_e7=(%" PRId64 ", %" PRId64 ") US=%d",
           hex, resolution(published), lat_e7, lon_e7,
           valid_country("US") ? 1 : 0);
}
