#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="$ROOT/.github/workflows/release.yml"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

contains() {
  local needle="$1"
  grep -Fq "$needle" "$WORKFLOW" || fail "release workflow should contain: $needle"
}

not_contains() {
  local needle="$1"
  if grep -Fq "$needle" "$WORKFLOW"; then
    fail "release workflow should not contain: $needle"
  fi
}

contains "Detect macOS signing and notarization secrets"
contains "steps.macos_signing.outputs.available == 'true'"
contains "SIGNING_AVAILABLE: \${{ steps.macos_signing.outputs.available }}"
contains "FANGS_UNSIGNED_ZIP_SUFFIX:"
contains "unset FANGS_CODESIGN_IDENTITY APPLE_ID APPLE_TEAM_ID APPLE_APP_SPECIFIC_PASSWORD"
contains "dist/fangs-\${TAG#v}-macos-\$(uname -m)-unsigned.zip"
not_contains "Require macOS signing and notarization secrets"
not_contains "exit \"\$missing\""

echo "release_workflow_tests: ok"
