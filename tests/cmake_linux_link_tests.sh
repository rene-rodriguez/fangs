#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE="$ROOT/CMakeLists.txt"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

ui_theme_block="$(
  awk '
    /add_executable\(ui_theme_tests/ { in_block = 1 }
    in_block { print }
    in_block && /add_test\(NAME ui_theme_tests/ { exit }
  ' "$CMAKE"
)"

if ! grep -Eq 'target_link_libraries\(ui_theme_tests[[:space:]]+m\)' <<<"$ui_theme_block"; then
  fail "ui_theme_tests must link libm for sqrtf on Linux"
fi

echo "cmake_linux_link_tests: ok"
