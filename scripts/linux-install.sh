#!/usr/bin/env bash
# =============================================================================
# Fangs — Linux local install (build + desktop integration)
# =============================================================================
# Builds Fangs (if not already built), packages it via scripts/package-linux.sh,
# and installs it under a prefix (default ~/.local) with a desktop entry + icon
# — so it shows up correctly in your app launcher and taskbar/alt-tab, not just
# on PATH. This is the source-build equivalent of what scripts/macos-bundle.sh
# does for macOS (produce a real, launcher-integrated local install) and of
# what the AUR package (packaging/aur/) does system-wide for Arch.
#
# Usage:  scripts/linux-install.sh [prefix]      (default: $FANGS_PREFIX or ~/.local)
# Requires: patchelf (same as scripts/package-linux.sh).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${1:-${FANGS_PREFIX:-$HOME/.local}}"

# --- 0. Ensure the binary exists ---------------------------------------------
if [ ! -x "$ROOT/build/fangs" ]; then
  echo "==> build/fangs not found; building via scripts/linux-build.sh"
  "$ROOT/scripts/linux-build.sh" Release
fi

# --- 1. Package a relocatable bundle (bin + lib + desktop entry + icon) -----
"$ROOT/scripts/package-linux.sh"
ARCH="$(uname -m)"
STAGE="$ROOT/dist/fangs-linux-$ARCH"

# --- 2. Install into $PREFIX --------------------------------------------------
echo "==> Installing -> $PREFIX"
mkdir -p "$PREFIX/bin" "$PREFIX/lib" \
         "$PREFIX/share/applications" \
         "$PREFIX/share/icons/hicolor/1024x1024/apps" \
         "$PREFIX/share/pixmaps"

cp "$STAGE/bin/fangs" "$PREFIX/bin/fangs"
chmod +x "$PREFIX/bin/fangs"
cp -P "$STAGE/lib/"libghostty-vt.* "$PREFIX/lib/"

# Exec= needs an absolute path: desktop environments often launch apps with a
# minimal PATH that doesn't include $PREFIX/bin (the same reason pty.c starts
# a login shell for the terminal's own child process).
sed "s|^Exec=fangs|Exec=$PREFIX/bin/fangs|" \
    "$STAGE/share/applications/fangs.desktop" \
    > "$PREFIX/share/applications/fangs.desktop"
cp "$STAGE/share/icons/hicolor/1024x1024/apps/fangs.png" \
   "$PREFIX/share/icons/hicolor/1024x1024/apps/fangs.png"
cp "$STAGE/share/pixmaps/fangs.png" "$PREFIX/share/pixmaps/fangs.png"

command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo
echo "==> DONE. Installed: $PREFIX/bin/fangs"
echo "    Launch it from your app launcher (\"Fangs\") or run: fangs"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "    Note: add $PREFIX/bin to your PATH: export PATH=\"$PREFIX/bin:\$PATH\"" ;;
esac
