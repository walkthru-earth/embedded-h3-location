#!/usr/bin/env bash
# SPDX-License-Identifier: CC-BY-4.0
# Copyright (c) 2025-2026 Walkthru.Earth (https://walkthru.earth)
#
# Compare vendor/h3/VERSION with the newest uber/h3 release tag (vX.Y.Z).
# A newer tag prints a warning, and with --strict it also exits 1. Upgrade
# steps live in AGENTS.md. Needs only git and network access, no token.
#
# Usage: tools/check_h3_release.sh [--strict]
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
vendored="v$(head -n 1 "$repo/vendor/h3/VERSION")"
latest="$(git ls-remote --tags --refs https://github.com/uber/h3.git 'v*' \
    | sed -n 's|.*refs/tags/\(v[0-9]*\.[0-9]*\.[0-9]*\)$|\1|p' \
    | sort -t. -k1.2,1n -k2,2n -k3,3n | tail -n 1)"

if [[ -z "$latest" ]]; then
    echo "error: no uber/h3 release tag found" >&2
    exit 2
fi

newest="$(printf '%s\n%s\n' "$vendored" "$latest" \
    | sort -t. -k1.2,1n -k2,2n -k3,3n | tail -n 1)"
if [[ "$newest" == "$vendored" ]]; then
    echo "vendored H3 $vendored is the newest release tag ($latest)"
    exit 0
fi

message="uber/h3 $latest is newer than the vendored $vendored. See the upgrade steps in AGENTS.md."
if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::warning title=H3 release::$message"
else
    echo "warning: $message" >&2
fi
[[ "${1:-}" == "--strict" ]] && exit 1
exit 0
