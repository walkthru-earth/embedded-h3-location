#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-4.0
# Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#
# Replace vendor/h3 with the core library files of an official uber/h3 release
# tag and rewrite vendor/h3/manifest.json with their SHA-256 hashes. Files are
# copied byte for byte. Only release tags are accepted, never a branch.
#
# Usage: tools/vendor_h3.sh v4.5.0
set -euo pipefail

tag="${1:?usage: tools/vendor_h3.sh vX.Y.Z}"
if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: $tag is not a release tag like v4.5.0" >&2
    exit 1
fi

repo="$(cd "$(dirname "$0")/.." && pwd)"
dest="$repo/vendor/h3"
work="$(mktemp -d)"
# The new tree is built in a staging folder next to vendor/h3, so the final
# renames stay on one file system. vendor/h3 is replaced only after every copy
# and the manifest succeed. An interrupted swap restores the old tree.
stage="$repo/vendor/.h3-stage.$$"
old="$repo/vendor/.h3-old.$$"
cleanup() {
    if [[ -d "$old" && ! -d "$dest" ]]; then
        mv "$old" "$dest"
        echo "error: swap interrupted, restored the previous vendor/h3" >&2
    fi
    rm -rf "$work" "$stage" "$old"
}
trap cleanup EXIT

git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$tag" \
    https://github.com/uber/h3.git "$work/h3"
commit="$(git -C "$work/h3" rev-parse HEAD)"
version="$(head -n 1 "$work/h3/VERSION")"
if [[ "v$version" != "$tag" ]]; then
    echo "error: VERSION says $version but the tag is $tag" >&2
    exit 1
fi

mkdir -p "$stage/h3lib/include" "$stage/h3lib/lib"
cp "$work/h3/LICENSE" "$work/h3/NOTICE" "$work/h3/VERSION" "$stage/"
cp "$work/h3/src/h3lib/include/"*.h "$work/h3/src/h3lib/include/h3api.h.in" \
    "$stage/h3lib/include/"
cp "$work/h3/src/h3lib/lib/"*.c "$stage/h3lib/lib/"

python3 - "$stage" "$tag" "$commit" <<'PY'
import hashlib, json, pathlib, sys

dest, tag, commit = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
names = ["LICENSE", "NOTICE", "VERSION"] + sorted(
    p.relative_to(dest).as_posix()
    for p in (dest / "h3lib").rglob("*") if p.is_file())
manifest = {
    "upstream": "https://github.com/uber/h3",
    "tag": tag,
    "commit": commit,
    "license": "Apache-2.0",
    "files": {n: hashlib.sha256((dest / n).read_bytes()).hexdigest() for n in names},
}
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"staged uber/h3 {tag} at {commit}, {len(names)} files")
PY

[[ -d "$dest" ]] && mv "$dest" "$old"
mv "$stage" "$dest"
rm -rf "$old"
echo "vendor/h3 now holds uber/h3 $tag"

echo "Next, read the upstream CHANGELOG.md for $tag, update NOTICE, README.md,"
echo "CHANGELOG.md and AGENTS.md, then run the host tests and tools/mutation_test.sh."
