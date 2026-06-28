#!/bin/bash
# =============================================================================
# Fangs — macOS .app bundler
# =============================================================================
# Assembles `dist/Fangs.app` from the built binary:
#   - copies build/fangs into Contents/MacOS/
#   - bundles its one non-system dependency (libghostty-vt.dylib) into
#     Contents/Frameworks/ and rewrites the load commands so the .app is
#     relocatable (@executable_path/../Frameworks) with no build-tree rpath leak
#   - writes Info.plist, installs the icon if present
#   - code-signs inner-out. By default this is ad-hoc for local dev; when
#     FANGS_CODESIGN_IDENTITY is set it uses Developer ID + hardened runtime
#     entitlements, notarizes with notarytool, and staples the ticket
#   - zips the result for distribution / Homebrew cask
#
# Everything else the binary links (libcurl, the system frameworks) ships with
# macOS, so nothing else needs bundling.
#
# Usage:  scripts/macos-bundle.sh [version]     (default version: 0.1.0)
# Requires: a built binary (run scripts/macos-build.sh first; this script will
#           invoke it automatically if build/fangs is missing).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/macos-release-lib.sh"

VERSION="${1:-0.1.0}"
BIN="$ROOT/build/fangs"
APP="$ROOT/dist/Fangs.app"
BUNDLE_ID="io.github.rene-rodriguez.fangs"
ENTITLEMENTS="$ROOT/packaging/macos/hardened-runtime.entitlements"
SIGN_IDENTITY="${FANGS_CODESIGN_IDENTITY:-}"
ARCH="$(uname -m)"

# --- 0. Ensure the binary exists ---------------------------------------------
if [ ! -x "$BIN" ]; then
  echo "==> build/fangs not found; building via scripts/macos-build.sh"
  "$ROOT/scripts/macos-build.sh" Release
fi

# --- 1. Lay out the bundle skeleton ------------------------------------------
echo "==> Assembling $APP"
rm -rf "$APP"
MACOS="$APP/Contents/MacOS"
FRAMEWORKS="$APP/Contents/Frameworks"
RES="$APP/Contents/Resources"
mkdir -p "$MACOS" "$FRAMEWORKS" "$RES"

cp "$BIN" "$MACOS/fangs"
APPBIN="$MACOS/fangs"

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
if [ -f "$ROOT/assets/fangs.icns" ]; then
  cp "$ROOT/assets/fangs.icns" "$RES/fangs.icns"
  ICON_KEY="
    <key>CFBundleIconFile</key>
    <string>fangs</string>"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Fangs</string>
    <key>CFBundleDisplayName</key>
    <string>Fangs</string>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>CFBundleExecutable</key>
    <string>fangs</string>
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

# --- 5. Code-sign (inner-out) ------------------------------------------------
if [ -n "$SIGN_IDENTITY" ]; then
  echo "==> Code-signing with Developer ID: $SIGN_IDENTITY"
  CODESIGN_ARGS=(--force --sign "$SIGN_IDENTITY" --timestamp --options runtime --entitlements "$ENTITLEMENTS")
else
  echo "==> Code-signing (ad-hoc; local builds only)"
  CODESIGN_ARGS=(--force --sign -)
fi

codesign "${CODESIGN_ARGS[@]}" "$FRAMEWORKS/$DYNAME"
codesign "${CODESIGN_ARGS[@]}" "$APPBIN"
codesign "${CODESIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP" && echo "==> Signature verified"

# --- 6. Notarize + staple when Developer ID signing is enabled ---------------
if [ -n "$SIGN_IDENTITY" ]; then
  NOTARY_AUTH=()
  if [ -n "${FANGS_NOTARY_KEYCHAIN_PROFILE:-}" ]; then
    NOTARY_AUTH=(--keychain-profile "$FANGS_NOTARY_KEYCHAIN_PROFILE")
  elif [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_SPECIFIC_PASSWORD:-}" ]; then
    NOTARY_AUTH=(--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_SPECIFIC_PASSWORD")
  else
    echo "ERROR: Developer ID signing is enabled but no notarization credentials were provided." >&2
    echo "Set FANGS_NOTARY_KEYCHAIN_PROFILE, or APPLE_ID + APPLE_TEAM_ID + APPLE_APP_SPECIFIC_PASSWORD." >&2
    exit 1
  fi

  NOTARY_ZIP="$ROOT/dist/fangs-$VERSION-macos-$ARCH-notary.zip"
  rm -f "$NOTARY_ZIP"
  ( cd "$ROOT/dist" && /usr/bin/ditto -c -k --keepParent "Fangs.app" "$NOTARY_ZIP" )

  echo "==> Submitting for notarization"
  xcrun notarytool submit "$NOTARY_ZIP" --wait "${NOTARY_AUTH[@]}"
  rm -f "$NOTARY_ZIP"

  echo "==> Stapling notarization ticket"
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  spctl -a -vvv -t exec "$APP"
fi

# --- 7. Zip for distribution -------------------------------------------------
ZIP="$(fangs_macos_app_zip_path "$ROOT" "$VERSION" "$ARCH" "$SIGN_IDENTITY" "${FANGS_UNSIGNED_ZIP_SUFFIX:-}")"
rm -f "$ZIP"
( cd "$ROOT/dist" && /usr/bin/ditto -c -k --keepParent "Fangs.app" "$ZIP" )

echo
echo "==> DONE"
echo "    App: $APP"
echo "    Zip: $ZIP"
echo "    sha256: $(shasum -a 256 "$ZIP" | awk '{print $1}')"
echo
echo "Verify deps are self-contained (only /usr/lib + /System + @rpath/@executable_path):"
otool -L "$APPBIN" | sed 's/^/    /'
