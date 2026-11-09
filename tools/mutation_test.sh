#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-4.0
# Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#
# Mutation proof for the guards in src/location.cpp, src/multipolygon.cpp, and
# multipolygon_capacity and nonzero_alloc_size in
# include/embedded_h3/location.h. Each mutant breaks one guard in a scratch
# copy of the repo, rebuilds and runs every host test. A killed mutant fails
# the tests. Each mutant keeps every name in use, so a
# -Werror unused warning cannot be what kills it. Every guard mutant must be
# killed. Equivalent mutants are listed separately with the reason they cannot
# be killed, and they must survive, so the list stays honest.
#
# Usage: tools/mutation_test.sh [build type, default Debug]
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
build_type="${1:-Debug}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# CMakeLists.txt checks the version against idf_component.yml and CHANGELOG.md.
cp -R "$repo/CMakeLists.txt" "$repo/idf_component.yml" "$repo/CHANGELOG.md" \
    "$repo/include" "$repo/src" "$repo/tests" "$repo/vendor" "$work/"
cmake -S "$work" -B "$work/build" -DCMAKE_BUILD_TYPE="$build_type" \
    -DENABLE_SANITIZERS=ON >/dev/null
cmake --build "$work/build" --parallel >/dev/null
(cd "$work/build" && ctest --output-on-failure >/dev/null)

python3 - "$work" <<'PY'
import pathlib, subprocess, sys

work = pathlib.Path(sys.argv[1])
LOC = "src/location.cpp"
MP = "src/multipolygon.cpp"
HDR = "include/embedded_h3/location.h"
originals = {name: (work / name).read_text() for name in (LOC, MP, HDR)}

# Objects to delete so a mutant or its restore rebuilds. The header reaches
# every C++ object, the libraries and the tests.
objects = {LOC: "location.cpp.o*", MP: "multipolygon.cpp.o*", HDR: "*.cpp.o*"}

# Entries are (name, old text, new text) for src/location.cpp, or
# (file, name, old text, new text) for another file. Each old text must appear
# exactly once in its file.
guards = [
    ("valid_cell always true",
     "return isValidCell(cell) != 0;", "return isValidCell(cell) != 0 || true;"),
    ("resolution skips the cell check",
     "  if (!valid_cell(cell))\n    return unexpected(Error::InvalidCell);\n  return getResolution(cell);",
     "  return getResolution(cell);"),
    ("parse_cell skips the length check",
     "text.size() != kCellTextLength || ", ""),
    ("parse_cell skips the hex check",
     " || !std::ranges::all_of(text, is_hex)",
     " || (false && !std::ranges::all_of(text, is_hex))"),
    ("parse_cell skips the final cell check",
     "  if (!valid_cell(parsed))\n    return unexpected(Error::InvalidCell);\n", ""),
    ("format_cell skips the cell check",
     "    return unexpected(Error::InvalidCell);\n  // H3 asks for 17 bytes",
     "    {}\n  // H3 asks for 17 bytes"),
    ("format_cell drops the NUL",
     "copy_n(canonical.begin(), kCellTextBytes,",
     "copy_n(canonical.begin(), kCellTextLength,"),
    ("coarsen skips the resolution check",
     "  if (!valid_resolution(maximum_resolution))\n    return unexpected(Error::InvalidResolution);\n", ""),
    ("coarsen skips the cell check",
     "  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n  H3Index parent", "  H3Index parent"),
    ("coarsen asks H3 for the limit, not the min",
     "std::min(maximum_resolution, getResolution(input))", "maximum_resolution"),
    ("coarsen checks the cell before the resolution",
     "  if (!valid_resolution(maximum_resolution))\n    return unexpected(Error::InvalidResolution);\n  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n",
     "  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n  if (!valid_resolution(maximum_resolution))\n    return unexpected(Error::InvalidResolution);\n"),
    ("parent_at skips the resolution check",
     "  if (!valid_resolution(target))\n    return unexpected(Error::InvalidResolution);\n", ""),
    ("parent_at skips the cell check",
     "  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n  if (getResolution(input) < target)",
     "  if (getResolution(input) < target)"),
    ("parent_at skips the too coarse check",
     "  if (getResolution(input) < target)\n    return unexpected(Error::TooCoarse);\n", ""),
    ("parent_at refuses a cell already at the target",
     "getResolution(input) < target", "getResolution(input) <= target"),
    ("parent_at checks the cell before the resolution",
     "  if (!valid_resolution(target))\n    return unexpected(Error::InvalidResolution);\n  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n",
     "  if (!valid_cell(input))\n    return unexpected(Error::InvalidCell);\n  if (!valid_resolution(target))\n    return unexpected(Error::InvalidResolution);\n"),
    ("center_degrees skips the cell check",
     "  if (!valid_cell(cell))\n    return unexpected(Error::InvalidCell);\n  LatLng center", "  LatLng center"),
    ("to_e7 truncates the latitude",
     "std::llround(degrees.latitude * 1e7)",
     "static_cast<std::int64_t>(degrees.latitude * 1e7)"),
    ("to_e7 rounds the latitude half to even",
     "std::llround(degrees.latitude * 1e7)", "std::llrint(degrees.latitude * 1e7)"),
    ("to_e7 rounds the longitude half to even",
     "std::llround(degrees.longitude * 1e7)", "std::llrint(degrees.longitude * 1e7)"),
    ("to_e7 rounds the latitude half up",
     "std::llround(degrees.latitude * 1e7)",
     "std::llround(std::floor(degrees.latitude * 1e7 + 0.5))"),
    ("from_degrees skips the latitude finite check",
     "!std::isfinite(latitude) || ", ""),
    ("from_degrees skips the longitude finite check",
     " || !std::isfinite(longitude)", ""),
    ("from_degrees skips the south bound",
     "latitude < -90.0 || ", ""),
    ("from_degrees skips the north bound",
     "latitude > 90.0 || ", ""),
    ("from_degrees skips the west bound",
     "longitude < -180.0 ||", "false ||"),
    ("from_degrees skips the east bound",
     "      longitude > 180.0)", "      false)"),
    ("from_degrees rejects the north pole",
     "latitude > 90.0", "latitude >= 90.0"),
    ("from_degrees rejects the antimeridian",
     "longitude > 180.0", "longitude >= 180.0"),
    ("from_degrees checks range before finiteness",
     "  if (!std::isfinite(latitude) || !std::isfinite(longitude))\n    return unexpected(Error::NonFinite);\n  if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||\n      longitude > 180.0)\n    return unexpected(Error::OutOfRange);\n",
     "  if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||\n      longitude > 180.0)\n    return unexpected(Error::OutOfRange);\n  if (!std::isfinite(latitude) || !std::isfinite(longitude))\n    return unexpected(Error::NonFinite);\n"),
    ("valid_country matches a prefix",
     "detail::kIsoAlpha2.substr(i, 2) == country",
     "detail::kIsoAlpha2.substr(i, 2).starts_with(country)"),
    ("from_h3_error loses E_CELL_INVALID",
     "  case E_CELL_INVALID:\n    return Error::InvalidCell;\n", ""),
    ("from_h3_error loses E_RES_DOMAIN",
     "  case E_RES_DOMAIN:\n    return Error::InvalidResolution;\n", ""),
    ("from_h3_error loses E_LATLNG_DOMAIN",
     "  case E_LATLNG_DOMAIN:\n    return Error::OutOfRange;\n", ""),
    ("error_name changes a name",
     'return "h3_failure";', 'return "h3failure";'),
    ("error_name changes the too_coarse name",
     'return "too_coarse";', 'return "toocoarse";'),
    ("from_h3_error loses E_RES_MISMATCH",
     "  case E_RES_MISMATCH:\n    return Error::MixedResolution;\n", ""),
    ("from_h3_error loses E_DUPLICATE_INPUT",
     "  case E_DUPLICATE_INPUT:\n    return Error::DuplicateCell;\n", ""),
    ("from_h3_error loses E_MEMORY_BOUNDS",
     "  case E_MEMORY_BOUNDS:\n    return Error::TooManyCells;\n", ""),
    ("from_h3_error loses E_MEMORY_ALLOC",
     "  case E_MEMORY_ALLOC:\n    return Error::OutOfMemory;\n", ""),
    ("error_name changes the no_cells name",
     'return "no_cells";', 'return "nocells";'),
    ("error_name changes the polygon_buffer_full name",
     'return "polygon_buffer_full";', 'return "polygon_buffer";'),
    # cells_to_multipolygon and its copy.
    (MP, "cells_to_multipolygon skips the empty check",
     "  if (cells.empty())\n    return unexpected(Error::NoCells);\n", ""),
    (MP, "cells_to_multipolygon ignores the H3 status",
     "      status != E_SUCCESS)\n    return unexpected(detail::from_h3_error(status));\n  result.armed",
     "      false)\n    return unexpected(detail::from_h3_error(status));\n  result.armed"),
    (MP, "cells_to_multipolygon never frees the H3 result",
     "result.armed = true;", "result.armed = false;"),
    (MP, "copy accepts zero polygons",
     "h3.numPolygons <= 0", "h3.numPolygons < 0"),
    (MP, "copy skips the null polygons check",
     " || h3.polygons == nullptr", ""),
    (MP, "copy accepts a ring of 2 vertices",
     "loop.numVerts < 3", "loop.numVerts < 2"),
    (MP, "copy refuses a ring of 3 vertices",
     "loop.numVerts < 3", "loop.numVerts < 4"),
    (MP, "copy skips the null vertices check",
     " || loop.verts == nullptr", ""),
    (MP, "copy skips the negative holes check",
     "polygon.numHoles < 0 || ", ""),
    (MP, "copy skips the null holes check",
     " || (polygon.numHoles > 0 && polygon.holes == nullptr)", ""),
    (MP, "copy writes past the polygon buffer",
     "polygon_count == polygons.size()", "polygon_count > polygons.size()"),
    (MP, "copy refuses an exact polygon buffer",
     "polygon_count == polygons.size()", "polygon_count + 1 == polygons.size()"),
    (MP, "copy writes past the ring buffer",
     "ring_count == rings.size()", "ring_count > rings.size()"),
    (MP, "copy refuses an exact ring buffer",
     "ring_count == rings.size()", "ring_count + 1 == rings.size()"),
    (MP, "copy forgets the closing point in the bound",
     "vertices + 1 > points.size() - point_count",
     "vertices > points.size() - point_count"),
    (MP, "copy refuses an exact point buffer",
     "vertices + 1 > points.size() - point_count",
     "vertices + 2 > points.size() - point_count"),
    (MP, "copy closes the ring with the wrong point",
     "points[point_count++] = points[first];",
     "points[point_count++] = points[first + 1];"),
    (MP, "copy counts the ring without its closing point",
     ".count = static_cast<std::uint32_t>(vertices + 1)",
     ".count = static_cast<std::uint32_t>(vertices)"),
    (MP, "copy swaps longitude and latitude",
     ".longitude = degrees(loop.verts[i].lng),\n                                     .latitude = degrees(loop.verts[i].lat)",
     ".longitude = degrees(loop.verts[i].lat),\n                                     .latitude = degrees(loop.verts[i].lng)"),
    (MP, "copy drops the last hole",
     "for (int h = 0; h < polygon.numHoles; ++h)",
     "for (int h = 0; h + 1 < polygon.numHoles; ++h)"),
    (MP, "copy counts one ring per polygon",
     ".ring_count = static_cast<std::uint32_t>(ring_count - first_ring)",
     ".ring_count = static_cast<std::uint32_t>(std::min<std::size_t>(ring_count - first_ring, 1))"),
    (MP, "copy returns the whole point buffer",
     ".points = points.first(point_count)", ".points = points.first(points.size() + 0 * point_count)"),
    (MP, "degrees uses a rounded 180 / pi, as H3 radsToDegs does",
     "return radians * 180.0 / std::numbers::pi;",
     "return radians * (180.0 / std::numbers::pi);"),
    # multipolygon_capacity.
    (HDR, "capacity skips the low resolution bound",
     "if (resolution < kMinResolution || resolution > kMaxResolution)\n    return std::unexpected(Error::InvalidResolution);\n  const std::size_t points_per_edge",
     "if (resolution > kMaxResolution)\n    return std::unexpected(Error::InvalidResolution);\n  const std::size_t points_per_edge"),
    (HDR, "capacity skips the high resolution bound",
     "if (resolution < kMinResolution || resolution > kMaxResolution)\n    return std::unexpected(Error::InvalidResolution);\n  const std::size_t points_per_edge",
     "if (resolution < kMinResolution)\n    return std::unexpected(Error::InvalidResolution);\n  const std::size_t points_per_edge"),
    (HDR, "capacity gives odd resolutions one point per edge",
     "(resolution % 2 != 0) ? 2 : 1", "(resolution % 2 != 0) ? 1 : 1"),
    (HDR, "capacity drops a closing point",
     "6 * points_per_edge + 2", "6 * points_per_edge + 1"),
    (HDR, "capacity gives one ring per cell",
     ".rings = 2 * cells", ".rings = 1 * cells"),
    (HDR, "capacity skips the overflow check",
     "  if (cells > kMaxMultiPolygonEntries / points_per_cell)\n    return std::unexpected(Error::TooManyCells);\n", ""),
    (HDR, "capacity refuses the largest count that fits",
     "cells > kMaxMultiPolygonEntries / points_per_cell",
     "cells >= kMaxMultiPolygonEntries / points_per_cell"),
    # nonzero_alloc_size, which allocator hooks use so the globe survives an
    # allocator that returns NULL for zero bytes.
    (HDR, "nonzero_alloc_size passes zero through",
     "return size != 0 ? size : 1;", "return size != 0 ? size : 0;"),
    (HDR, "nonzero_alloc_size rounds every size to 1",
     "return size != 0 ? size : 1;", "return size != 0 ? 1 : 1;"),
]

# Mutants that no test can kill, with the reason.
equivalent = [
    ("from_degrees skips the resolution check",
     "  if (!valid_resolution(res))\n    return unexpected(Error::InvalidResolution);\n", "",
     "H3 latLngToCell returns E_RES_DOMAIN, which maps to the same InvalidResolution."),
    ("parse_cell ignores the stringToH3 status",
     "      status != E_SUCCESS)\n    return unexpected(detail::from_h3_error(status));\n  if (!valid_cell(parsed))",
     "      false)\n    return unexpected(detail::from_h3_error(status));\n  if (!valid_cell(parsed))",
     "stringToH3 fails only when sscanf finds no hex digits, and the shape check already proved 15."),
    ("format_cell ignores the h3ToString status",
     "      status != E_SUCCESS)\n    return unexpected(detail::from_h3_error(status));\n  std::ranges::copy_n",
     "      false)\n    return unexpected(detail::from_h3_error(status));\n  std::ranges::copy_n",
     "h3ToString fails only for a buffer under 17 bytes, and the buffer is a fixed 17."),
    ("coarsen ignores the cellToParent status",
     "      status != E_SUCCESS)\n    return unexpected(detail::from_h3_error(status));\n  return parent;",
     "      false)\n    return unexpected(detail::from_h3_error(status));\n  return parent;",
     "A valid cell and a parent resolution of at most its own never fail in cellToParent."),
    ("center_degrees ignores the cellToLatLng status",
     "if (const H3Error status = cellToLatLng(cell, &center); status != E_SUCCESS)",
     "if (const H3Error status = cellToLatLng(cell, &center); false)",
     "cellToLatLng fails only for an invalid cell, which the guard before it rejects."),
    ("from_degrees ignores the latLngToCell status",
     "      status != E_SUCCESS)\n    return unexpected(detail::from_h3_error(status));\n  return indexed;",
     "      false)\n    return unexpected(detail::from_h3_error(status));\n  return indexed;",
     "latLngToCell fails for non finite input or a bad resolution, both rejected first. Its last E_FAILED is marked ALWAYS in H3."),
    (MP, "copy uses buffers past 32 bit indexes",
     "buffer.first(std::min(buffer.size(), kMaxMultiPolygonEntries))",
     "buffer.first(buffer.size() + 0 * kMaxMultiPolygonEntries)",
     "Needs a buffer of more than 2^32 - 1 entries, 64 GiB of points, which no test host allocates."),
    (MP, "the H3 result is freed on errors too",
     "    if (armed)\n", "    if (true || armed)\n",
     "On every error cellsToMultiPolygon leaves polygons NULL and numPolygons 0 (H3 cellsToMultiPoly.c), so destroyGeoMultiPolygon frees only NULL."),
]

def split(entry, extra):
    # Pads a location.cpp entry with its file, so every entry is
    # (file, name, old, new) plus `extra` trailing fields.
    return entry if len(entry) == 4 + extra else (LOC, *entry)

dirty = set()

def run(path, old, new):
    original = originals[path]
    if original.count(old) != 1:
        sys.exit(f"mutation text not found exactly once in {path}: {old!r}")
    # Restore the file the last mutant changed, then mutate this one.
    for name in dirty:
        (work / name).write_text(originals[name])
    (work / path).write_text(original.replace(old, new))
    # Make 3.81 compares mtimes in whole seconds, so a mutant written within
    # the same second as the last build can look up to date. Deleting the
    # objects of both the restored and the mutated file forces a rebuild
    # under both make and Ninja.
    for name in dirty | {path}:
        for obj in (work / "build").rglob(objects[name]):
            obj.unlink()
    dirty.clear()
    dirty.add(path)
    build = subprocess.run(["cmake", "--build", str(work / "build"), "--parallel"],
                           capture_output=True)
    if build.returncode != 0:
        sys.exit(f"mutant does not build, fix the mutation: {old!r}\n" + build.stdout.decode()[-3000:])
    test = subprocess.run(["ctest", "--test-dir", str(work / "build")], capture_output=True)
    return "survived" if test.returncode == 0 else "killed (test)"

failed = 0
print("Guard mutants, each must be killed")
for path, name, old, new in (split(g, 0) for g in guards):
    result = run(path, old, new)
    print(f"  {result:15} {name}")
    failed += result == "survived"
print("Equivalent mutants, each must survive")
for path, name, old, new, why in (split(e, 1) for e in equivalent):
    result = run(path, old, new)
    print(f"  {result:15} {name}. {why}")
    failed += result != "survived"
for name in dirty:
    (work / name).write_text(originals[name])
print(f"{len(guards)} guard mutants, {len(equivalent)} equivalent mutants, {failed} unexpected")
sys.exit(1 if failed else 0)
PY
