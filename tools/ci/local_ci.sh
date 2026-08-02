#!/usr/bin/env bash
# Local CI: configure + build + ctest for the Linux presets (M0-T6).
#
# Run this before pushing. It is the same sequence the GitHub workflow runs, so
# a green run here means a green run there — the point of having it at all is
# that "CI red on main => no new task claims" (11 §3), and finding out locally
# costs minutes instead of a round trip.
#
#   tools/ci/local_ci.sh                 # linux-x64 (CPU), Release
#   tools/ci/local_ci.sh --preset linux-cuda --config Debug
#   tools/ci/local_ci.sh --all           # every preset valid on this machine
#
# Exits non-zero on the first failure so callers can rely on the status.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

PRESETS=()
CONFIG="Release"
RUN_ALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset) PRESETS+=("$2"); shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --all)    RUN_ALL=1; shift ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "local_ci: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

if [[ $RUN_ALL -eq 1 ]]; then
    PRESETS=(linux-x64)
    # linux-cuda needs the toolkit; a preset that cannot configure is not a
    # failure of this build, so it is only added when nvcc is actually present.
    if command -v nvcc >/dev/null 2>&1; then
        PRESETS+=(linux-cuda)
    else
        echo "local_ci: nvcc not on PATH -> skipping linux-cuda (12 §2: CPU-only builds must work)"
    fi
elif [[ ${#PRESETS[@]} -eq 0 ]]; then
    PRESETS=(linux-x64)
fi

case "$CONFIG" in
    Debug)   SUFFIX="deb" ;;
    Release) SUFFIX="rel" ;;
    *) echo "local_ci: --config must be Debug or Release" >&2; exit 2 ;;
esac

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "local_ci: VCPKG_ROOT is not set. The Linux presets read the vcpkg" >&2
    echo "          toolchain from it (12 §1); only win-x64 hard-codes a path." >&2
    exit 2
fi

FAILED=0
for PRESET in "${PRESETS[@]}"; do
    BUILD_PRESET="${PRESET}-${SUFFIX}"
    echo "=== local_ci: ${PRESET} (${CONFIG}) ==="
    if cmake --preset "$PRESET" \
        && cmake --build --preset "$BUILD_PRESET" \
        && ctest --preset "$BUILD_PRESET"; then
        echo "--- local_ci: ${BUILD_PRESET} OK"
    else
        echo "--- local_ci: ${BUILD_PRESET} FAILED" >&2
        FAILED=1
    fi
done

if [[ $FAILED -ne 0 ]]; then
    echo "local_ci: FAILED" >&2
    exit 1
fi
echo "local_ci: OK"
