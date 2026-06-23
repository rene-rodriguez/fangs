#!/bin/bash
# =============================================================================
# Nova Terminal — macOS .app bundler
# =============================================================================
# Assembles `dist/Nova Terminal.app` from the built binary:
#   - copies build/nova-terminal into Contents/MacOS/
#   - bundles its one non-system dependency (libghostty-vt.dylib) into
#     Contents/Frameworks/ and rewrites the load commands so the .app is
#     relocatable (@executable_path/../Frameworks) with no build-tree rpath leak
#   - writes Info.plist, installs the icon if present
#   - ad-hoc code-signs (install_name_tool invalidates signatures, so this is
#     required for the app to launch on Apple Silicon)
#   - zips the result for distribution / Homebrew cask
#
# Everything else the binary links (libcurl, the system frameworks) ships with
# macOS, so nothing else needs bundling.
#
# Usage:  scripts/macos-bundle.sh [version]     (default version: 0.1.0)
# Requires: a built binary (run scripts/macos-build.sh first; this script will
#           invoke it automatically if build/nova-terminal is missing).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-0.1.0}"
BIN="$ROOT/build/nova-terminal"
APP="$ROOT/dist/Nova Terminal.app"
BUNDLE_ID="io.github.rene-rodriguez.nova-terminal"

# --- 0. Ensure the binary exists ---------------------------------------------
if [ ! -x "$BIN" ]; then
  echo "==> build/nova-terminal not found; building via scripts/macos-build.sh"
  "$ROOT/scripts/macos-build.sh" Release
fi

# --- 1. Lay out the bundle skeleton ------------------------------------------
echo "==> Assembling $APP"
rm -rf "$APP"
MACOS="$APP/Contents/MacOS"
FRAMEWORKS="$APP/Contents/Frameworks"
RES="$APP/Contents/Resources"
mkdir -p "$MACOS" "$FRAMEWORKS" "$RES"

cp "$BIN" "$MACOS/nova-terminal"
APPBIN="$MACOS/nova-terminal"

# --- 2. Resolve and bundle libghostty-vt -------------------------------------
# The dependency is recorded as @rpath/libghostty-vt.dylib; find the real file
# by walking the binary's own LC_RPATH entries (works from any build location).
DEP="$(otool -L "$APPBIN" | awk '/@rpath\/libghostty-vt/{print $1; exit}')"
[ -n "$DEP" ] || { echo "ERROR: no @rpath/libghostty-vt dependency in $APPBIN"; exit 1; }
DYNAME="$(basename "$DEP")"   # libghostty-vt.dylib

RPATHS="$(otool -l "$APPBIN" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')"
SRC_DYLIB=""
for rp in $RPATHS; do
  if [ -e "$rp/$DYNAME" ]; then SRC_DYLIB="$rp/$DYNAME"; break; fi
done
[ -n "$SRC_DYLIB" ] || { echo "ERROR: could not locate $DYNAME in rpaths:
$RPATHS"; exit 1; }

echo "==> Bundling $DYNAME from $SRC_DYLIB"
cp -L "$SRC_DYLIB" "$FRAMEWORKS/$DYNAME"   # -L: follow the version symlink chain
chmod u+w "$FRAMEWORKS/$DYNAME"

# --- 3. Make the bundle relocatable ------------------------------------------
install_name_tool -id "@rpath/$DYNAME" "$FRAMEWORKS/$DYNAME"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$APPBIN"
# Drop the absolute build-tree rpath(s) so the app doesn't point at a dev path.
for rp in $RPATHS; do
  install_name_tool -delete_rpath "$rp" "$APPBIN" 2>/dev/null || true
done

# --- 4. Info.plist -----------------------------------------------------------
ICON_KEY=""
if [ -f "$ROOT/assets/nova.icns" ]; then
  cp "$ROOT/assets/nova.icns" "$RES/nova.icns"
  ICON_KEY="
    <key>CFBundleIconFile</key>
    <string>nova</string>"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Nova Terminal</string>
    <key>CFBundleDisplayName</key>
    <string>Nova Terminal</string>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleExecutable</key>
    <string>nova-terminal</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>$ICON_KEY
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.developer-tools</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>BYOK local-first terminal. No telemetry, no logins.</string>
</dict>
</plist>
PLIST

# --- 4b. License files (sealed by the signing step below) --------------------
cp "$ROOT/LICENSE" "$RES/LICENSE" 2>/dev/null || true
cp "$ROOT/assets/OFL-JetBrainsMono.txt" "$RES/LICENSE-OFL-JetBrainsMono.txt" 2>/dev/null || true

# --- 5. Ad-hoc code-sign (inner-out) -----------------------------------------
echo "==> Code-signing (ad-hoc)"
codesign --force --sign - "$FRAMEWORKS/$DYNAME"
codesign --force --sign - "$APPBIN"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP" && echo "==> Signature verified"

# --- 6. Zip for distribution -------------------------------------------------
ZIP="$ROOT/dist/nova-terminal-$VERSION-macos-$(uname -m).zip"
rm -f "$ZIP"
( cd "$ROOT/dist" && /usr/bin/ditto -c -k --keepParent "Nova Terminal.app" "$ZIP" )

echo
echo "==> DONE"
echo "    App: $APP"
echo "    Zip: $ZIP"
echo "    sha256: $(shasum -a 256 "$ZIP" | awk '{print $1}')"
echo
echo "Verify deps are self-contained (only /usr/lib + /System + @rpath/@executable_path):"
otool -L "$APPBIN" | sed 's/^/    /'
