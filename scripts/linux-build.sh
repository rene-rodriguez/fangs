#!/bin/bash
# =============================================================================
# Fangs — Linux build
# =============================================================================
# Linux needs none of the macOS SDK workaround — Zig links the system libc
# directly. This script just guarantees the right Zig (0.15.2) is in front and
# runs the normal CMake/Ninja build. Distros ship Zig 0.16.0, which won't build
# the pinned libghostty-vt, so we vendor 0.15.2 unless PATH already has it.
#
#   Usage: scripts/linux-build.sh [Debug|Release]   (default: Release)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$ROOT/vendor"
ZIG_VERSION="0.15.2"

case "$(uname -m)" in
  arm64|aarch64) ZARCH=aarch64 ;;
  x86_64)        ZARCH=x86_64 ;;
  *) echo "ERROR: unsupported arch '$(uname -m)'"; exit 1 ;;
esac
ZIG_DIR="$VENDOR/zig-$ZARCH-linux-$ZIG_VERSION"

# Ensure a usable Zig (vendors 0.15.2 if PATH doesn't already provide it).
"$ROOT/scripts/bootstrap-vendor.sh"

# Prefer the vendored toolchain when bootstrap installed one; otherwise rely on
# the 0.15.2 already on PATH (bootstrap verified it).
if [ -x "$ZIG_DIR/zig" ]; then
  export PATH="$ZIG_DIR:$PATH"
fi

echo "==> Using zig -> $(command -v zig) ($(zig version))"
if [ "$(zig version)" != "$ZIG_VERSION" ]; then
  echo "ERROR: expected zig $ZIG_VERSION, got $(zig version)"; exit 1
fi

cd "$ROOT"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE="${1:-Release}"
cmake --build build

echo
echo "==> DONE. Binary: $ROOT/build/fangs"
file "$ROOT/build/fangs"
