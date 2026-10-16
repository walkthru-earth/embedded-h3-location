#include "aq_location.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

// Always-evaluated check. Unlike CHECK(), it survives -DNDEBUG, so Release
// builds still run every API call under test and fail loudly on a mismatch.
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
                   #expr);                                                     \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

using namespace aq::location;

int main() {
  // 1. Official Uber H3 indexing API example (not a wrapper-generated vector).
  Cell cell = 0;
  CHECK(from_degrees(45.0, 40.0, 2, cell));
  CHECK(cell == 0x822d57fffffffffULL);
  CHECK(resolution(cell) == 2);

  // 2. Canonical parsing & formatting of 15-hex-digit H3 cell strings.
  CHECK(parse_cell("85283473FFFFFFF", cell));
  CHECK(cell == 0x85283473fffffffULL);
  char text[kCellTextBytes]{};
  CHECK(format_cell(cell, text, sizeof(text)));
  CHECK(std::strcmp(text, "85283473fffffff") == 0);

  // 3. Centroid computation in degrees and integer 1e-7 degrees.
  double lat = 0.0, lon = 0.0;
  CHECK(center_degrees(cell, lat, lon));
  // Official H3 cellToLatLng documentation example, printed to ten decimals.
  CHECK(std::abs(lat - 37.3457933754) < 1e-10);
  CHECK(std::abs(lon - -121.9763759726) < 1e-10);
  std::int64_t lat_e7 = 0, lon_e7 = 0;
  CHECK(center_e7(cell, lat_e7, lon_e7));
  CHECK(lat_e7 == 373457934 && lon_e7 == -1219763760);

  // 4. Privacy resolution coarsening via H3 cellToParent.
  Cell parent = 0;
  CHECK(coarsen(cell, 4, parent));
  CHECK(parent == 0x8428347ffffffffULL);

  // 5. Reject malformed strings, prefixes, signs, whitespace, and short buffers.
  const Cell unchanged = cell;
  for (const char *bad :
       {"", "0", "085283473fffffff", "0x85283473fffffff", "+85283473fffffff",
        "85283473ffffffg", "85283473fffffff ", "85283473fffffffjunk"}) {
    CHECK(!parse_cell(bad, cell));
    CHECK(cell == unchanged);
  }
  CHECK(!parse_cell(nullptr, cell));
  CHECK(!format_cell(cell, nullptr, sizeof(text)));
  CHECK(!format_cell(cell, text, sizeof(text) - 1));

  // 6. Reject invalid H3 bit patterns (deleted pentagon subsequence, bad modes,
  // reserved bits, unused digit corruption).
  for (Cell bad :
       {0ULL, 0x81087ffffffffffULL, cell | (1ULL << 63), cell | (1ULL << 56),
        (cell & ~(15ULL << 59)) | (2ULL << 59), cell & ~7ULL,
        cell | (7ULL << 30), (cell & ~(127ULL << 45)) | (127ULL << 45)}) {
    CHECK(!valid_cell(bad));
    CHECK(resolution(bad) == -1);
    parent = unchanged;
    CHECK(!coarsen(bad, 5, parent));
    CHECK(parent == unchanged);
    lat = 111.0;
    lon = 222.0;
    CHECK(!center_degrees(bad, lat, lon));
    CHECK(lat == 111.0 && lon == 222.0);
    lat_e7 = 111;
    lon_e7 = 222;
    CHECK(!center_e7(bad, lat_e7, lon_e7));
    CHECK(lat_e7 == 111 && lon_e7 == 222);
  }
  CHECK(valid_cell(0x80c3fffffffffffULL)); // upstream res0 pentagon fixture
  CHECK(valid_cell(0x81083ffffffffffULL)); // valid center child of base4
  CHECK(!coarsen(cell, -1, parent));
  CHECK(!coarsen(cell, 16, parent));

  // 7. Irreversible privacy coarsening across resolutions 0..15.
  // Increasing maximum_resolution cannot reconstruct finer location data.
  Cell fine = 0, coarse = 0, raised = 0, lower = 0;
  CHECK(from_degrees(37.775938728915946, -122.41795063018799, 9, fine));
  CHECK(fine == 0x8928308280fffffULL); // official quick-start SF fixture
  CHECK(coarsen(fine, kDefaultResolution, coarse));
  CHECK(coarse == 0x85283083fffffffULL);
  CHECK(coarsen(coarse, 15, raised) && raised == coarse);
  CHECK(coarsen(coarse, 3, lower) && resolution(lower) == 3);
  CHECK(coarsen(lower, 5, raised) && raised == lower);
  for (int maximum = 0; maximum <= 15; ++maximum) {
    CHECK(coarsen(fine, maximum, parent));
    CHECK(resolution(parent) == (maximum < 9 ? maximum : 9));
    CHECK(center_degrees(parent, lat, lon));
    CHECK(center_e7(parent, lat_e7, lon_e7));
    CHECK(std::abs(lat - lat_e7 / 1e7) <= 5.1e-8);
    CHECK(std::abs(lon - lon_e7 / 1e7) <= 5.1e-8);
  }

  // 8. Antimeridian, poles, and invalid geographic coordinate bounds.
  Cell west = 0, east = 0;
  CHECK(from_degrees(0.0, -180.0, 5, west));
  CHECK(from_degrees(0.0, 180.0, 5, east));
  CHECK(west == east);
  CHECK(center_degrees(west, lat, lon));
  CHECK(std::abs(lon) > 179.0 && lon >= -180.0 && lon <= 180.0);
  for (double pole : {-90.0, 90.0})
    CHECK(from_degrees(pole, 180.0, 15, cell));
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -91.0, 91.0})
    CHECK(!from_degrees(invalid, 0.0, 5, cell));
  for (double invalid :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -181.0, 181.0})
    CHECK(!from_degrees(0.0, invalid, 5, cell));
  CHECK(!from_degrees(0.0, 0.0, -1, cell));
  CHECK(!from_degrees(0.0, 0.0, 16, cell));

  // 9. Assigned ISO 3166-1 alpha-2 country validation (249 assigned codes).
  for (const char *code :
       {"EG", "US", "TW", "BQ", "SS", "GB", "AQ", "DE", "JP", "ZW"})
    CHECK(valid_country(code));
  for (const char *code :
       {"", "A", "ZZ", "AA", "XK", "UK", "QM", "us", "USA", "12", "E "})
    CHECK(!valid_country(code));
  CHECK(!valid_country(nullptr));

  std::puts("test_h3_location: ALL TESTS PASSED");
  return 0;
}
