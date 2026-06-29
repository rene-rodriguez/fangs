#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SETTINGS="$ROOT/src/ui_settings.c"
CMAKE="$ROOT/CMakeLists.txt"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

grep -Fq 'static bool theme_mode_dropdown_open = false;' "$SETTINGS" \
  || fail "settings modal must keep explicit theme mode dropdown state"

grep -Fq 'static bool theme_name_dropdown_open = false;' "$SETTINGS" \
  || fail "settings modal must keep explicit theme name dropdown state"

grep -Fq 'GuiDropdownBox(theme_mode_bounds, "Dark;Light", &active_theme_mode, theme_mode_dropdown_open)' "$SETTINGS" \
  || fail "settings modal must expose dark/light mode as a dropdown"

grep -Fq 'GuiDropdownBox(theme_bounds, theme_combo_list(theme_mode_light), &active_theme, theme_name_dropdown_open)' "$SETTINGS" \
  || fail "theme selector must be filtered by the selected dark/light mode"

if grep -Fq 'GuiComboBox(theme_bounds, theme_combo_list(), &active_theme)' "$SETTINGS"; then
  fail "theme selector must not use cyclic GuiComboBox"
fi

grep -Fq 'GuiLock();' "$SETTINGS" \
  || fail "settings modal must lock controls under the open theme dropdown"

grep -Fq 'GuiUnlock();' "$SETTINGS" \
  || fail "settings modal must unlock controls after drawing the open theme dropdown"

grep -Fq 'theme_mode_dropdown_open = false;' "$SETTINGS" \
  || fail "settings modal must reset mode dropdown state when the modal closes"

grep -Fq 'theme_name_dropdown_open = false;' "$SETTINGS" \
  || fail "settings modal must reset theme dropdown state when the modal closes"

grep -Fq 'NAME settings_theme_dropdown_tests' "$CMAKE" \
  || fail "settings theme dropdown regression test must be registered with ctest"

echo "settings_theme_dropdown_tests: ok"
