// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
//
// Host checks for cells_to_multipolygon and multipolygon_capacity. CMake
// builds this file twice. test_multipolygon links a library compiled with the
// H3 allocator prefix eh3l_test_ and defines those hooks below, so a counting
// allocator sees every H3 allocation, can fail any one of them and proves
// nothing is left after each call. test_multipolygon_malloc links the default
// library, where H3 calls plain malloc, and skips the counting checks. Host
// numbers are 64 bit. They bound, not measure, a device heap.
#include <embedded_h3/location.h>

#include "multipolygon.h"

#include <h3api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numbers>
#include <span>
#include <type_traits>
#include <vector>

#ifndef EMBEDDED_H3_LOCATION_TEST_COUNTING
#define EMBEDDED_H3_LOCATION_TEST_COUNTING 0
#endif

// Always evaluated. Unlike assert() it survives -DNDEBUG, so Release builds
// run every call under test. No __FILE__, the line and expression are enough.
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "line %d: CHECK failed: %s\n", __LINE__, #expr);    \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

namespace loc = embedded_h3::location;
using loc::Cell;
using loc::Error;
using loc::LngLat;
using loc::MultiPolygon;
using loc::Polygon;
using loc::Ring;

namespace {

constexpr bool kCounting = EMBEDDED_H3_LOCATION_TEST_COUNTING != 0;

// ---- Counting allocator, the H3_ALLOC_PREFIX hooks ----

// The default CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL in ESP-IDF. Plain malloc of
// this size or less prefers internal RAM on a board with PSRAM.
constexpr std::size_t kAlwaysInternal = 16384;

struct Heap {
  std::size_t live_bytes = 0;
  std::size_t peak_bytes = 0;
  std::size_t live_blocks = 0;
  std::size_t allocations = 0;
  std::size_t largest = 0;
  std::size_t small_live = 0; // live bytes in blocks <= kAlwaysInternal
  std::size_t small_peak = 0;
  std::size_t small_allocations = 0;
  std::size_t fail_at = SIZE_MAX; // allocation index to fail
};
Heap heap;

void reset_heap(std::size_t fail_at = SIZE_MAX) {
  heap = {};
  heap.fail_at = fail_at;
}

// True when every H3 block is back. Always true without the counting hooks.
bool heap_clean() { return heap.live_blocks == 0 && heap.live_bytes == 0; }

#if EMBEDDED_H3_LOCATION_TEST_COUNTING
constexpr std::size_t kHeader = alignof(std::max_align_t);

void *take(std::size_t size, bool zero) {
  if (heap.allocations++ == heap.fail_at)
    return nullptr;
  auto *block = static_cast<unsigned char *>(std::malloc(kHeader + size));
  if (block == nullptr)
    return nullptr;
  std::memcpy(block, &size, sizeof size);
  if (zero)
    std::memset(block + kHeader, 0, size);
  heap.live_bytes += size;
  heap.peak_bytes = std::max(heap.peak_bytes, heap.live_bytes);
  ++heap.live_blocks;
  heap.largest = std::max(heap.largest, size);
  if (size <= kAlwaysInternal) {
    heap.small_live += size;
    heap.small_peak = std::max(heap.small_peak, heap.small_live);
    ++heap.small_allocations;
  }
  return block + kHeader;
}

std::size_t size_of(void *pointer) {
  std::size_t size = 0;
  std::memcpy(&size, static_cast<unsigned char *>(pointer) - kHeader,
              sizeof size);
  return size;
}

void give(void *pointer) {
  if (pointer == nullptr)
    return;
  const std::size_t size = size_of(pointer);
  heap.live_bytes -= size;
  --heap.live_blocks;
  if (size <= kAlwaysInternal)
    heap.small_live -= size;
  std::free(static_cast<unsigned char *>(pointer) - kHeader);
}
#endif

} // namespace

#if EMBEDDED_H3_LOCATION_TEST_COUNTING
// libc semantics, as heap_caps_* on ESP-IDF give them.
extern "C" {
void *eh3l_test_malloc(std::size_t size) { return take(size, false); }
void *eh3l_test_calloc(std::size_t count, std::size_t size) {
  std::size_t bytes = 0;
  if (__builtin_mul_overflow(count, size, &bytes))
    return nullptr;
  return take(bytes, true);
}
void *eh3l_test_realloc(void *pointer, std::size_t size) {
  if (pointer == nullptr)
    return take(size, false);
  if (size == 0) {
    give(pointer);
    return nullptr;
  }
  void *moved = take(size, false);
  if (moved == nullptr)
    return nullptr; // the old block stays, as libc realloc leaves it
  std::memcpy(moved, pointer, std::min(size, size_of(pointer)));
  give(pointer);
  return moved;
}
void eh3l_test_free(void *pointer) { give(pointer); }
}
#endif

namespace {

// ---- Helpers ----

template <typename T>
bool fails_with(const std::expected<T, Error> &result, Error error) {
  return !result.has_value() && result.error() == error;
}

Cell cell_at(double lat, double lng, int res) {
  const auto cell = loc::from_degrees(lat, lng, res);
  CHECK(cell.has_value());
  return cell.value_or(0);
}

std::vector<Cell> disk(Cell origin, int k) {
  std::int64_t size = 0;
  CHECK(maxGridDiskSize(k, &size) == E_SUCCESS);
  std::vector<Cell> out(static_cast<std::size_t>(size));
  CHECK(gridDisk(origin, k, out.data()) == E_SUCCESS);
  std::erase(out, Cell{0});
  std::ranges::sort(out);
  return out;
}

struct Buffers {
  std::vector<LngLat> points;
  std::vector<Ring> rings;
  std::vector<Polygon> polygons;
  Buffers(std::size_t cells, int res) {
    const auto capacity = loc::multipolygon_capacity(cells, res);
    CHECK(capacity.has_value());
    if (!capacity)
      return;
    points.resize(capacity->points);
    rings.resize(capacity->rings);
    polygons.resize(capacity->polygons);
  }
};

std::expected<MultiPolygon, Error> run(std::span<const Cell> cells,
                                       Buffers &buffers) {
  return loc::cells_to_multipolygon(cells, buffers.points, buffers.rings,
                                    buffers.polygons);
}

// Twice the signed area in the lng/lat plane, positive counterclockwise.
double signed_area(const MultiPolygon &view, const Ring &ring) {
  double sum = 0;
  for (std::uint32_t i = 0; i + 1 < ring.count; ++i) {
    const LngLat &a = view.points[ring.first + i];
    const LngLat &b = view.points[ring.first + i + 1];
    sum += a.longitude * b.latitude - b.longitude * a.latitude;
  }
  return sum;
}

bool closed(const MultiPolygon &view) {
  for (const Ring &ring : view.rings) {
    if (ring.count < 4)
      return false;
    const LngLat &first = view.points[ring.first];
    const LngLat &last = view.points[ring.first + ring.count - 1];
    if (first.longitude != last.longitude || first.latitude != last.latitude)
      return false;
  }
  return true;
}

// Rings follow one another and polygons cover every ring, in order.
bool packed(const MultiPolygon &view) {
  std::uint32_t point = 0;
  for (const Ring &ring : view.rings) {
    if (ring.first != point)
      return false;
    point += ring.count;
  }
  std::uint32_t ring = 0;
  for (const Polygon &polygon : view.polygons) {
    if (polygon.first_ring != ring || polygon.ring_count == 0)
      return false;
    ring += polygon.ring_count;
  }
  return point == view.points.size() && ring == view.rings.size();
}

// h3-js 4.5.0 lib/h3core.js radsToDegs, (rad * 180) / Math.PI.
double h3js_degrees(double radians) {
  return radians * 180.0 / std::numbers::pi;
}

// The output must equal, bit for bit, what cellsToLinkedMultiPolygon gives,
// which is the call h3-js 4.5.0 makes for cellsToMultiPolygon(cells, true).
// h3-js then closes each ring with its first point.
bool equals_linked(std::span<const Cell> cells, const MultiPolygon &view) {
  LinkedGeoPolygon linked{};
  if (cellsToLinkedMultiPolygon(cells.data(), static_cast<int>(cells.size()),
                                &linked) != E_SUCCESS)
    return false;
  bool same = true;
  std::size_t p = 0;
  std::size_t r = 0;
  // One ring against the closed ring at view.rings[r].
  const auto same_ring = [&](const LinkedGeoLoop &loop) {
    const LinkedLatLng *first = loop.first;
    if (r >= view.rings.size() || first == nullptr)
      return false;
    const Ring ring = view.rings[r];
    std::size_t count = 0;
    bool equal = true;
    for (const LinkedLatLng *v = first; v != nullptr && equal;
         v = v->next, ++count) {
      const LngLat &point = view.points[ring.first + count];
      equal = count + 1 < ring.count &&
              point.longitude == h3js_degrees(v->vertex.lng) &&
              point.latitude == h3js_degrees(v->vertex.lat);
    }
    if (!equal || count + 1 != ring.count)
      return false;
    const LngLat &closing = view.points[ring.first + count];
    return closing.longitude == h3js_degrees(first->vertex.lng) &&
           closing.latitude == h3js_degrees(first->vertex.lat);
  };
  for (const LinkedGeoPolygon *polygon = &linked; polygon != nullptr && same;
       polygon = polygon->next, ++p) {
    std::size_t rings_here = 0;
    for (const LinkedGeoLoop *loop = polygon->first; loop != nullptr && same;
         loop = loop->next, ++r, ++rings_here)
      same = same_ring(*loop);
    same = same && p < view.polygons.size() &&
           rings_here == view.polygons[p].ring_count;
  }
  same = same && p == view.polygons.size() && r == view.rings.size();
  destroyLinkedMultiPolygon(&linked);
  return same;
}

std::uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
std::uint64_t next_random() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}
double uniform(double low, double high) {
  return low + (high - low) * static_cast<double>(next_random() >> 11) /
                   static_cast<double>(1ULL << 53);
}

// ---- Cases ----

void capacity() {
  using loc::multipolygon_capacity;
  // Even resolutions, 6 points per cell plus 2 closing points.
  const auto even = multipolygon_capacity(3, 8);
  CHECK(even && even->points == 24 && even->rings == 6 && even->polygons == 3);
  // Odd (Class III) resolutions, 12 points per cell plus 2 closing points.
  const auto odd = multipolygon_capacity(3, 9);
  CHECK(odd && odd->points == 42 && odd->rings == 6 && odd->polygons == 3);
  const auto none = multipolygon_capacity(0, 0);
  CHECK(none && none->points == 0 && none->rings == 0 && none->polygons == 0);
  CHECK(multipolygon_capacity(1, 0).has_value());
  CHECK(multipolygon_capacity(1, 15).has_value());
  for (const int bad : {-1, 16, std::numeric_limits<int>::min(),
                        std::numeric_limits<int>::max()})
    CHECK(fails_with(multipolygon_capacity(1, bad), Error::InvalidResolution));
  // The resolution is checked before the size.
  CHECK(fails_with(multipolygon_capacity(SIZE_MAX, 16),
                   Error::InvalidResolution));
  // The largest count whose points still index with 32 bits fits, one more
  // does not, and huge counts never wrap.
  constexpr std::size_t kLimit = std::numeric_limits<std::uint32_t>::max();
  CHECK(loc::kMaxMultiPolygonEntries == kLimit);
  for (const int res : {8, 9}) {
    const std::size_t per_cell = res % 2 != 0 ? 14 : 8;
    const auto top = multipolygon_capacity(kLimit / per_cell, res);
    CHECK(top && top->points == kLimit / per_cell * per_cell &&
          top->points <= kLimit);
    CHECK(fails_with(multipolygon_capacity(kLimit / per_cell + 1, res),
                     Error::TooManyCells));
    CHECK(fails_with(multipolygon_capacity(SIZE_MAX, res), Error::TooManyCells));
    CHECK(fails_with(multipolygon_capacity(SIZE_MAX / per_cell + 1, res),
                     Error::TooManyCells));
  }
  // Usable at compile time. The values are checked at run time above, so a
  // mutant fails a test instead of the build.
  static_assert(loc::multipolygon_capacity(2, 8).has_value());
}

void single_cell() {
  const Cell cell = cell_at(30.0444, 31.2357, 8);
  const std::array cells{cell};
  Buffers buffers(1, 8);
  reset_heap();
  const auto view = run(cells, buffers);
  CHECK(heap_clean());
  CHECK(view.has_value());
  if (!view)
    return;
  CHECK(view->polygons.size() == 1 && view->rings.size() == 1 &&
        view->points.size() == 7);
  CHECK(closed(*view) && packed(*view));
  CHECK(signed_area(*view, view->rings[0]) > 0);
  // The views point into the caller's buffers.
  CHECK(view->points.data() == buffers.points.data());
  CHECK(view->rings.data() == buffers.rings.data());
  CHECK(view->polygons.data() == buffers.polygons.data());
  // Same corners as cellToBoundary, longitude first, in some rotation.
  CellBoundary boundary{};
  CHECK(cellToBoundary(cell, &boundary) == E_SUCCESS && boundary.numVerts == 6);
  for (int i = 0; i < boundary.numVerts; ++i) {
    bool found = false;
    for (std::size_t j = 0; j < 6; ++j)
      found = found ||
              (std::fabs(view->points[j].longitude -
                         h3js_degrees(boundary.verts[i].lng)) < 1e-12 &&
               std::fabs(view->points[j].latitude -
                         h3js_degrees(boundary.verts[i].lat)) < 1e-12);
    CHECK(found);
  }
  CHECK(equals_linked(cells, *view));
}

void shapes() {
  const Cell origin = cell_at(29.9688, 31.1342, 8);
  // Two neighbors make one ring of 10 corners.
  {
    const auto near = disk(origin, 1);
    std::vector<Cell> pair{origin, near[0] == origin ? near[1] : near[0]};
    std::ranges::sort(pair);
    Buffers buffers(2, 8);
    const auto view = run(pair, buffers);
    CHECK(view && view->polygons.size() == 1 && view->rings.size() == 1 &&
          view->points.size() == 11);
    CHECK(view && equals_linked(pair, *view));
  }
  // Far apart cells are separate polygons.
  {
    std::vector<Cell> apart{origin, cell_at(30.0444, 31.2357, 8),
                            cell_at(31.2001, 29.9187, 8)};
    std::ranges::sort(apart);
    Buffers buffers(apart.size(), 8);
    const auto view = run(apart, buffers);
    CHECK(view && view->polygons.size() == 3 && view->rings.size() == 3 &&
          closed(*view) && packed(*view));
    CHECK(view && equals_linked(apart, *view));
  }
  // The six around a cell enclose it, so one polygon with a hole.
  {
    auto ring = disk(origin, 1);
    std::erase(ring, origin);
    Buffers buffers(ring.size(), 8);
    const auto view = run(ring, buffers);
    CHECK(view && view->polygons.size() == 1 && view->rings.size() == 2 &&
          view->polygons[0].ring_count == 2);
    if (view) {
      CHECK(signed_area(*view, view->rings[0]) > 0); // outer counterclockwise
      CHECK(signed_area(*view, view->rings[1]) < 0); // hole clockwise
      CHECK(view->rings[1].count == 7);              // the center hexagon
      CHECK(closed(*view) && packed(*view));
      CHECK(equals_linked(ring, *view));
    }
  }
  // A pentagon at an even resolution has five corners.
  {
    std::array<H3Index, 12> pentagons{};
    CHECK(getPentagons(8, pentagons.data()) == E_SUCCESS);
    const std::array one{Cell{pentagons[0]}};
    Buffers buffers(1, 8);
    const auto view = run(one, buffers);
    CHECK(view && view->rings.size() == 1 && view->points.size() == 6);
    CHECK(view && equals_linked(one, *view));
  }
  // At odd (Class III) resolutions every pentagon edge crosses an icosahedron
  // edge and adds a distortion vertex, 10 corners and a closing point, more
  // than one point per edge allows.
  for (const int res : {1, 3, 5, 7, 9, 11, 13, 15}) {
    std::array<H3Index, 12> pentagons{};
    CHECK(getPentagons(res, pentagons.data()) == E_SUCCESS);
    for (const H3Index pentagon : pentagons) {
      const std::array one{Cell{pentagon}};
      Buffers buffers(1, res);
      const auto view = run(one, buffers);
      CHECK(view && view->rings.size() == 1 && view->points.size() == 11);
      CHECK(view && equals_linked(one, *view));
    }
  }
  // All 122 resolution 0 cells cover the globe. H3 returns it as 8 triangles,
  // so a ring of exactly 3 vertices must be accepted.
  {
    std::vector<Cell> globe(static_cast<std::size_t>(res0CellCount()));
    CHECK(getRes0Cells(globe.data()) == E_SUCCESS);
    std::ranges::sort(globe);
    Buffers buffers(globe.size(), 0);
    reset_heap();
    const auto view = run(globe, buffers);
    CHECK(heap_clean());
    CHECK(view && view->polygons.size() == 8 && view->rings.size() == 8 &&
          view->points.size() == 32 && closed(*view) && packed(*view));
    CHECK(view && equals_linked(globe, *view));
  }
}

void errors() {
  const Cell a = cell_at(30.0444, 31.2357, 8);
  const Cell b = cell_at(30.1, 31.3, 8);
  Buffers buffers(4, 8);
  const auto error_of = [&](std::vector<Cell> cells) {
    reset_heap();
    const auto view = run(cells, buffers);
    CHECK(heap_clean());
    CHECK(!view.has_value());
    return view.error_or(Error::H3Failure);
  };
  CHECK(error_of({}) == Error::NoCells);
  CHECK(error_of({0}) == Error::InvalidCell);
  // The reserved high bit set makes any index invalid (H3 isValidCell).
  CHECK(error_of({a, b | (1ULL << 63)}) == Error::InvalidCell);
  CHECK(error_of({a, cell_at(30.0444, 31.2357, 9)}) == Error::MixedResolution);
  CHECK(error_of({a, b, a}) == Error::DuplicateCell);
  // Empty input is refused before the buffers are looked at.
  CHECK(fails_with(loc::cells_to_multipolygon({}, {}, {}, {}), Error::NoCells));
}

void full_buffers() {
  const Cell origin = cell_at(29.9688, 31.1342, 8);
  auto cells = disk(origin, 1);
  std::erase(cells, origin);
  cells.push_back(cell_at(30.0444, 31.2357, 8));
  std::ranges::sort(cells);
  Buffers exact(cells.size(), 8);
  const auto view = run(cells, exact);
  CHECK(view.has_value());
  if (!view)
    return;
  const std::size_t points = view->points.size();
  const std::size_t rings = view->rings.size();
  const std::size_t polygons = view->polygons.size();
  const auto result_with = [&](std::size_t pn, std::size_t rn, std::size_t gn) {
    std::vector<LngLat> sp(pn);
    std::vector<Ring> sr(rn);
    std::vector<Polygon> sg(gn);
    reset_heap();
    const auto result = loc::cells_to_multipolygon(cells, sp, sr, sg);
    CHECK(heap_clean());
    return result.transform([](const MultiPolygon &) { return 0; });
  };
  CHECK(result_with(points, rings, polygons).has_value()); // exact size fits
  CHECK(fails_with(result_with(points - 1, rings, polygons),
                   Error::PointBufferFull));
  CHECK(fails_with(result_with(points, rings - 1, polygons),
                   Error::RingBufferFull));
  CHECK(fails_with(result_with(points, rings, polygons - 1),
                   Error::PolygonBufferFull));
  CHECK(fails_with(result_with(0, 0, 0), Error::PolygonBufferFull));
  CHECK(fails_with(result_with(0, 0, polygons), Error::RingBufferFull));
  CHECK(fails_with(result_with(0, rings, polygons), Error::PointBufferFull));
}

// Malformed H3 output, which H3 4.5.0 never returns, gives H3Failure. These
// reach detail::copy_multipolygon directly.
void malformed_output() {
  std::array<LatLng, 3> verts{LatLng{.lat = 0.1, .lng = 0.2},
                              LatLng{.lat = 0.1, .lng = 0.3},
                              LatLng{.lat = 0.2, .lng = 0.25}};
  // A value where H3's radsToDegs and the h3-js conversion differ in the last
  // bit, so the conversion below is pinned to h3-js.
  double odd = 0;
  for (int i = 1; i < 100000 && odd == 0; ++i) {
    const double radians = 0.0001 * i;
    if (radsToDegs(radians) != h3js_degrees(radians))
      odd = radians;
  }
  CHECK(odd != 0);
  verts[0].lng = odd;
  GeoLoop triangle{.numVerts = 3, .verts = verts.data()};
  GeoPolygon polygon{.geoloop = triangle, .numHoles = 0, .holes = nullptr};
  GeoMultiPolygon h3{.numPolygons = 1, .polygons = &polygon};
  std::array<LngLat, 8> points{};
  std::array<Ring, 2> rings{};
  std::array<Polygon, 1> polygons{};
  const auto copy = [&] {
    return loc::detail::copy_multipolygon(h3, points, rings, polygons);
  };
  const auto view = copy();
  CHECK(view && view->polygons.size() == 1 && view->rings.size() == 1 &&
        view->points.size() == 4 && closed(*view) && packed(*view));
  if (view) {
    CHECK(view->points[0].longitude == h3js_degrees(odd));
    CHECK(view->points[0].longitude != radsToDegs(odd));
    CHECK(view->points[2].latitude == h3js_degrees(0.2));
    CHECK(view->points[3].longitude == view->points[0].longitude &&
          view->points[3].latitude == view->points[0].latitude);
  }
  // A triangle with a triangular hole, both copied in order.
  polygon.numHoles = 1;
  polygon.holes = &triangle;
  const auto holed = copy();
  CHECK(holed && holed->polygons.size() == 1 &&
        holed->polygons[0].ring_count == 2 && holed->points.size() == 8 &&
        packed(*holed));
  polygon.numHoles = 0;
  polygon.holes = nullptr;

  h3.numPolygons = 0;
  CHECK(fails_with(copy(), Error::H3Failure));
  h3.numPolygons = -1;
  CHECK(fails_with(copy(), Error::H3Failure));
  h3.numPolygons = 1;
  h3.polygons = nullptr;
  CHECK(fails_with(copy(), Error::H3Failure));
  h3.polygons = &polygon;
  polygon.geoloop.numVerts = 2;
  CHECK(fails_with(copy(), Error::H3Failure));
  polygon.geoloop.numVerts = -3;
  CHECK(fails_with(copy(), Error::H3Failure));
  polygon.geoloop.numVerts = 3;
  polygon.geoloop.verts = nullptr;
  CHECK(fails_with(copy(), Error::H3Failure));
  polygon.geoloop.verts = verts.data();
  polygon.numHoles = -1;
  CHECK(fails_with(copy(), Error::H3Failure));
  polygon.numHoles = 1; // holes still null
  CHECK(fails_with(copy(), Error::H3Failure));
  polygon.numHoles = 0;
  CHECK(copy().has_value());
}

void out_of_memory() {
  if constexpr (!kCounting) {
    std::puts("test_multipolygon_malloc: plain malloc, no counting checks");
    return;
  }
  const Cell origin = cell_at(29.9688, 31.1342, 8);
  auto cells = disk(origin, 2);
  std::erase(cells, origin); // a hole
  cells.push_back(cell_at(30.0444, 31.2357, 8));
  std::ranges::sort(cells);
  Buffers buffers(cells.size(), 8);
  reset_heap();
  CHECK(run(cells, buffers).has_value());
  const std::size_t total = heap.allocations;
  CHECK(total > 4);
  CHECK(heap_clean());
  for (std::size_t index = 0; index < total; ++index) {
    reset_heap(index);
    CHECK(fails_with(run(cells, buffers), Error::OutOfMemory));
    CHECK(heap_clean());
  }
  std::printf("test_multipolygon: %zu allocations, each one failed in turn, "
              "no leak\n",
              total);
}

// One random disk with about a third of its cells dropped, to make holes and
// islands. Returns whether a set was checked.
bool random_set() {
  const int res = 5 + static_cast<int>(next_random() % 6); // 5..10
  const double lat = uniform(-89.9, 89.9);
  const double lng = uniform(-180.0, 180.0);
  auto cells =
      disk(cell_at(lat, lng, res), 1 + static_cast<int>(next_random() % 4));
  std::erase_if(cells, [](Cell) { return next_random() % 3 == 0; });
  if (cells.empty())
    return false;
  Buffers buffers(cells.size(), res);
  reset_heap();
  const auto view = run(cells, buffers);
  CHECK(heap_clean());
  CHECK(view.has_value());
  CHECK(view && closed(*view) && packed(*view));
  CHECK(view && equals_linked(cells, *view));
  return true;
}

void random_sets() {
  std::size_t sets = 0;
  for (int round = 0; round < 300; ++round)
    sets += random_set() ? 1 : 0;
  // Pentagons at odd resolutions carry distortion corners, the worst case for
  // the capacity bound.
  for (const int res : {1, 3, 5, 7, 9}) {
    std::array<H3Index, 12> pentagons{};
    CHECK(getPentagons(res, pentagons.data()) == E_SUCCESS);
    for (const H3Index pentagon : pentagons) {
      const auto cells = disk(pentagon, 2);
      Buffers buffers(cells.size(), res);
      const auto view = run(cells, buffers);
      CHECK(view && equals_linked(cells, *view));
      ++sets;
    }
  }
  // Near both poles.
  for (const double lat : {89.99, -89.99}) {
    const auto cells = disk(cell_at(lat, 0, 8), 3);
    Buffers buffers(cells.size(), 8);
    const auto view = run(cells, buffers);
    CHECK(view && equals_linked(cells, *view));
    ++sets;
  }
  std::printf("test_multipolygon: %zu random, pentagon and pole sets equal "
              "cellsToLinkedMultiPolygon bit for bit\n",
              sets);
}

void worst_case(const char *name, const std::vector<Cell> &cells) {
  Buffers buffers(cells.size(), 8);
  reset_heap();
  const auto view = run(cells, buffers);
  CHECK(view.has_value());
  CHECK(heap_clean());
  if (!view || !kCounting)
    return;
  const auto capacity = loc::multipolygon_capacity(cells.size(), 8);
  CHECK(capacity.has_value());
  if (!capacity)
    return;
  std::printf("test_multipolygon heap %s: %zu cells, %zu polygons %zu rings "
              "%zu points, peak %zu B in %zu allocations, largest %zu B, %zu "
              "allocations <= %zu B with peak %zu B, caller buffers %zu B\n",
              name, cells.size(), view->polygons.size(), view->rings.size(),
              view->points.size(), heap.peak_bytes, heap.allocations,
              heap.largest, heap.small_allocations, kAlwaysInternal,
              heap.small_peak,
              capacity->points * sizeof(LngLat) +
                  capacity->rings * sizeof(Ring) +
                  capacity->polygons * sizeof(Polygon));
}

void worst_cases() {
  // 720 distinct cells, the most one OpenSensor Parquet file holds.
  constexpr std::size_t kCells = 720;
  // Every cell its own polygon, the most loops and allocations.
  std::vector<Cell> apart;
  for (std::size_t i = 0; apart.size() < kCells; ++i)
    apart.push_back(cell_at(20.0 + 0.05 * static_cast<double>(i / 30),
                            25.0 + 0.05 * static_cast<double>(i % 30), 8));
  std::ranges::sort(apart);
  CHECK(std::ranges::adjacent_find(apart) == apart.end());
  worst_case("720 apart", apart);
  // One solid blob.
  auto blob = disk(cell_at(30.0444, 31.2357, 8), 15); // 721 cells
  blob.resize(kCells);
  worst_case("720 blob", blob);
  // Every other cell of a large disk, many holes.
  const auto big = disk(cell_at(30.0444, 31.2357, 8), 22);
  std::vector<Cell> sparse;
  for (std::size_t i = 0; i < big.size() && sparse.size() < kCells; i += 2)
    sparse.push_back(big[i]);
  worst_case("720 sparse", sparse);
}

static_assert(std::is_trivially_copyable_v<LngLat> &&
              std::is_trivially_copyable_v<Ring> &&
              std::is_trivially_copyable_v<Polygon>);

} // namespace

int main() {
  capacity();
  single_cell();
  shapes();
  errors();
  full_buffers();
  malformed_output();
  out_of_memory();
  random_sets();
  worst_cases();
  std::puts(kCounting ? "test_multipolygon: ALL TESTS PASSED"
                      : "test_multipolygon_malloc: ALL TESTS PASSED");
  return 0;
}
