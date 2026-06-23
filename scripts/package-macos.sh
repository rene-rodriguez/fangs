#!/usr/bin/env bash
# =============================================================================
# Nova Terminal — macOS CLI release packager
# =============================================================================
# Assembles a relocatable CLI tarball from a Release build (for `install.sh`):
#   nova-terminal-macos-<arch>/
#     bin/nova-terminal          (LC_RPATH = @executable_path/../lib)
#     lib/libghostty-vt.dylib
#     LICENSE                    (MIT — Nova's own license)
#     LICENSE-OFL-JetBrainsMono.txt
#     README.md
#
# Bundles the one non-system dependency (libghostty-vt); everything else ships
# with macOS. Re-signs ad-hoc afterwards (install_name_tool invalidates the
# signature, which Apple Silicon requires to launch). For the .app bundle +
# Homebrew cask, use scripts/macos-bundle.sh instead.
#
# Usage:  scripts/package-macos.sh [version]      (default: 0.0.0)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-0.0.0}"
ARCH="$(uname -m)"   # arm64 | x86_64
BIN="$ROOT/build/nova-terminal"
NAME="nova-terminal-macos-$ARCH"
STAGE="$ROOT/dist/$NAME"

[ -x "$BIN" ] || { echo "ERROR: $BIN not found; build first."; exit 1; }

echo "==> Staging $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib"
cp "$BIN" "$STAGE/bin/nova-terminal"
APPBIN="$STAGE/bin/nova-terminal"

# Find the @rpath/libghostty-vt.dylib dependency, then locate the real file by
# walking the binary's own LC_RPATH entries (works from any build location).
DEP="$(otool -L "$APPBIN" | awk '/@rpath\/libghostty-vt/{print $1; exit}')"
[ -n "$DEP" ] || { echo "ERROR: no @rpath/libghostty-vt dependency in $APPBIN"; exit 1; }
DYNAME="$(basename "$DEP")"

RPATHS="$(otool -l "$APPBIN" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')"
SRC_DYLIB=""
for rp in $RPATHS; do
    [ -e "$rp/$DYNAME" ] && { SRC_DYLIB="$rp/$DYNAME"; break; }
done
[ -n "$SRC_DYLIB" ] || { echo "ERROR: could not locate $DYNAME in: $RPATHS"; exit 1; }

cp "$SRC_DYLIB" "$STAGE/lib/$DYNAME"

# Resolve the bundled dylib relative to the binary: drop the build-tree rpaths
# and add one pointing at ../lib.
for rp in $RPATHS; do
    install_name_tool -delete_rpath "$rp" "$APPBIN" 2>/dev/null || true
done
install_name_tool -add_rpath "@executable_path/../lib" "$APPBIN"

# install_name_tool invalidates code signatures; re-sign ad-hoc.
codesign --force --sign - "$STAGE/lib/$DYNAME"
codesign --force --sign - "$APPBIN"

cp "$ROOT/LICENSE" "$STAGE/LICENSE" 2>/dev/null || true
cp "$ROOT/assets/OFL-JetBrainsMono.txt" "$STAGE/LICENSE-OFL-JetBrainsMono.txt" 2>/dev/null || true
cp "$ROOT/README.md" "$STAGE/" 2>/dev/null || true

TARBALL="$ROOT/dist/$NAME.tar.gz"
tar -C "$ROOT/dist" -czf "$TARBALL" "$NAME"
echo "==> Built $TARBALL (version $VERSION)"
echo "$TARBALL"
