# Live Theme Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preview a newly selected settings theme immediately without persisting it until Save, and restore the opening theme on Cancel or Escape.

**Architecture:** `ui_settings` keeps the existing complete `AppConfig` draft and reports a theme slug only when the rendered theme must change. `main.c` owns applying that slug to all terminal engines and UI chrome, so preview, save, and rollback use the same application path without mutating the persisted configuration before Save.

**Tech Stack:** C11, raylib/raygui, CMake/CTest, Bash static regression test.

---

### Task 1: Cover the settings preview contract

**Files:**
- Modify: `tests/settings_theme_dropdown_tests.sh`
- Modify: `CMakeLists.txt` only if a new test target is required (not expected)

- [ ] **Step 1: Write a failing regression assertion**

Require `src/ui_settings.c` to retain the opening theme and require `src/ui_settings.h` to expose a nullable preview-theme output:

```bash
grep -Fq 'static char opening_theme[32];' "$SETTINGS" \
  || fail "settings must retain the theme active when the modal opened"
grep -Fq 'const char **out_preview_theme' "$HEADER" \
  || fail "settings must report preview and rollback theme changes"
```

- [ ] **Step 2: Run the regression test and verify failure**

Run: `bash tests/settings_theme_dropdown_tests.sh`
Expected: failure because settings does not retain the opening theme or report a preview change.

- [ ] **Step 3: Implement the minimal settings-state contract**

Capture `cfg->theme` when initializing the draft. On a theme dropdown change, report `draft.theme`; on Cancel or Escape, report `opening_theme`; on Save, report `draft.theme`. Clear the retained opening theme with the rest of the modal state.

- [ ] **Step 4: Run the regression test and verify success**

Run: `bash tests/settings_theme_dropdown_tests.sh`
Expected: `settings_theme_dropdown_tests: ok`.

### Task 2: Apply preview changes without persisting them

**Files:**
- Modify: `src/ui_settings.h`
- Modify: `src/ui_settings.c`
- Modify: `src/main.c`

- [ ] **Step 1: Extract theme application in `main.c`**

Create a static helper accepting `App *`, theme slug, and `applied_theme` buffer. Move the existing all-tabs/all-panes `term_engine_apply_theme`, `apply_gui_style`, and `ui_theme_derive` sequence into it.

- [ ] **Step 2: Use the helper for persisted configuration changes**

Keep the existing once-per-change guard, but delegate its body to the helper so startup and saved-config behavior stay unchanged.

- [ ] **Step 3: Consume settings preview output**

Pass a `const char *preview_theme` output to `ui_settings_draw`. When it is non-null, invoke the helper after the modal draw. Do not change `cfg.theme` unless the modal reports Save.

- [ ] **Step 4: Build and run focused tests**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R 'settings_theme_dropdown_tests|theme_tests|ui_theme_tests'`
Expected: build succeeds and all selected tests pass.

### Task 3: Verify the complete regression surface

**Files:**
- Verify: `src/ui_settings.c`
- Verify: `src/main.c`

- [ ] **Step 1: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: all registered tests pass.

- [ ] **Step 2: Review the diff**

Run: `git diff --check && git diff -- src/ui_settings.c src/ui_settings.h src/main.c tests/settings_theme_dropdown_tests.sh`
Expected: no whitespace errors; changes are limited to theme preview, rollback, and its regression coverage.
