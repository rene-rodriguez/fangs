#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/macos-release-lib.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_eq() {
  local expected="$1"
  local actual="$2"
  local label="$3"
  if [ "$actual" != "$expected" ]; then
    fail "$label: expected '$expected', got '$actual'"
  fi
}

assert_eq \
  "$ROOT/dist/fangs-1.2.3-macos-arm64-unsigned.zip" \
  "$(fangs_macos_app_zip_path "$ROOT" "1.2.3" "arm64" "" "true")" \
  "unsigned CI bundle should use explicit unsigned suffix"

assert_eq \
  "$ROOT/dist/fangs-1.2.3-macos-arm64.zip" \
  "$(fangs_macos_app_zip_path "$ROOT" "1.2.3" "arm64" "" "false")" \
  "local ad-hoc bundle should keep the default filename unless requested"

assert_eq \
  "$ROOT/dist/fangs-1.2.3-macos-arm64.zip" \
  "$(fangs_macos_app_zip_path "$ROOT" "1.2.3" "arm64" "Developer ID Application: Example" "true")" \
  "Developer ID bundle should never be marked unsigned"

echo "macos_release_lib_tests: ok"
