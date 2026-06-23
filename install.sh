#!/bin/sh
# =============================================================================
# Nova Terminal — one-line installer
#
#   curl -fsSL https://raw.githubusercontent.com/rene-rodriguez/nova-terminal/main/install.sh | sh
#
# Detects OS + arch, downloads the matching prebuilt tarball from the latest
# GitHub release, and installs it under a prefix (default: ~/.local).
#
# Private repo? Provide a token with `repo` scope and the installer authenticates
# both the API lookup and the asset download:
#   curl -fsSL .../install.sh | NOVA_GITHUB_TOKEN=ghp_xxx sh
#
# Env:
#   NOVA_PREFIX        install prefix             (default: $HOME/.local)
#   NOVA_VERSION       release tag to install     (default: latest)
#   NOVA_GITHUB_TOKEN  token for a private repo    (falls back to GH_TOKEN / GITHUB_TOKEN)
# =============================================================================
set -eu

REPO="rene-rodriguez/nova-terminal"
PREFIX="${NOVA_PREFIX:-$HOME/.local}"
TOKEN="${NOVA_GITHUB_TOKEN:-${GH_TOKEN:-${GITHUB_TOKEN:-}}}"
API="https://api.github.com/repos/${REPO}"

err() { printf '%s\n' "$*" >&2; }

os="$(uname -s)"
arch="$(uname -m)"

case "$os" in
    Linux)  plat="linux" ;;
    Darwin) plat="macos" ;;
    *) err "Unsupported OS: $os (Linux and macOS only)."; exit 1 ;;
esac

case "$arch" in
    x86_64|amd64)  a="x86_64" ;;
    arm64|aarch64) a="arm64" ;;
    *) err "Unsupported architecture: $arch."; exit 1 ;;
esac

asset="nova-terminal-${plat}-${a}.tar.gz"

# curl against the GitHub API, adding the auth header only when a token is set.
api_get() {
    # usage: api_get <url>   (Accept: github+json)
    if [ -n "$TOKEN" ]; then
        curl -fsSL -H "Accept: application/vnd.github+json" \
                   -H "Authorization: Bearer $TOKEN" "$1"
    else
        curl -fsSL -H "Accept: application/vnd.github+json" "$1"
    fi
}

# Resolve the release JSON (latest unless pinned via NOVA_VERSION).
if [ "${NOVA_VERSION:-latest}" = "latest" ]; then
    rel="$(api_get "${API}/releases/latest" 2>/dev/null || true)"
else
    rel="$(api_get "${API}/releases/tags/${NOVA_VERSION}" 2>/dev/null || true)"
fi

if [ -z "$rel" ]; then
    err "Could not fetch release metadata from ${REPO}."
    if [ -z "$TOKEN" ]; then
        err "If the repository is private, pass a token with 'repo' scope:"
        err "  curl -fsSL .../install.sh | NOVA_GITHUB_TOKEN=ghp_xxx sh"
    fi
    exit 1
fi

tag="$(printf '%s' "$rel" | grep -oE '"tag_name"[ ]*:[ ]*"[^"]+"' | head -1 | sed 's/.*"tag_name"[ ]*:[ ]*"//;s/"$//')"
[ -n "$tag" ] || { err "Could not parse the release tag."; exit 1; }

printf 'Installing nova-terminal %s (%s-%s) -> %s\n' "$tag" "$plat" "$a" "$PREFIX"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

dl_ok=0
if [ -n "$TOKEN" ]; then
    # Private assets must be fetched via the asset API URL with an octet-stream
    # Accept header (browser_download_url only works for public repos). Pull the
    # API URL of the asset object whose "name" matches ours.
    asset_url="$(printf '%s' "$rel" | tr -d '\n' | tr '{' '\n' \
        | grep "\"name\"[ ]*:[ ]*\"${asset}\"" \
        | grep -oE '"url"[ ]*:[ ]*"https://api.github.com[^"]*/assets/[0-9]+"' \
        | head -1 | sed 's/.*"url"[ ]*:[ ]*"//;s/"$//')"
    if [ -n "$asset_url" ]; then
        curl -fSL -H "Accept: application/octet-stream" \
                  -H "Authorization: Bearer $TOKEN" \
                  "$asset_url" -o "$tmp/$asset" && dl_ok=1 || dl_ok=0
    fi
else
    url="https://github.com/${REPO}/releases/download/${tag}/${asset}"
    curl -fSL "$url" -o "$tmp/$asset" && dl_ok=1 || dl_ok=0
fi

if [ "$dl_ok" -ne 1 ]; then
    err ""
    err "No prebuilt binary for ${plat}-${a} in release ${tag}."
    err "Build from source instead: https://github.com/${REPO}#build-from-source"
    exit 1
fi

tar -C "$tmp" -xzf "$tmp/$asset"
src="$tmp/nova-terminal-${plat}-${a}"

mkdir -p "$PREFIX/bin" "$PREFIX/lib"
cp "$src/bin/nova-terminal" "$PREFIX/bin/nova-terminal"
chmod +x "$PREFIX/bin/nova-terminal"
# Bundled libghostty-vt (resolved via the binary's relative RPATH -> ../lib).
cp -P "$src/lib/"libghostty-vt.* "$PREFIX/lib/" 2>/dev/null || true

printf 'Installed: %s/bin/nova-terminal\n' "$PREFIX"

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) printf 'Note: add %s/bin to your PATH:\n  export PATH="%s/bin:$PATH"\n' "$PREFIX" "$PREFIX" ;;
esac
