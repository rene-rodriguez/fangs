# Shell Integration (OSC 133) — enabling command blocks

Nova's VT engine (libghostty-vt) tracks **OSC 133 semantic prompt marks**: it can
tell prompt text, your typed command, and command output apart, and exposes
command-output regions via `ghostty_terminal_select_output()` (given a grid ref
within output, it returns that command's full output bounds). But it only knows
these boundaries if your **shell emits the marks**. This file is the shell-side half.

Adding this gives two things today, and unlocks a third:
1. **Cleaner AI context** — the engine knows where commands/output begin, so
   future context capture can scope to "the last command's output".
2. **A foundation for command blocks** (Warp-style) — see "Status" below.

> The marks are invisible escape sequences; they don't change how your prompt looks.

## zsh — add to `~/.zshrc`

```zsh
# Nova OSC 133 shell integration
autoload -Uz add-zsh-hook
_nova_precmd()  { print -Pn "\e]133;D;$?\e\\"; print -Pn "\e]133;A\e\\"; }  # prev cmd done + prompt start
_nova_preexec() { print -Pn "\e]133;C\e\\"; }                               # command about to run
add-zsh-hook precmd  _nova_precmd
add-zsh-hook preexec _nova_preexec
# Mark the end of the prompt / start of the typed command (the "B" mark):
PS1="$PS1%{$(print -Pn '\e]133;B\e\\')%}"
```

## bash — add to `~/.bashrc`

```bash
# Nova OSC 133 shell integration
_nova_prompt() {
  local ec=$?
  printf '\e]133;D;%s\e\\' "$ec"   # previous command done (+ exit code)
  printf '\e]133;A\e\\'            # prompt start
}
PROMPT_COMMAND="_nova_prompt${PROMPT_COMMAND:+; $PROMPT_COMMAND}"
PS1="${PS1}\[\e]133;B\e\\\\\]"     # prompt end / command start (B mark)
# "C" (command running) via the DEBUG trap, skipping the prompt hook itself:
_nova_preexec() { [[ "$BASH_COMMAND" == _nova_prompt ]] || printf '\e]133;C\e\\'; }
trap '_nova_preexec' DEBUG
```

After editing, `source ~/.zshrc` (or `~/.bashrc`) or open a new Nova window.

## Verifying it works

These sequences are silent, so there's no visible change to your prompt. Once the
snippet is active, run a couple of commands: each gets a **command block** — a
separator line, a colored left gutter, and a ✓/✗ exit-status badge. That's the
quickest confirmation the engine is seeing the marks.

## The four marks

| OSC | Meaning | Emitted |
|---|---|---|
| `133;A` | Prompt start | `precmd` (zsh) / `PROMPT_COMMAND` (bash) |
| `133;B` | Prompt end / command input start | end of `PS1`/`PROMPT` |
| `133;C` | Command execution start | `preexec` (zsh) / `DEBUG` trap (bash) |
| `133;D;<exit>` | Command finished (+ exit code) | next `precmd` / `PROMPT_COMMAND` |

## Command blocks — what you get

- **Shell side (this doc): ready.** Add the snippet and the engine starts tracking
  semantic boundaries.
- **Terminal side: built.** Each command (once your shell emits the marks) renders
  a Warp-style block:
  - a **separator** line above each prompt, plus a colored **left gutter** strip
    spanning the command + its output;
  - a **✓ / ✗ status badge** (green for exit 0, red otherwise) on the prompt row;
  - a **copy** button on hover (top-right of the block) that copies just that
    command's output to the clipboard;
  - **Cmd+↑ / Cmd+↓** (or **Ctrl+↑ / Ctrl+↓**) to jump the viewport between command
    prompts.

  Implementation note: the overlay is driven by the OSC-133 marks (a tracked grid
  ref anchored at each prompt + the exit code from the `D` mark); "copy output"
  uses the engine's `ghostty_terminal_select_output()`. The bytes are never
  modified — the marks are observed as they pass through to the VT engine. Without
  the snippet there are no marks and the overlay simply doesn't draw.

See `plan.md` for the roadmap.
