#!/usr/bin/env bash

fangs_truthy() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON) return 0 ;;
    *) return 1 ;;
  esac
}

fangs_macos_app_zip_path() {
  local root="$1"
  local version="$2"
  local arch="$3"
  local sign_identity="${4:-}"
  local unsigned_zip_suffix="${5:-}"
  local suffix=""

  if [ -z "$sign_identity" ] && fangs_truthy "$unsigned_zip_suffix"; then
    suffix="-unsigned"
  fi

  printf '%s/dist/fangs-%s-macos-%s%s.zip\n' "$root" "$version" "$arch" "$suffix"
}
