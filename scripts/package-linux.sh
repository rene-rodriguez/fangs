#!/usr/bin/env bash
# =============================================================================
# Fangs — Linux release packager
# =============================================================================
# Assembles a relocatable tarball from a Release build:
#   fangs-linux-<arch>/
#     bin/fangs              (RPATH = $ORIGIN/../lib)
#     lib/libghostty-vt.so.0.1.0     (+ .so.0, .so symlinks)
#     share/applications/fangs.desktop   (Exec=fangs — installer rewrites it)
#     share/icons/hicolor/1024x1024/apps/fangs.png
#     share/pixmaps/fangs.png             (untheme-aware fallback)
#     LICENSE                        (MIT — Fangs's own license)
#     LICENSE-OFL-JetBrainsMono.txt
#     README.md
#
# The one non-system dependency (libghostty-vt) is bundled; everything else
# (libcurl, libGL, X11/Wayland) ships with the distro. Run a Release build
# first (cmake -B build ...; cmake --build build).
#
# Usage:  scripts/package-linux.sh [version]      (default: 0.0.0)
# Requires: patchelf.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-0.0.0}"
ARCH="$(uname -m)"
BIN="$ROOT/build/fangs"
LIBDIR="$ROOT/build/_deps/ghostty-src/zig-out/lib"
NAME="fangs-linux-$ARCH"
STAGE="$ROOT/dist/$NAME"

[ -x "$BIN" ] || { echo "ERROR: $BIN not found; build first."; exit 1; }
[ -f "$LIBDIR/libghostty-vt.so.0.1.0" ] || { echo "ERROR: libghostty-vt not found in $LIBDIR"; exit 1; }
command -v patchelf >/dev/null || { echo "ERROR: patchelf required."; exit 1; }

echo "==> Staging $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib"

cp "$BIN" "$STAGE/bin/fangs"
cp "$LIBDIR/libghostty-vt.so.0.1.0" "$STAGE/lib/"
ln -sf libghostty-vt.so.0.1.0 "$STAGE/lib/libghostty-vt.so.0"
ln -sf libghostty-vt.so.0.1.0 "$STAGE/lib/libghostty-vt.so"

# Resolve the bundled lib relative to the binary, so the tarball runs from
# anywhere (and `install.sh` can drop bin/ + lib/ under any prefix).
patchelf --set-rpath '$ORIGIN/../lib' "$STAGE/bin/fangs"

# Desktop entry + icon, so an install under any prefix gets launcher/taskbar
# integration (installers rewrite Exec= to the installed binary's absolute
# path, since the desktop environment's launch PATH may not include it).
mkdir -p "$STAGE/share/applications" \
         "$STAGE/share/icons/hicolor/1024x1024/apps" \
         "$STAGE/share/pixmaps"
cp "$ROOT/packaging/aur/fangs.desktop" "$STAGE/share/applications/fangs.desktop"
cp "$ROOT/assets/fangs-icon.png" "$STAGE/share/icons/hicolor/1024x1024/apps/fangs.png"
cp "$ROOT/assets/fangs-icon.png" "$STAGE/share/pixmaps/fangs.png"

cp "$ROOT/LICENSE" "$STAGE/LICENSE" 2>/dev/null || true
cp "$ROOT/assets/OFL-JetBrainsMono.txt" "$STAGE/LICENSE-OFL-JetBrainsMono.txt" 2>/dev/null || true
cp "$ROOT/README.md" "$STAGE/" 2>/dev/null || true

TARBALL="$ROOT/dist/$NAME.tar.gz"
tar -C "$ROOT/dist" -czf "$TARBALL" "$NAME"
echo "==> Built $TARBALL (version $VERSION)"
echo "$TARBALL"
