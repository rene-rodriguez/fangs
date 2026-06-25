#!/bin/bash
# =============================================================================
# Fangs — vendor bootstrap (cross-platform)
# =============================================================================
# Ensures a Zig 0.15.2 toolchain is available to the CMake build. Everything
# else (raylib, libghostty-vt / ghostty) is pulled by CMake's FetchContent at
# configure time, so only Zig is handled here.
#
#   macOS : ALWAYS vendors Zig into ./vendor — scripts/macos-build.sh borrows
#           Zig's bundled libSystem.tbd to work around the Zig 0.15.2 <-> macOS
#           26 SDK linker gap, so a vendored copy is required even if PATH has
#           the right Zig.
#   Linux : if `zig` on PATH is already 0.15.2, nothing to do; otherwise vendors
#           Zig into ./vendor (distros ship 0.16.0, which won't build the pin).
#
# Idempotent: a second run is a no-op once the toolchain is in place. Safe to
# invoke directly or from macos-build.sh / linux-build.sh.
# =============================================================================
set -euo pipefail

ZIG_VERSION="0.15.2"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$ROOT/vendor"
mkdir -p "$VENDOR"

# --- Detect platform / arch ---------------------------------------------------
case "$(uname -s)" in
  Darwin) ZOS=macos ;;
  Linux)  ZOS=linux ;;
  *) echo "ERROR: unsupported OS '$(uname -s)'"; exit 1 ;;
esac
case "$(uname -m)" in
  arm64|aarch64) ZARCH=aarch64 ;;
  x86_64)        ZARCH=x86_64 ;;
  *) echo "ERROR: unsupported arch '$(uname -m)'"; exit 1 ;;
esac

ZIG_NAME="zig-${ZARCH}-${ZOS}-${ZIG_VERSION}"
ZIG_DIR="$VENDOR/$ZIG_NAME"
ZIG_BIN="$ZIG_DIR/zig"

# --- Linux: a matching Zig on PATH means we don't need to vendor anything ------
if [ "$ZOS" = "linux" ]; then
  if [ ! -x "$ZIG_BIN" ] && command -v zig >/dev/null 2>&1 \
     && [ "$(zig version 2>/dev/null)" = "$ZIG_VERSION" ]; then
    echo "==> zig $ZIG_VERSION already on PATH ($(command -v zig)) — nothing to vendor."
    exit 0
  fi
fi

# --- Already vendored? --------------------------------------------------------
if [ -x "$ZIG_BIN" ] && [ "$("$ZIG_BIN" version 2>/dev/null)" = "$ZIG_VERSION" ]; then
  echo "==> Zig $ZIG_VERSION already vendored at $ZIG_DIR"
  exit 0
fi

# --- Download -----------------------------------------------------------------
TARBALL="${ZIG_NAME}.tar.xz"
URL="https://ziglang.org/download/${ZIG_VERSION}/${TARBALL}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> Downloading $URL"
curl -fSL --retry 3 -o "$TMP/$TARBALL" "$URL"

# --- Verify against the shasum published in ziglang.org's download index ------
# (Integrity check vs a corrupted/truncated download; same HTTPS origin as the
#  tarball, so not a standalone supply-chain proof.)
sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
echo "==> Verifying SHA256"
EXPECTED="$(curl -fsSL https://ziglang.org/download/index.json \
  | grep -A1 "${TARBALL}\"" | grep '"shasum"' \
  | sed -E 's/.*"shasum": *"([0-9a-f]{64})".*/\1/' | head -n1 || true)"
ACTUAL="$(sha256_of "$TMP/$TARBALL")"
if [ -n "$EXPECTED" ]; then
  if [ "$EXPECTED" != "$ACTUAL" ]; then
    echo "ERROR: SHA256 mismatch for $TARBALL"
    echo "       expected $EXPECTED"
    echo "       actual   $ACTUAL"
    exit 1
  fi
  echo "    OK ($ACTUAL)"
else
  echo "    WARNING: could not read expected shasum from index.json."
  echo "             Proceeding (download was HTTPS from ziglang.org); got $ACTUAL"
fi

# --- Extract ------------------------------------------------------------------
# The tarball unpacks to a top-level "$ZIG_NAME/" dir, landing exactly at $ZIG_DIR.
echo "==> Extracting to $ZIG_DIR"
rm -rf "$ZIG_DIR"
tar -xf "$TMP/$TARBALL" -C "$VENDOR"
[ -x "$ZIG_BIN" ] || { echo "ERROR: extraction did not yield $ZIG_BIN"; exit 1; }

echo "==> Vendor ready: zig $("$ZIG_BIN" version) at $ZIG_BIN"
