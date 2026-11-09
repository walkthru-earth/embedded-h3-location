#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-4.0
# Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#
# Compare vendor/h3/VERSION with the newest uber/h3 release tag (vX.Y.Z).
# A newer tag prints a warning, and with --strict it also exits 1. When the
# tag list cannot be fetched, the default mode warns and exits 0, so a network
# problem does not fail pull request CI. With --strict it exits 2, so the
# weekly run shows it. Upgrade steps live in AGENTS.md. Needs only git and
# network access, no token.
#
# Usage: tools/check_h3_release.sh [--strict]
set -euo pipefail

strict=false
[[ "${1:-}" == "--strict" ]] && strict=true

warn() {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::warning title=H3 release::$1"
    else
        echo "warning: $1" >&2
    fi
}

repo="$(cd "$(dirname "$0")/.." && pwd)"
vendored="v$(head -n 1 "$repo/vendor/h3/VERSION")"
if ! tags="$(git ls-remote --tags --refs https://github.com/uber/h3.git 'v*')"; then
    warn "could not list uber/h3 tags, the release check did not run."
    $strict && exit 2
    exit 0
fi
latest="$(printf '%s\n' "$tags" \
    | sed -n 's|.*refs/tags/\(v[0-9]*\.[0-9]*\.[0-9]*\)$|\1|p' \
    | sort -t. -k1.2,1n -k2,2n -k3,3n | tail -n 1)"

if [[ -z "$latest" ]]; then
    warn "no uber/h3 release tag found, the release check did not run."
    $strict && exit 2
    exit 0
fi

newest="$(printf '%s\n%s\n' "$vendored" "$latest" \
    | sort -t. -k1.2,1n -k2,2n -k3,3n | tail -n 1)"
if [[ "$newest" == "$vendored" ]]; then
    echo "vendored H3 $vendored is the newest release tag ($latest)"
    exit 0
fi

warn "uber/h3 $latest is newer than the vendored $vendored. See the upgrade steps in AGENTS.md."
$strict && exit 1
exit 0
