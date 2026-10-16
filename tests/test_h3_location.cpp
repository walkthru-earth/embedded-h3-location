#include "aq_location.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

using namespace aq::location;

int main() {
  // 1. Official Uber H3 indexing API example (not a wrapper-generated vector).
  Cell cell = 0;
  assert(from_degrees(45.0, 40.0, 2, cell));
  assert(cell == 0x822d57fffffffffULL);
  assert(resolution(cell) == 2);

  // 2. Canonical parsing & formatting of 15-hex-digit H3 cell strings.
  assert(parse_cell("85283473FFFFFFF", cell));
  assert(cell == 0x85283473fffffffULL);
  char text[kCellTextBytes]{};
  assert(format_cell(cell, text, sizeof(text)));
  assert(std::strcmp(text, "85283473fffffff") == 0);

  // 3. Centroid computation in degrees and integer 1e-7 degrees.
  double lat = 0.0, lon = 0.0;
  assert(center_degrees(cell, lat, lon));
  // Official H3 cellToLatLng documentation example, printed to ten decimals.
  assert(std::abs(lat - 37.3457933754) < 1e-10);
  assert(std::abs(lon - -121.9763759726) < 1e-10);
  std::int64_t lat_e7 = 0, lon_e7 = 0;
  assert(center_e7(cell, lat_e7, lon_e7));
  assert(lat_e7 == 373457934 && lon_e7 == -1219763760);

  // 4. Privacy resolution coarsening via H3 cellToParent.
  Cell parent = 0;
  assert(coarsen(cell, 4, parent));
  assert(parent == 0x8428347ffffffffULL);

  // 5. Reject malformed strings, prefixes, signs, whitespace, and short buffers.
  const Cell unchanged = cell;
  for (const char *bad :
       {"", "0", "085283473fffffff", "0x85283473fffffff", "+85283473fffffff",
        "85283473ffffffg", "85283473fffffff ", "85283473fffffffjunk"}) {
    assert(!parse_cell(bad, cell));
    assert(cell == unchanged);
  }
  assert(!parse_cell(nullptr, cell));
  assert(!format_cell(cell, nullptr, sizeof(text)));
  assert(!format_cell(cell, text, sizeof(text) - 1));

  // 6. Reject invalid H3 bit patterns (deleted pentagon subsequence, bad modes,
  // reserved bits, unused digit corruption).
  for (Cell bad :
       {0ULL, 0x81087ffffffffffULL, cell | (1ULL << 63), cell | (1ULL << 56),
        (cell & ~(15ULL << 59)) | (2ULL << 59), cell & ~7ULL,
        cell | (7ULL << 30), (cell & ~(127ULL << 45)) | (127ULL << 45)}) {
    assert(!valid_cell(bad));
    assert(resolution(bad) == -1);
    parent = unchanged;
    assert(!coarsen(bad, 5, parent));
    assert(parent == unchanged);
    lat = 111.0;
    lon = 222.0;
    assert(!center_degrees(bad, lat, lon));
    assert(lat == 111.0 && lon == 222.0);
    lat_e7 = 111;
    lon_e7 = 222;
    assert(!center_e7(bad, lat_e7, lon_e7));
    assert(lat_e7 == 111 && lon_e7 == 222);
  }
  assert(valid_cell(0x80c3fffffffffffULL)); // upstream res0 pentagon fixture
  assert(valid_cell(0x81083ffffffffffULL)); // valid center child of base4
  assert(!coarsen(cell, -1, parent));
  assert(!coarsen(cell, 16, parent));

  // 7. Irreversible privacy coarsening across resolutions 0..15.
  // Increasing maximum_resolution cannot reconstruct finer location data.
  Cell fine = 0, coarse = 0, raised = 0, lower = 0;
  assert(from_degrees(37.775938728915946, -122.41795063018799, 9, fine));
  assert(fine == 0x8928308280fffffULL); // official quick-start SF fixture
  assert(coarsen(fine, kDefaultResolution, coarse));
  assert(coarse == 0x85283083fffffffULL);
  assert(coarsen(coarse, 15, raised) && raised == coarse);
  assert(coarsen(coarse, 3, lower) && resolution(lower) == 3);
  assert(coarsen(lower, 5, raised) && raised == lower);
  for (int maximum = 0; maximum <= 15; ++maximum) {
    assert(coarsen(fine, maximum, parent));
    assert(resolution(parent) == (maximum < 9 ? maximum : 9));
    assert(center_degrees(parent, lat, lon));
    assert(center_e7(parent, lat_e7, lon_e7));
    assert(std::abs(lat - lat_e7 / 1e7) <= 5.1e-8);
    assert(std::abs(lon - lon_e7 / 1e7) <= 5.1e-8);
  }

  // 8. Antimeridian, poles, and invalid geographic coordinate bounds.
  Cell west = 0, east = 0;
  assert(from_degrees(0.0, -180.0, 5, west));
  assert(from_degrees(0.0, 180.0, 5, east));
  assert(west == east);
  assert(center_degrees(west, lat, lon));
  assert(std::abs(lon) > 179.0 && lon >= -180.0 && lon <= 180.0);
  for (double pole : {-90.0, 90.0})
    assert(from_degrees(pole, 180.0, 15, cell));
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -91.0, 91.0})
    assert(!from_degrees(invalid, 0.0, 5, cell));
  for (double invalid :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -181.0, 181.0})
    assert(!from_degrees(0.0, invalid, 5, cell));
  assert(!from_degrees(0.0, 0.0, -1, cell));
  assert(!from_degrees(0.0, 0.0, 16, cell));

  // 9. Assigned ISO 3166-1 alpha-2 country validation (249 assigned codes).
  for (const char *code :
       {"EG", "US", "TW", "BQ", "SS", "GB", "AQ", "DE", "JP", "ZW"})
    assert(valid_country(code));
  for (const char *code :
       {"", "A", "ZZ", "AA", "XK", "UK", "QM", "us", "USA", "12", "E "})
    assert(!valid_country(code));
  assert(!valid_country(nullptr));

  std::puts("test_h3_location: ALL TESTS PASSED");
  return 0;
}
