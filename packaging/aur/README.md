# AUR packaging — `fangs-git`

A VCS `PKGBUILD` that builds Fangs from the `main` branch for
Arch/CachyOS (x86-64). Verified end-to-end with `makepkg` on CachyOS
(2026-06-22): builds, all 8 ctest suites pass in `check()`, and the package
installs the binary, the bundled `libghostty-vt.so`, a `.desktop` entry, and the
font license — with no RPATH or build-path leak.

## Files
- `PKGBUILD` — the package recipe.
- `.SRCINFO` — generated metadata (`makepkg --printsrcinfo > .SRCINFO`); regenerate after editing `PKGBUILD`.
- `fangs.desktop` — application launcher entry.

## Build / install locally
```bash
cd packaging/aur
makepkg -si        # -s installs make/runtime deps, -i installs the package
```

## Notes & caveats
- **Zig 0.15.2 is fetched, not depended on.** The pinned ghostty commit builds
  only with Zig **0.15.2**; Arch currently ships 0.16.0 (wrong version). The
  `PKGBUILD` downloads the exact 0.15.2 toolchain as a checksummed source and
  puts it on `PATH` for the build only — it is never installed.
- **Network at build time.** The CMake build uses `FetchContent` to pull raylib
  5.5 and the pinned ghostty source during `build()`. `makepkg` allows network
  in `build()` (only source extraction is sandboxed), so this works, but it is
  not a hermetic/offline build.
- **Repo must be reachable.** The `source` clones
  `https://github.com/rene-rodriguez/fangs.git`. While that repo is
  private, an AUR consumer can't clone it anonymously — publish the repo (or
  point `source` at a public mirror) before uploading to the AUR.
- **x86-64 only** for now (the prebuilt Zig source and `arch=()` are x86-64);
  add an `aarch64` Zig source + `sha256sums_aarch64` to extend.
- **Project license is TBD** (`license=('custom')`); only the bundled JetBrains
  Mono OFL is installed to `/usr/share/licenses`. Fill in the real license here
  and in the repo once chosen.
