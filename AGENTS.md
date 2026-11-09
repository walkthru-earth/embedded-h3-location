# Agent map for embedded-h3-location

A stateless C++23 layer over the official Uber H3 C library, for ESP-IDF and
host CMake. It indexes, coarsens, formats and parses cells, gives centers and
checks ISO 3166-1 alpha-2 codes. The public API is one header,
`include/embedded_h3/location.h`, in `namespace embedded_h3::location`. The
CMake target is `walkthru_earth::embedded_h3_location`.

The library sets no privacy policy. The resolution is always a caller
argument with no default. A rule such as "publish at H3 res 8" belongs to the
consumer, for example the OpenSensor firmware, never to this repo.

## C++ rules

- C++23 with `__cpp_lib_expected >= 202211L` (GCC 13, LLVM 17 libc++), checked
  by the header. `CXX_STANDARD 23` with `CXX_STANDARD_REQUIRED` in both paths. Under
  ESP-IDF the component also appends `-std=gnu++23` last, because IDF 6.1
  adds `gnu++26` build wide. CI checks the last `-std` flag. Not C++26 yet.
- Fallible calls return `std::expected<T, Error>`. `Error` is an error only
  `enum class` with no success value. Never call `.value()`, it aborts under
  `-fno-exceptions`. Use `has_value()`, `*` or `value_or()`.
- `[[nodiscard]]` and `noexcept` on every public function. Fixed extent
  `std::span` for buffers. `std::string_view` for text, and never pass its
  `.data()` to a C API that wants a NUL, copy it first.
- No exceptions, no RTTI, no heap, no `std::format`, no iostream. Constant
  tables are `static constexpr`, checked with `consteval` where possible.
- Designated initializers in declaration order. `enum class` with
  `using enum` where it reads better.
- Never use `__FILE__` in our code. H3's `assert()` still does, the path map
  below handles it.

## Vendored H3

- `vendor/h3/` holds unmodified files from an official uber/h3 release tag,
  now v4.5.0. CMake checks each file against `vendor/h3/manifest.json`,
  stops on any unlisted file apart from `.DS_Store`, and compiles only the
  `.c` files the manifest lists.
- Never vendor `master` or a branch, even for a known fix. Unreleased fixes
  are listed in `README.md` and `CHANGELOG.md` until a release ships them.
- `tools/check_h3_release.sh` warns on pull requests and fails the weekly CI
  run when uber/h3 has a newer release.

Upgrade steps.

1. Read the upstream `CHANGELOG.md` for the new tag.
2. Run `tools/vendor_h3.sh vX.Y.Z`. It accepts only release tags, checks
   `VERSION`, stages the new tree next to `vendor/h3` and swaps it in only
   after every copy and the manifest succeed.
3. Review `git diff vendor/`, then update `NOTICE`, `README.md`,
   `CHANGELOG.md` and this file.
4. Run the gates below and the ESP-IDF example build.

`cellsToMultiPolygon` is internal in H3 4.5.0, declared in
`cellsToMultiPoly.h`, not in `h3api.h`. It allocates through `H3_MEMORY`
(`malloc`, `calloc`, `realloc`, `free`). Any wrapper we add must document
that heap use in the header and must work with `H3_ALLOC_PREFIX`, so a
consumer can route H3 allocations, for example to PSRAM. No current API
allocates.

## Reproducible builds

`EMBEDDED_H3_LOCATION_MAP_PATHS` (on by default) adds `-ffile-prefix-map` for
the checkout, which is public, and for the build directory, which comes last.
CI builds `examples/basic` from two paths with
`CONFIG_APP_REPRODUCIBLE_BUILD=y`, compares the images and greps them for the
paths. The component manager needs the checkout folder to be named
`embedded-h3-location` for `override_path`.

## Verification status

Only ESP32-S3 on ESP-IDF 6.1 (commit `fff9895c82d744c7237be8847347bdd1b07c6643`,
the firmware pin) is verified. Do not claim C3, C6, P4 or any other chip in
docs, the manifest or CI until a board of that chip has a dated bench record.
Host tests do not prove hardware behavior.

## Gates

Run all of these before a pull request.

- `cmake --preset dev && cmake --build --preset dev && ctest --preset dev`
- the same with the `release` preset
- `tools/mutation_test.sh`. Every guard mutant must be killed. A new guard
  needs a new mutant. An equivalent mutant needs a written reason.
- the ESP-IDF build of `examples/basic` for `esp32s3`

CI runs the same jobs. Every action is pinned by full SHA with a version
comment, and the `espressif/idf` image by digest.

## Licensing

Our code is CC-BY-4.0 with Walkthru.Earth attribution. Vendored H3 keeps its
Apache-2.0 `LICENSE` and `NOTICE`. The manifest says
`CC-BY-4.0 AND Apache-2.0`. When the H3 version changes, update `NOTICE` and
the third-party table in `README.md`.

## Releases

1. Bump `version` in `idf_component.yml` and `project(... VERSION)` in
   `CMakeLists.txt`.
2. Move the `CHANGELOG.md` section from Unreleased to the date.
3. Merge to `main` through a pull request with green CI.
4. Only the owner tags `vX.Y.Z` and creates the GitHub release. Agents never
   tag, release or push to `main`.

## Writing and facts

- No em dashes. In prose, no colons or semicolons. Short plain sentences.
- Verify every fact from a primary source, such as the pinned SDK, upstream
  source at the tag or official docs. Never guess. List the sources in the
  pull request and say plainly what could not be confirmed.
