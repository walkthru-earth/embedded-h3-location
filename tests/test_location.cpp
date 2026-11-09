// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#include <embedded_h3/location.h>

#include "e7.h"
#include "h3_error.h"
#include "iso_alpha2.h"

#include <h3api.h>

#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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

namespace {

template <typename T>
bool fails_with(const std::expected<T, Error> &result, Error error) {
  return !result.has_value() && result.error() == error;
}

template <typename T>
bool holds(const std::expected<T, Error> &result, const T &value) {
  return result.has_value() && *result == value;
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// Test only bit helpers to build invalid indexes. The library never does this.
constexpr int kResShift = 52;
constexpr Cell kResMask = Cell{15} << kResShift;
constexpr Cell with_resolution(Cell cell, int res) {
  return (cell & ~kResMask) | (Cell(res) << kResShift);
}
constexpr Cell with_digit(Cell cell, int res, Cell digit) {
  const int shift = (15 - res) * 3;
  return (cell & ~(Cell{7} << shift)) | (digit << shift);
}

// Small deterministic generator so the sweep is identical on every run.
struct Lcg {
  std::uint64_t state;
  double next_unit() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) * 0x1.0p-53;
  }
};

// The constants mirror the macros, and the string is MAJOR.MINOR.PATCH. CMake
// checks the macros against the project, manifest and changelog versions.
void version() {
  static_assert(loc::kVersionMajor == EMBEDDED_H3_LOCATION_VERSION_MAJOR);
  static_assert(loc::kVersionMinor == EMBEDDED_H3_LOCATION_VERSION_MINOR);
  static_assert(loc::kVersionPatch == EMBEDDED_H3_LOCATION_VERSION_PATCH);
  std::array<char, 32> text{};
  const int length =
      std::snprintf(text.data(), text.size(), "%d.%d.%d", loc::kVersionMajor,
                    loc::kVersionMinor, loc::kVersionPatch);
  CHECK(length > 0 && static_cast<std::size_t>(length) < text.size());
  CHECK(loc::kVersion ==
        std::string_view(text.data(), static_cast<std::size_t>(length)));
  CHECK(loc::kVersion == std::string_view(EMBEDDED_H3_LOCATION_VERSION_STRING));
}

void error_names() {
  constexpr std::array all{
      Error::InvalidCell,     Error::InvalidResolution, Error::OutOfRange,
      Error::NonFinite,       Error::InvalidText,       Error::H3Failure,
      Error::TooCoarse,       Error::NoCells,           Error::MixedResolution,
      Error::DuplicateCell,   Error::TooManyCells,      Error::OutOfMemory,
      Error::PointBufferFull, Error::RingBufferFull,    Error::PolygonBufferFull};
  constexpr std::array<std::string_view, all.size()> names{
      "invalid_cell",      "invalid_resolution", "out_of_range",
      "non_finite",        "invalid_text",       "h3_failure",
      "too_coarse",        "no_cells",           "mixed_resolution",
      "duplicate_cell",    "too_many_cells",     "out_of_memory",
      "point_buffer_full", "ring_buffer_full",   "polygon_buffer_full"};
  for (std::size_t i = 0; i < all.size(); ++i) {
    CHECK(static_cast<std::size_t>(std::to_underlying(all[i])) == i + 1); // no zero, no success value
    CHECK(names[i] == loc::error_name(all[i]));
  }
  CHECK(std::string_view(loc::error_name(static_cast<Error>(0))) == "unknown");
  CHECK(std::string_view(loc::error_name(static_cast<Error>(200))) ==
        "unknown");
}

void h3_error_mapping() {
  using loc::detail::from_h3_error;
  CHECK(from_h3_error(E_CELL_INVALID) == Error::InvalidCell);
  CHECK(from_h3_error(E_RES_DOMAIN) == Error::InvalidResolution);
  CHECK(from_h3_error(E_LATLNG_DOMAIN) == Error::OutOfRange);
  CHECK(from_h3_error(E_RES_MISMATCH) == Error::MixedResolution);
  CHECK(from_h3_error(E_DUPLICATE_INPUT) == Error::DuplicateCell);
  CHECK(from_h3_error(E_MEMORY_BOUNDS) == Error::TooManyCells);
  CHECK(from_h3_error(E_MEMORY_ALLOC) == Error::OutOfMemory);
  for (std::uint32_t code = 0; code < H3_ERROR_END + 2; ++code) {
    if (code == E_CELL_INVALID || code == E_RES_DOMAIN ||
        code == E_LATLNG_DOMAIN || code == E_RES_MISMATCH ||
        code == E_DUPLICATE_INPUT || code == E_MEMORY_BOUNDS ||
        code == E_MEMORY_ALLOC)
      continue;
    CHECK(from_h3_error(code) == Error::H3Failure);
  }
  CHECK(from_h3_error(UINT32_MAX) == Error::H3Failure);
}

void official_fixtures() {
  // Official H3 latLngToCell documentation example.
  CHECK(holds(loc::from_degrees(45.0, 40.0, 2), Cell{0x822d57fffffffffULL}));
  CHECK(holds(loc::resolution(0x822d57fffffffffULL), 2));

  // Official cellToLatLng example, printed to ten decimals.
  const auto parsed = loc::parse_cell("85283473FFFFFFF");
  CHECK(holds(parsed, Cell{0x85283473fffffffULL}));
  std::array<char, loc::kCellTextBytes> text{};
  CHECK(loc::format_cell(*parsed, text).has_value());
  CHECK(std::string_view(text.data()) == "85283473fffffff");
  const auto center = loc::center_degrees(*parsed);
  CHECK(center.has_value());
  CHECK(std::abs(center->latitude - 37.3457933754) < 1e-10);
  CHECK(std::abs(center->longitude - -121.9763759726) < 1e-10);
  const auto e7 = loc::center_e7(*parsed);
  CHECK(e7.has_value());
  CHECK(e7->latitude == 373457934 && e7->longitude == -1219763760);
  CHECK(holds(loc::coarsen(*parsed, 4), Cell{0x8428347ffffffffULL}));

  // Official quick start fixture in San Francisco.
  const auto fine =
      loc::from_degrees(37.775938728915946, -122.41795063018799, 9);
  CHECK(holds(fine, Cell{0x8928308280fffffULL}));
  CHECK(holds(loc::coarsen(*fine, 5), Cell{0x85283083fffffffULL}));
}

void parse_and_format() {
  const Cell cell = 0x85283473fffffffULL;
  for (std::string_view bad :
       {std::string_view{}, std::string_view{""}, std::string_view{"0"},
        std::string_view{"85283473ffffff"},   // 14 digits
        std::string_view{"085283473fffffff"}, // 16 digits
        std::string_view{"0x85283473fffff"}, std::string_view{"+85283473ffffff"},
        std::string_view{"-85283473ffffff"}, std::string_view{" 85283473ffffff"},
        std::string_view{"85283473ffffff "}, std::string_view{"85283473ffffffg"},
        std::string_view{"85283473fffffffjunk"},
        std::string_view{"85283473ffff\0ff", 15}})
    CHECK(fails_with(loc::parse_cell(bad), Error::InvalidText));

  // Fifteen hex digits that are not a cell, including a deleted subsequence.
  for (std::string_view not_cell :
       {"000000000000000", "81087ffffffffff", "fffffffffffffff",
        "05283473fffffff"})
    CHECK(fails_with(loc::parse_cell(not_cell), Error::InvalidCell));

  // A view into a longer buffer has no NUL after the 15 digits.
  constexpr char longer[] = "85283473fffffff85283473fffffff";
  CHECK(holds(loc::parse_cell(std::string_view(longer, 15)), cell));
  CHECK(holds(loc::parse_cell(std::string_view(longer + 15, 15)), cell));

  // char[16] and std::array<char, 16> both bind to the fixed extent span.
  char raw[loc::kCellTextBytes];
  std::memset(raw, 'x', sizeof(raw));
  CHECK(loc::format_cell(cell, raw).has_value());
  CHECK(raw[loc::kCellTextLength] == '\0');
  CHECK(std::strcmp(raw, "85283473fffffff") == 0);
  CHECK(holds(loc::parse_cell(raw), cell));

  // A shorter or longer buffer does not compile.
  static_assert(!std::is_convertible_v<char (&)[loc::kCellTextBytes - 1],
                                       std::span<char, loc::kCellTextBytes>>);
  static_assert(!std::is_convertible_v<char (&)[loc::kCellTextBytes + 1],
                                       std::span<char, loc::kCellTextBytes>>);

  // Round trip over every resolution.
  Cell fine = *loc::from_degrees(-33.8688, 151.2093, 15);
  for (int res = 0; res <= 15; ++res) {
    const Cell at = *loc::coarsen(fine, res);
    std::array<char, loc::kCellTextBytes> text{};
    CHECK(loc::format_cell(at, text).has_value());
    CHECK(std::strlen(text.data()) == loc::kCellTextLength);
    CHECK(holds(loc::parse_cell(text.data()), at));
  }
}

// Each corruption must be rejected by every function that takes a cell.
void invalid_cells() {
  const Cell cell = 0x85283473fffffffULL;
  for (Cell bad :
       {Cell{0}, Cell{0x81087ffffffffffULL}, cell | (Cell{1} << 63),
        cell | (Cell{1} << 56), (cell & ~(Cell{15} << 59)) | (Cell{2} << 59),
        cell & ~Cell{7}, cell | (Cell{7} << 30),
        (cell & ~(Cell{127} << 45)) | (Cell{127} << 45),
        with_resolution(cell, 4), std::numeric_limits<Cell>::max()}) {
    CHECK(!loc::valid_cell(bad));
    CHECK(fails_with(loc::resolution(bad), Error::InvalidCell));
    CHECK(fails_with(loc::coarsen(bad, 5), Error::InvalidCell));
    CHECK(fails_with(loc::parent_at(bad, 0), Error::InvalidCell));
    CHECK(fails_with(loc::center_degrees(bad), Error::InvalidCell));
    CHECK(fails_with(loc::center_e7(bad), Error::InvalidCell));
    std::array<char, loc::kCellTextBytes> text{};
    text.fill('x');
    CHECK(fails_with(loc::format_cell(bad, text), Error::InvalidCell));
    for (char c : text)
      CHECK(c == 'x'); // left unchanged on error
  }
}

void resolution_arguments() {
  const Cell cell = 0x85283473fffffffULL;
  for (int bad : {-1, 16, INT_MIN, INT_MAX}) {
    CHECK(fails_with(loc::coarsen(cell, bad), Error::InvalidResolution));
    CHECK(fails_with(loc::parent_at(cell, bad), Error::InvalidResolution));
    CHECK(fails_with(loc::from_degrees(0.0, 0.0, bad),
                     Error::InvalidResolution));
  }
  // coarsen checks the limit before the cell.
  CHECK(fails_with(loc::coarsen(0, 16), Error::InvalidResolution));
  CHECK(fails_with(loc::coarsen(0, 5), Error::InvalidCell));
  // parent_at checks the resolution, then the cell, then TooCoarse.
  CHECK(fails_with(loc::parent_at(0, 16), Error::InvalidResolution));
  CHECK(fails_with(loc::parent_at(0, 15), Error::InvalidCell));
  CHECK(fails_with(loc::parent_at(cell, 15), Error::TooCoarse));
  // Both ends of the range are accepted.
  CHECK(holds(loc::coarsen(cell, 0), Cell{0x8029fffffffffffULL}));
  CHECK(holds(loc::coarsen(cell, 15), cell));
  CHECK(holds(loc::resolution(*loc::from_degrees(0.0, 0.0, 0)), 0));
  CHECK(holds(loc::resolution(*loc::from_degrees(0.0, 0.0, 15)), 15));
}

void coordinate_bounds() {
  // Poles and the antimeridian are inside the range.
  for (int res : {0, 8, 15}) {
    for (double pole : {-90.0, 90.0})
      for (double lng : {-180.0, 0.0, 180.0})
        CHECK(loc::from_degrees(pole, lng, res).has_value());
    const auto west = loc::from_degrees(0.0, -180.0, res);
    const auto east = loc::from_degrees(0.0, 180.0, res);
    CHECK(west.has_value() && east.has_value() && *west == *east);
    CHECK(holds(loc::from_degrees(-0.0, -0.0, res), *loc::from_degrees(0.0, 0.0, res)));
  }
  const auto antimeridian = loc::center_degrees(*loc::from_degrees(0.0, 180.0, 5));
  CHECK(antimeridian.has_value());
  CHECK(std::abs(antimeridian->longitude) > 179.0 &&
        std::abs(antimeridian->longitude) <= 180.0);

  // Just outside each bound.
  for (double lat : {std::nextafter(90.0, kInf), std::nextafter(-90.0, -kInf),
                     91.0, -91.0, 1e300, -1e300})
    CHECK(fails_with(loc::from_degrees(lat, 0.0, 5), Error::OutOfRange));
  for (double lng : {std::nextafter(180.0, kInf),
                     std::nextafter(-180.0, -kInf), 181.0, -181.0, 360.0})
    CHECK(fails_with(loc::from_degrees(0.0, lng, 5), Error::OutOfRange));

  // NaN and both infinities, in either argument.
  for (double bad : {kNaN, -kNaN, kInf, -kInf}) {
    CHECK(fails_with(loc::from_degrees(bad, 0.0, 5), Error::NonFinite));
    CHECK(fails_with(loc::from_degrees(0.0, bad, 5), Error::NonFinite));
  }

  // Documented order. NonFinite, then OutOfRange, then InvalidResolution.
  CHECK(fails_with(loc::from_degrees(kNaN, 500.0, 16), Error::NonFinite));
  CHECK(fails_with(loc::from_degrees(0.0, kInf, -1), Error::NonFinite));
  CHECK(fails_with(loc::from_degrees(91.0, 0.0, 16), Error::OutOfRange));
  CHECK(fails_with(loc::from_degrees(0.0, -181.0, -1), Error::OutOfRange));
}

void pentagons() {
  std::array<H3Index, 12> bases{};
  CHECK(pentagonCount() == 12);
  CHECK(getPentagons(0, bases.data()) == E_SUCCESS);
  for (H3Index base : bases) {
    CHECK(loc::valid_cell(base) && isPentagon(base));
    // Center child at res 15, then every coarsening stays a pentagon.
    H3Index child = 0;
    CHECK(cellToCenterChild(base, 15, &child) == E_SUCCESS);
    for (int res = 0; res <= 15; ++res) {
      const auto parent = loc::coarsen(child, res);
      CHECK(parent.has_value() && isPentagon(*parent));
      CHECK(holds(loc::parent_at(child, res), *parent));
      CHECK(holds(loc::resolution(*parent), res));
      CHECK(loc::center_e7(*parent).has_value());
    }
    // Deleted K axis subsequence. The first non zero digit of a pentagon
    // descendant can never be 1.
    const Cell at1 = with_digit(with_resolution(base, 1), 1, 1);
    const Cell at3 = with_digit(
        with_digit(with_digit(with_resolution(base, 3), 1, 0), 2, 0), 3, 1);
    for (Cell deleted : {at1, at3}) {
      CHECK(!loc::valid_cell(deleted));
      CHECK(fails_with(loc::coarsen(deleted, 0), Error::InvalidCell));
      CHECK(fails_with(loc::center_degrees(deleted), Error::InvalidCell));
    }
    // The same digits under a non zero first digit are fine.
    CHECK(loc::valid_cell(with_digit(with_digit(with_digit(
        with_resolution(base, 3), 1, 0), 2, 2), 3, 1)));
  }
  CHECK(loc::valid_cell(0x80c3fffffffffffULL));
  CHECK(loc::valid_cell(0x81083ffffffffffULL));
}

// coarsen never discloses more than asked, never more than the input, and
// composing two limits equals applying the smaller one.
void coarsen_monotonicity() {
  Lcg rng{0x5eed};
  for (int sample = 0; sample < 64; ++sample) {
    const double lat = rng.next_unit() * 180.0 - 90.0;
    const double lng = rng.next_unit() * 360.0 - 180.0;
    const int input_res = sample % 16;
    const auto input = loc::from_degrees(lat, lng, input_res);
    CHECK(input.has_value());
    std::array<Cell, 16> at{};
    for (int a = 0; a <= 15; ++a) {
      const auto parent = loc::coarsen(*input, a);
      CHECK(parent.has_value());
      at[a] = *parent;
      CHECK(holds(loc::resolution(at[a]), a < input_res ? a : input_res));
      if (a >= input_res)
        CHECK(at[a] == *input); // raising the limit adds nothing
      const auto e7 = loc::center_e7(at[a]);
      const auto deg = loc::center_degrees(at[a]);
      CHECK(e7.has_value() && deg.has_value());
      CHECK(std::abs(deg->latitude - e7->latitude / 1e7) <= 5.1e-8);
      CHECK(std::abs(deg->longitude - e7->longitude / 1e7) <= 5.1e-8);
    }
    for (int a = 0; a <= 15; ++a)
      for (int b = 0; b <= 15; ++b) {
        CHECK(holds(loc::coarsen(at[a], b), at[a < b ? a : b]));
      }
  }
}

// parent_at gives the same cell at a fixed resolution for every finer input,
// keeps an input already at that resolution and refuses every coarser one.
void parent_at_fixed_resolution() {
  Lcg rng{0xbeef};
  for (int sample = 0; sample < 16; ++sample) {
    const double lat = rng.next_unit() * 180.0 - 90.0;
    const double lng = rng.next_unit() * 360.0 - 180.0;
    const auto finest = loc::from_degrees(lat, lng, 15);
    CHECK(finest.has_value());
    for (int target = 0; target <= 15; ++target) {
      H3Index expected = 0;
      CHECK(cellToParent(*finest, target, &expected) == E_SUCCESS);
      for (int input_res = 0; input_res <= 15; ++input_res) {
        H3Index input = 0;
        CHECK(cellToParent(*finest, input_res, &input) == E_SUCCESS);
        const auto out = loc::parent_at(input, target);
        if (input_res < target) {
          CHECK(fails_with(out, Error::TooCoarse));
        } else {
          CHECK(holds(out, Cell{expected}));
          CHECK(holds(loc::coarsen(input, target), Cell{expected}));
        }
      }
    }
  }
  // A firmware style policy at res 8, the official fixture.
  const Cell fine = 0x8928308280fffffULL; // res 9
  const auto at8 = loc::parent_at(fine, 8);
  CHECK(at8.has_value());
  CHECK(holds(loc::resolution(*at8), 8));
  CHECK(holds(loc::parent_at(*at8, 8), *at8));
  CHECK(fails_with(loc::parent_at(0x85283083fffffffULL, 8), Error::TooCoarse));
}

// Independent snapshot of the 249 assigned ISO 3166-1 alpha-2 codes, from
// Debian iso-codes v4.20.1 data/iso_3166-1.json (tag commit
// 74623235ae99f6e835e1e465017ce4c544ae6b53, file SHA-256
// f01b812b57fba9f31ff621bf33e7c7570a01964dbeb5be2167e94decf538c89f). Row n
// holds the second letters of the codes that start with letter n. The layout
// differs from src/iso_alpha2.h on purpose, so one copy error cannot hide in
// both.
constexpr std::array<std::string_view, 26> kSecondLettersByFirstLetter = {
    "DEFGILMOQRSTUWXZ",        // A
    "ABDEFGHIJLMNOQRSTVWYZ",   // B
    "ACDFGHIKLMNORUVWXYZ",     // C
    "EJKMOZ",                  // D
    "CEGHRST",                 // E
    "IJKMOR",                  // F
    "ABDEFGHILMNPQRSTUWY",     // G
    "KMNRTU",                  // H
    "DELMNOQRST",              // I
    "EMOP",                    // J
    "EGHIMNPRWYZ",             // K
    "ABCIKRSTUVY",             // L
    "ACDEFGHKLMNOPQRSTUVWXYZ", // M
    "ACEFGILOPRUZ",            // N
    "M",                       // O
    "AEFGHKLMNRSTWY",          // P
    "A",                       // Q
    "EOSUW",                   // R
    "ABCDEGHIJKLMNORSTVXYZ",   // S
    "CDFGHJKLMNORTVWZ",        // T
    "AGMSYZ",                  // U
    "ACEGINU",                 // V
    "FS",                      // W
    "",                        // X
    "ET",                      // Y
    "AMW",                     // Z
};

// Inputs whose product with 1e7 is exactly k + 0.5 in binary64, checked
// below, so the rounding rule decides the result. Half away from zero gives
// 1, 2, 3 and 1234568, where half to even would give 0, 2, 2 and 1234568,
// and truncation would give 0, 1, 2 and 1234567.
void e7_halfway() {
  struct Case {
    double degrees;
    std::int64_t e7;
  };
  constexpr std::array<Case, 5> cases{{
      {.degrees = 0.5e-7, .e7 = 1},
      {.degrees = 1.5e-7, .e7 = 2},
      {.degrees = 2.5e-7, .e7 = 3},
      {.degrees = 0.12345675, .e7 = 1234568},
      {.degrees = 45.00000025, .e7 = 450000003},
  }};
  for (const Case &c : cases) {
    const double scaled = c.degrees * 1e7;
    CHECK(scaled - std::floor(scaled) == 0.5);
    for (const double sign : {1.0, -1.0}) {
      const auto e7 = loc::detail::to_e7(
          {.latitude = sign * c.degrees, .longitude = -sign * c.degrees});
      CHECK(e7.latitude == static_cast<std::int64_t>(sign) * c.e7);
      CHECK(e7.longitude == -static_cast<std::int64_t>(sign) * c.e7);
    }
  }
}

void countries() {
  // Every uppercase pair, 676 in all, matches the snapshot exactly. A swapped,
  // missing or extra code in the library table fails here.
  std::size_t accepted = 0;
  for (char first = 'A'; first <= 'Z'; ++first) {
    for (char second = 'A'; second <= 'Z'; ++second) {
      const std::array<char, 2> pair{first, second};
      const std::string_view code{pair.data(), pair.size()};
      const bool assigned =
          kSecondLettersByFirstLetter[static_cast<std::size_t>(first - 'A')].find(
              second) != std::string_view::npos;
      CHECK(loc::valid_country(code) == assigned);
      accepted += assigned ? 1 : 0;
    }
  }
  CHECK(accepted == 249);
  CHECK(accepted == loc::detail::kIsoAlpha2Count);
  for (std::string_view code : {"EG", "US", "TW", "BQ", "SS", "GB", "AQ", "ZW"})
    CHECK(loc::valid_country(code));
  for (std::string_view code :
       {std::string_view{}, std::string_view{""}, std::string_view{"A"},
        std::string_view{"ZZ"}, std::string_view{"AA"}, std::string_view{"XK"},
        std::string_view{"UK"}, std::string_view{"QM"}, std::string_view{"us"},
        std::string_view{"Us"}, std::string_view{"USA"}, std::string_view{"12"},
        std::string_view{"E "}, std::string_view{"U\0", 2},
        std::string_view{"DZE"}, std::string_view{"ZA "}})
    CHECK(!loc::valid_country(code));
  // Views need no NUL. Two letters inside a longer buffer are a code.
  constexpr std::string_view usa = "USA";
  CHECK(loc::valid_country(usa.substr(0, 2)));
  CHECK(loc::valid_country(usa.substr(1, 2)));
}

// Every public call is noexcept and the result types carry no success error.
static_assert(noexcept(loc::valid_cell(0)));
static_assert(noexcept(loc::resolution(0)));
static_assert(noexcept(loc::parse_cell(std::string_view{})));
static_assert(noexcept(loc::coarsen(0, 0)));
static_assert(noexcept(loc::parent_at(0, 0)));
static_assert(noexcept(loc::center_degrees(0)));
static_assert(noexcept(loc::center_e7(0)));
static_assert(noexcept(loc::from_degrees(0, 0, 0)));
static_assert(noexcept(loc::valid_country(std::string_view{})));
static_assert(noexcept(loc::error_name(Error::InvalidCell)));
static_assert(noexcept(loc::multipolygon_capacity(0, 0)));
static_assert(noexcept(loc::cells_to_multipolygon({}, {}, {}, {})));
static_assert(std::is_same_v<std::underlying_type_t<Error>, std::uint8_t>);

} // namespace

int main() {
  version();
  error_names();
  h3_error_mapping();
  official_fixtures();
  parse_and_format();
  invalid_cells();
  resolution_arguments();
  coordinate_bounds();
  pentagons();
  coarsen_monotonicity();
  parent_at_fixed_resolution();
  e7_halfway();
  countries();
  std::puts("test_location: ALL TESTS PASSED");
  return 0;
}
