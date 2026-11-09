#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-4.0
# Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#
# Mutation proof for the guards in src/location.cpp. Each mutant breaks one
# guard in a scratch copy of the repo, rebuilds and runs the host tests. A
# killed mutant fails the tests. Each mutant keeps every name in use, so a
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

cp -R "$repo/CMakeLists.txt" "$repo/include" "$repo/src" "$repo/tests" \
    "$repo/vendor" "$work/"
cmake -S "$work" -B "$work/build" -DCMAKE_BUILD_TYPE="$build_type" \
    -DENABLE_SANITIZERS=ON >/dev/null
cmake --build "$work/build" --parallel >/dev/null
"$work/build/test_location" >/dev/null

cp "$work/src/location.cpp" "$work/location.cpp.orig"

python3 - "$work" <<'PY'
import pathlib, subprocess, sys

work = pathlib.Path(sys.argv[1])
source = work / "src" / "location.cpp"
original = (work / "location.cpp.orig").read_text()

# (name, old text, new text). Each old text must appear exactly once.
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
]

def run(old, new):
    if original.count(old) != 1:
        sys.exit(f"mutation text not found exactly once: {old!r}")
    source.write_text(original.replace(old, new))
    # Make 3.81 compares mtimes in whole seconds, so a mutant written within
    # the same second as the last build can look up to date. Deleting the
    # object forces a rebuild under both make and Ninja.
    for obj in (work / "build").rglob("location.cpp.o*"):
        obj.unlink()
    build = subprocess.run(["cmake", "--build", str(work / "build"), "--parallel"],
                           capture_output=True)
    if build.returncode != 0:
        sys.exit(f"mutant does not build, fix the mutation: {old!r}\n" + build.stderr.decode()[-2000:])
    test = subprocess.run([str(work / "build" / "test_location")], capture_output=True)
    return "survived" if test.returncode == 0 else "killed (test)"

failed = 0
print("Guard mutants, each must be killed")
for name, old, new in guards:
    result = run(old, new)
    print(f"  {result:15} {name}")
    failed += result == "survived"
print("Equivalent mutants, each must survive")
for name, old, new, why in equivalent:
    result = run(old, new)
    print(f"  {result:15} {name}. {why}")
    failed += result != "survived"
source.write_text(original)
print(f"{len(guards)} guard mutants, {len(equivalent)} equivalent mutants, {failed} unexpected")
sys.exit(1 if failed else 0)
PY
