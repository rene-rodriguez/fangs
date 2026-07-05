# Linux GUI verification runbook

> **Purpose:** the one remaining v1 item — confirm Fangs's features **on-screen on a
> Linux desktop session** (the CachyOS dev box used so far is headless). Everything
> else (build, tests, command blocks, macOS bundle) is already verified.
>
> **For the next session:** run these top to bottom on the Linux machine with a
> graphical session active. Each step says what "pass" looks like.

## 0. Prereqs

- A **graphical session** (X11 or Wayland) — these open a real window.
- **Zig 0.15.2** on `PATH`. ⚠️ Arch/CachyOS `pacman` ships **0.16.0**, which will *not*
  build the pinned `libghostty-vt`. Grab 0.15.2 from
  [ziglang.org/download](https://ziglang.org/download/) and put it first on `PATH`.
  Check: `zig version` → `0.15.2`.
- Build deps: `cmake` (3.19+), `ninja`, a C compiler, `curl`/libcurl dev headers.

### Provisioning a fresh CachyOS machine (do this once)

`pacman`'s Zig is too new, so install 0.15.2 to a persistent spot (not `/tmp`, which is
tmpfs and vanishes on reboot) and symlink it onto `PATH`:

```sh
ver=0.15.2
curl -fsSL "https://ziglang.org/download/$ver/zig-x86_64-linux-$ver.tar.xz" -o /tmp/zig.tar.xz
mkdir -p ~/.local/lib && tar -xf /tmp/zig.tar.xz -C ~/.local/lib
ln -sf ~/.local/lib/zig-x86_64-linux-$ver/zig ~/.local/bin/zig   # ensure ~/.local/bin is on PATH
zig version   # -> 0.15.2
```

Build deps on CachyOS: `sudo pacman -S --needed cmake ninja gcc curl`. For the GUI-driven
parts of this runbook on a **Wayland/Hyprland** box you'll also want `grim` (screenshots) and
`ydotool` + a running `ydotoold` (input injection); `wl-clipboard` provides `wl-paste` for the
copy-button check. To exercise the AI features without a cloud key, install **Ollama** and pull a
chat model (see §3).

## 1. Clean build + tests

A clean build avoids any stale `build/` cache (the repo was renamed
`ai-terminal-ghosttly` → `fangs`, which can poison an old cache).

```sh
cd ~/…/fangs      # repo root
git pull                  # get the command-blocks + packaging commits
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

**Pass:** build is warning-clean and **9/9 ctest suites pass** (the 9th is the new
`cmdblocks_osc_tests`). The Linux-only `forkpty` header-shadow bug is already fixed
in `src/pty.c`; the engine + raylib should init clean.

## 2. Headless smoke (optional, works without a display via xvfb)

Fast evidence even before eyeballing. Both write a PNG you (I) can inspect:

```sh
# Command-block overlay (canned OSC-133 → separators + ✓/✗ badges + copy button):
FANGS_BLOCKS_SMOKE_SCREENSHOT=/tmp/fangs_blocks.png ./build/fangs
# Kitty image rendering (canned inline PNG via Kitty graphics protocol):
FANGS_KITTY_SMOKE_SCREENSHOT=/tmp/fangs_kitty.png ./build/fangs
# Split layout + sidebar:
FANGS_PHASE3_SMOKE_SCREENSHOT=/tmp/fangs_phase3.png ./build/fangs
```

If the box is headless but you want the smoke anyway: `xvfb-run -a env FANGS_BLOCKS_SMOKE_SCREENSHOT=/tmp/fangs_blocks.png ./build/fangs`.

## 3. On-screen feature confirmation (the actual goal)

Launch the real app in the graphical session:

```sh
export FANGS_API_KEY=…          # for the live AI step below (cloud providers)
./build/fangs
```

**No cloud key? Use a local Ollama model.** Install Ollama, pull a chat model, and point
Fangs at it — no key needed:

```sh
ollama pull qwen2.5:7b                       # any chat model; llama3.1 is the provider default
# In Ctrl+, settings: Provider → ollama (prefills http://localhost:11434/v1/chat/completions),
# then set Model to the tag you pulled (e.g. qwen2.5:7b) and Save.
# Or edit ~/.config/fangs/config directly:
#   [ai]
#   provider = ollama
#   endpoint = http://localhost:11434/v1/chat/completions
#   model    = qwen2.5:7b
#   api_key  = ollama        # any non-empty value; Ollama ignores it, Fangs just needs it non-blank
```

Ollama's `/v1/chat/completions` is OpenAI-compatible and streams the same `data:` SSE chunks
`ai_http.c`/`sse.c` already consume, so both the sidebar and inline-gen work unchanged.

Walk this checklist (✅ each):

- [ ] **Window opens**, shell prompt is interactive, typing/resize/scroll work.
- [ ] **Theming** — `Ctrl+,` opens settings; change Theme (e.g. Gruvbox) + font size,
      Save → terminal recolors live (incl. `ls --color`, `vim`), in-app UI restyles,
      grid reflows. ESC passes through to the shell (doesn't kill the app).
- [ ] **Font zoom** — `Ctrl +` / `Ctrl -` resize the font live and reflow the grid;
      `Ctrl 0` resets to the default. The size persists across restarts.
- [ ] **AI sidebar** — `Ctrl+Shift+B` (or `Cmd+B`) toggles it; terminal stays usable
      while it's visible; clicking it focuses the input.
- [ ] **Live AI request** — with `FANGS_API_KEY` set, ask the sidebar about something
      on screen → streamed, context-aware answer; a fenced command shows a **Run**
      button that stages (no auto-Enter).
- [ ] **Inline generation** — `Ctrl+Space`, type *"undo last git commit"* → command
      staged at the prompt, no trailing newline, you press Enter.

## 3a. Kitty Images

Fangs supports static Kitty graphics protocol images. In the window, run:

```sh
python3 - <<'PY'
import base64, sys
png = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVR4nGP8z8Dwn4GBgYGJAQoAHxcC"
    "AsuzUSwAAAAASUVORK5CYII="
)
sys.stdout.write("\033_Gf=100,a=T,s=2,v=2;" + base64.b64encode(png).decode() + "\033\\\n")
PY
```

Check (✅):

- [ ] A small image appears in the terminal grid.
- [ ] Text before/after the image remains readable.
- [ ] In a split pane, the image draws inside the pane that emitted it, not at the window origin.

## 4. Command blocks (needs the OSC-133 shell snippet)

The blocks overlay only draws once the shell emits OSC-133 marks. To test **without
touching your real `~/.zshrc`**, use a throwaway `ZDOTDIR`:

```sh
ZD=$(mktemp -d)
cat > "$ZD/.zshrc" <<'EOF'
autoload -Uz add-zsh-hook
_fangs_precmd()  { print -Pn "\e]133;D;$?\e\\"; print -Pn "\e]133;A\e\\"; }
_fangs_preexec() { print -Pn "\e]133;C\e\\"; }
add-zsh-hook precmd  _fangs_precmd
add-zsh-hook preexec _fangs_preexec
PS1="$ %{$(print -Pn '\e]133;B\e\\')%}"
EOF
SHELL=$(command -v zsh) ZDOTDIR="$ZD" ./build/fangs
```

(Or just add the snippet from `docs/shell-integration.md` to your real shell config.)

Then in the window run a few commands and check (✅):

- [ ] `true` then `false` then `ls -la` → each gets a **separator** + a colored
      **left gutter**; `true`/`ls` show a green **✓**, `false` shows a red **✗**.
- [ ] **Hover** a finished block → a **copy** button (top-right); click it →
      that command's output is on the clipboard (`xclip -o -sel clip` / `wl-paste`).
- [ ] **Ctrl+↑ / Ctrl+↓** (or Cmd) jumps the viewport between command prompts. With a
      TUI running (e.g. `htop`), the arrows should still reach it (nav only consumes
      the key when there's a block to jump to).

## 5. Report

Update `docs/plan.md` "v1 polish — remaining" → done with a dated note, and capture a
screenshot or two for the record. That closes the last v1 item.

> **Completed 2026-06-22** on a CachyOS laptop (Hyprland/Wayland, Ryzen 7 5800H). 9/9 ctest
> + both smokes + every on-screen feature confirmed; the two AI round-trips were driven by a
> local Ollama `qwen2.5:7b` (sidebar streamed a context-aware answer; `Ctrl+Space` staged
> `ls -lA`). See the "Linux on-screen verification" note in `docs/plan.md`.

## Gotchas seen before

- Wrong Zig (0.16.0) → `libghostty-vt` build fails. Use 0.15.2.
- mem0/Serena may surface stale memories under this repo's earlier development
  names — ignore them; this is the native C **Fangs** terminal repo.
- **HiDPI on Wayland fractional scaling.** Fangs multiplies `font_size` by the display's content
  scale. On macOS Retina raylib's `GetWindowScaleDPI()` returns the right value (2.0), but GLFW's
  **Wayland** backend reports ~**1.0** even at e.g. **1.5×**, which used to render the font tiny.
  `fangs_content_scale()` now **auto-detects** the scale from the monitor's physical DPI (`DPI/96`,
  snapped to a 0.25 step; only departs from 1.0 above ~125 DPI), so `font_size` stays a *logical*
  size and a **synced dotfile works unchanged across machines** — no per-machine tuning needed.
  If the heuristic is wrong for a given display, override it:
  - **`FANGS_SCALE`** wins over auto-detect — `FANGS_SCALE=1.5 fangs` (env). Check a
    machine's actual scale with `hyprctl monitors -j` (the `scale` field).
  - **Runtime zoom** — `Ctrl +` / `Ctrl -` adjust the font live, `Ctrl 0` resets to the default;
    each change persists to the config. (`Cmd` also works on macOS.)
  - Keep `font_size` itself logical (default 16); don't pre-multiply it, or it'll stack with the
    auto-detected scale.
