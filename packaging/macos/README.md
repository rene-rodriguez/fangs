# macOS packaging — `Fangs.app` + Homebrew cask

Two artifacts live here:

- **`scripts/macos-bundle.sh`** (in the repo root `scripts/`) — turns the built
  binary into a relocatable, ad-hoc-signed `Fangs.app` plus a
  distributable `.zip`.
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
- **ad-hoc code-signs** inner-out (rewriting load commands invalidates any
  signature, so this is required for the app to launch on Apple Silicon);
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

1. `scripts/macos-bundle.sh <version>`
2. Upload `dist/fangs-<version>-macos-arm64.zip` to the GitHub release `v<version>`.
3. In `fangs.rb`, set `version` and paste the `sha256` the bundler printed.

## Known limitations / TODO

- **Notarization.** The app is only **ad-hoc signed**. Downloaded copies are
  Gatekeeper-quarantined, so first launch needs right-click → Open (or
  `xattr -dr com.apple.quarantine "<app>"`). Proper distribution needs a paid
  Apple Developer ID + `notarytool` — out of scope for this pass; the cask
  `caveats` document the workaround.
- **arm64 only.** The build/bundle here targets Apple Silicon (the dev machine).
  An x86-64 / universal build + a second cask arch branch is a follow-up.
- **Login shell.** Launched from Finder, the app spawns `$SHELL` as a
  non-login interactive shell, so `~/.zshrc` (not `~/.zprofile`) is sourced —
  fine for most setups; revisit if `PATH` looks short under the `.app`.
- **Command blocks** need the OSC-133 snippet from `docs/shell-integration.md`;
  AI features need `FANGS_API_KEY` (or a key in `~/.config/fangs/config`).
