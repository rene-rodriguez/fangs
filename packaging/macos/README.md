# macOS packaging — `Fangs.app` + Homebrew cask

Two artifacts live here:

- **`scripts/macos-bundle.sh`** (in the repo root `scripts/`) — turns the built
  binary into a relocatable `Fangs.app` plus a distributable `.zip`. Local
  builds are ad-hoc signed by default; release builds use Developer ID signing,
  notarization, and stapling when the signing environment is present.
- **`fangs.rb`** — a Homebrew **cask** that installs the prebuilt `.app`
  from a GitHub release.

## Build the .app locally

```sh
scripts/macos-build.sh           # build/fangs (handles the Zig/SDK workaround)
scripts/macos-bundle.sh 0.1.0    # dist/Fangs.app + dist/fangs-0.1.0-macos-arm64.zip
open "dist/Fangs.app"
```

The bundler:

- copies the binary into `Contents/MacOS/`;
- bundles its **one** non-system dependency, `libghostty-vt.dylib`, into
  `Contents/Frameworks/` and rewrites the load commands to
  `@executable_path/../Frameworks` (no build-tree path leaks). Everything else
  the binary links — `libcurl`, the system frameworks — ships with macOS;
- writes `Info.plist`, installs `assets/fangs.icns`;
- code-signs inner-out (rewriting load commands invalidates any existing
  signature). Set `FANGS_CODESIGN_IDENTITY` to use Developer ID signing with
  hardened runtime entitlements; otherwise the script uses ad-hoc signing for
  local builds;
- with Developer ID signing enabled, submits a temporary app zip to
  `notarytool --wait`, staples the accepted ticket, and validates with
  `stapler` + `spctl`;
- zips with `ditto` and prints the `sha256` for the cask.

The bundle is verified self-contained with `otool -L` at the end of the run, and
launching the bundled binary resolves the dylib from inside the `.app`.

## Install via Homebrew (once a release exists)

```sh
brew install --cask ./packaging/macos/fangs.rb   # local tap, for testing
```

Or, after the formula is published to a tap:

```sh
brew install --cask <tap>/fangs
```

## Cutting a release (maintainer)

1. Ensure the CI/release host has a paid Apple Developer ID certificate and
   notarization credentials.
2. `scripts/macos-bundle.sh <version>`
   with either `FANGS_NOTARY_KEYCHAIN_PROFILE` or
   `APPLE_ID` + `APPLE_TEAM_ID` + `APPLE_APP_SPECIFIC_PASSWORD`.
3. Upload `dist/fangs-<version>-macos-arm64.zip` to the GitHub release `v<version>`.
4. In `fangs.rb`, set `version` and paste the `sha256` the bundler printed.

The GitHub release workflow expects these secrets for notarized macOS assets:

- `MACOS_DEVELOPER_ID_CERTIFICATE_BASE64`
- `MACOS_DEVELOPER_ID_CERTIFICATE_PASSWORD`
- `MACOS_DEVELOPER_ID_APPLICATION_IDENTITY`
- `APPLE_ID`
- `APPLE_TEAM_ID`
- `APPLE_APP_SPECIFIC_PASSWORD`

Optional:

- `MACOS_KEYCHAIN_PASSWORD`

## Known limitations / TODO

- **Notarization requires paid Apple credentials.** Local builds remain ad-hoc
  unless `FANGS_CODESIGN_IDENTITY` and notary credentials are provided.
- **arm64 only.** The build/bundle here targets Apple Silicon (the dev machine).
  An x86-64 / universal build + a second cask arch branch is a follow-up.
- **Login shell.** Launched from Finder, the app spawns `$SHELL` as a
  non-login interactive shell, so `~/.zshrc` (not `~/.zprofile`) is sourced —
  fine for most setups; revisit if `PATH` looks short under the `.app`.
- **Command blocks** need the OSC-133 snippet from `docs/shell-integration.md`;
  AI features need `FANGS_API_KEY` (or a key in `~/.config/fangs/config`).
