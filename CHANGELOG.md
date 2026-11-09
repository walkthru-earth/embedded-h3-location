# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). The public API is
`include/embedded_h3/location.h`.

## [2.0.0] - Unreleased

Not tagged yet. Only the owner tags a release.

### Changed

- Breaking. The API moved to C++23. Every fallible call returns
  `std::expected<T, embedded_h3::location::Error>` in place of `bool` and an
  out parameter. Consumers need C++23 with `__cpp_lib_expected >= 202211L`,
  for example GCC 13 or LLVM 17 libc++. GCC 12 is refused by the header.
- Breaking. The header is now `<embedded_h3/location.h>` and the namespace is
  `embedded_h3::location`. `aq_location.h` and `aq::location` are gone, with
  no aliases. The CMake target and component name did not change.
- Breaking. `kDefaultResolution` is removed. `coarsen()` always takes the
  limit from the caller.
- Breaking. `resolution()` returns `Error::InvalidCell` in place of -1.
- Breaking. `parse_cell()` and `valid_country()` take `std::string_view`.
  `format_cell()` takes `std::span<char, 16>`, so a short buffer does not
  compile. `center_degrees()` and `center_e7()` return a struct.
- The component compiles its own C++ as `gnu++23` under ESP-IDF, so an SDK
  default change cannot move the dialect. The manifest requires ESP-IDF 6.1
  or later.

### Added

- `Error` enum with seven values and `error_name()` for logs.
- `parent_at(Cell, int)` returns the parent at exactly the given resolution
  and `Error::TooCoarse` for a coarser cell. It never refines a cell. The
  resolution is a caller argument, so a fixed rule such as H3 res 8 stays in
  the consumer.
- `-ffile-prefix-map` for this checkout and its build directory, on by default
  through `EMBEDDED_H3_LOCATION_MAP_PATHS`, so images hold no checkout path.
- Tests for every error value, every H3 error code mapping, resolution and
  coordinate bounds, pentagons and coarsening monotonicity.
- `tools/mutation_test.sh`, a mutation proof for each guard.
- `tools/vendor_h3.sh` to vendor an H3 release tag and rewrite the manifest.
  It stages the new tree and swaps it in only after a full copy.
- `tools/check_h3_release.sh` and a weekly CI job that fails once uber/h3 has
  a newer release or cannot be reached. Pull requests only get a warning.
- The CMake manifest check also stops on any file in `vendor/h3` that the
  manifest does not list.
- A country test that compares all 676 uppercase pairs with an independent
  snapshot from Debian iso-codes v4.20.1.
- CI jobs for the mutation proof and a two path reproducible build. Actions
  are pinned by SHA and the ESP-IDF image by digest.
- `.clangd`, `CMakePresets.json`, `AGENTS.md` and this changelog.

### H3

- Still Uber H3 v4.5.0, the newest release on 2026-11-09. These upstream
  fixes are merged on master but not released, so they are not included.
  - Latitude precision near the poles in `cellToLatLng` and `cellToBoundary`
    (uber/h3#1255). This affects `center_degrees()` and `center_e7()`.
  - Out of bounds write in `gridRing` with a negative `k` (uber/h3#1235).
    This library does not call `gridRing`.
  - Out of bounds read in `_baseCellToCCWrot60` (uber/h3#1224). Only vertex
    functions call it, and this library does not.
  - `dirFaces` initialized in `vertexRotations` (uber/h3#1273).

## [1.0.0] - 2026-10-16

### Added

- First release. C++17 `aq::location` API over unmodified Uber H3 v4.5.0
  with SHA-256 checked vendoring, cell coarsening, cell text, centers and
  ISO 3166-1 alpha-2 validation. ESP-IDF component and CMake library.

[2.0.0]: https://github.com/walkthru-earth/embedded-h3-location/compare/v1.0.0...v2-cpp23
[1.0.0]: https://github.com/walkthru-earth/embedded-h3-location/releases/tag/v1.0.0
