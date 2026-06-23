#!/bin/bash
# =============================================================================
# Nova Terminal — macOS build bootstrap (Phase 0a workaround)
# =============================================================================
# WHY THIS EXISTS
#   ghostling builds libghostty-vt with Zig 0.15.2. On macOS 26.x / Xcode 26,
#   Zig 0.15.2's self-hosted Mach-O linker cannot parse the system SDK's
#   libSystem.tbd, so `zig build lib-vt` fails to link with:
#       undefined symbol: __availability_version_check, _fork, _malloc, ...
#   Zig SHIPS its own parseable libSystem.tbd. The fix is to feed Zig a
#   "hybrid SDK": the real macOS SDK (for headers + frameworks, so ghostty's
#   build.zig `findNative` succeeds) but with libSystem.tbd swapped for Zig's
#   bundled one (which Zig's linker can parse). We deliver it to Zig via an
#   `xcrun` shim on PATH so BOTH findNative and the linker's -syslibroot use it.
#   clang/Apple-ld (which CAN parse the real tbd) are unaffected at runtime;
#   the produced binary links the REAL /usr/lib/libSystem.B.dylib.
#
#   NOTE: This is a workaround for a bleeding-edge-OS toolchain gap. It can be
#   deleted once upstream Zig handles the macOS 26 SDK (or ghostty bumps Zig).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$ROOT/vendor"
ZIG_DIR="$VENDOR/zig-aarch64-macos-0.15.2"          # Zig 0.15.2 (see README/spec)
ZIG_BIN="$ZIG_DIR/zig"
GHOSTLING="$VENDOR/ghostling"
FAKE="$VENDOR/fakesdk"
ZIGSHIM="$VENDOR/zigshim"
XCRUNSHIM="$VENDOR/xcrunshim"

[ -x "$ZIG_BIN" ] || { echo "ERROR: Zig 0.15.2 not found at $ZIG_BIN"; exit 1; }
[ -d "$GHOSTLING" ] || { echo "ERROR: ghostling checkout not found at $GHOSTLING"; exit 1; }

# Resolve the REAL SDK via the system xcrun (bypass any shim on PATH).
REALSDK="$(/usr/bin/xcrun --sdk macosx --show-sdk-path)"
ZIG_TBD="$ZIG_DIR/lib/libc/darwin/libSystem.tbd"
echo "==> Real SDK:      $REALSDK"
echo "==> Zig tbd:       $ZIG_TBD"

# --- 1. Build the hybrid SDK: real SDK + Zig's parseable libSystem.tbd --------
echo "==> Building hybrid SDK at $FAKE"
rm -rf "$FAKE"; mkdir -p "$FAKE/usr/lib"
for f in "$REALSDK"/usr/lib/*; do ln -sfn "$f" "$FAKE/usr/lib/$(basename "$f")"; done
rm -f "$FAKE/usr/lib/libSystem.tbd"
cp "$ZIG_TBD" "$FAKE/usr/lib/libSystem.tbd"          # the one swap that matters
ln -sfn "$REALSDK/usr/include" "$FAKE/usr/include"
ln -sfn "$REALSDK/System" "$FAKE/System"
cp "$REALSDK/SDKSettings.json"  "$FAKE/" 2>/dev/null || true
cp "$REALSDK/SDKSettings.plist" "$FAKE/" 2>/dev/null || true

# --- 2. Shims ----------------------------------------------------------------
echo "==> Writing zig + xcrun shims"
mkdir -p "$ZIGSHIM" "$XCRUNSHIM"
cat > "$ZIGSHIM/zig" <<EOF
#!/bin/sh
exec "$ZIG_BIN" "\$@"
EOF
cat > "$XCRUNSHIM/xcrun" <<EOF
#!/bin/sh
for a in "\$@"; do
  if [ "\$a" = "--show-sdk-path" ]; then echo "$FAKE"; exit 0; fi
done
exec /usr/bin/xcrun "\$@"
EOF
chmod +x "$ZIGSHIM/zig" "$XCRUNSHIM/xcrun"

# --- 3. Build ----------------------------------------------------------------
# Project-local Zig cache so global-cache SDK-detection poisoning can't bite.
export ZIG_GLOBAL_CACHE_DIR="$VENDOR/.zig-global-cache"
export PATH="$XCRUNSHIM:$ZIGSHIM:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"

echo "==> Sanity: xcrun -> $(xcrun --sdk macosx --show-sdk-path)"
echo "==> Sanity: zig    -> $(zig version)"

cd "$ROOT"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE="${1:-Release}"
cmake --build build

echo
echo "==> DONE. Binary: $ROOT/build/nova-terminal"
file "$ROOT/build/nova-terminal"
