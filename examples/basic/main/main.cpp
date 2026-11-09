// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
//
// Minimal ESP-IDF example for embedded-h3-location. Indexes a coordinate,
// coarsens it to this app's privacy resolution, logs the result and traces
// the outline of the cell.

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <embedded_h3/location.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

constexpr const char *kTag = "h3_basic";
// The privacy limit is app policy. The library has no default.
constexpr int kPublishResolution = 8;

// Heap for H3 when CONFIG_EMBEDDED_H3_LOCATION_ALLOC_PREFIX is "example_h3_",
// as sdkconfig.alloc sets it. On a board with PSRAM use MALLOC_CAP_SPIRAM to
// keep H3 out of internal RAM. With the prefix empty nothing calls these and
// the linker drops them.
constexpr std::uint32_t kH3Caps = MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT;

} // namespace

extern "C" {
void *example_h3_malloc(std::size_t size) {
  return heap_caps_malloc(size, kH3Caps);
}
void *example_h3_calloc(std::size_t count, std::size_t size) {
  return heap_caps_calloc(count, size, kH3Caps);
}
void *example_h3_realloc(void *pointer, std::size_t size) {
  return heap_caps_realloc(pointer, size, kH3Caps);
}
void example_h3_free(void *pointer) { heap_caps_free(pointer); }
}

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

  // The outline of the published cell, in buffers sized at compile time.
  const std::array cells{*published};
  constexpr auto size = loc::multipolygon_capacity(1, kPublishResolution);
  static_assert(size.has_value());
  std::array<loc::LngLat, size->points> points{};
  std::array<loc::Ring, size->rings> rings{};
  std::array<loc::Polygon, size->polygons> polygons{};
  const auto outline =
      loc::cells_to_multipolygon(cells, points, rings, polygons);
  if (!outline) {
    ESP_LOGE(kTag, "H3 outline failed: %s", loc::error_name(outline.error()));
    return;
  }
  const loc::LngLat &first = outline->points.front();
  ESP_LOGI(kTag, "outline polygons=%u rings=%u points=%u first=(%.7f, %.7f)",
           static_cast<unsigned>(outline->polygons.size()),
           static_cast<unsigned>(outline->rings.size()),
           static_cast<unsigned>(outline->points.size()), first.longitude,
           first.latitude);
}
