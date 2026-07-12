# Theme Catalog Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand Fangs' theme catalog with brand-aligned flagship dark/light themes, five new dark themes, five new light themes, remove off-brand themes, and update all references/tests.

**Architecture:** Theme definitions live in `src/theme.c` as static `Theme` structs registered in `ENTRIES[]`; UI mode filtering in `src/ui_settings.c` uses `theme_count()`/`theme_name()`/`theme_slug()` and `Theme.is_light`, so no settings UI changes are required. Tests in `tests/theme_tests.c` assert counts, polarity, and legacy alias resolution, so those assertions must update with the catalog.

**Tech Stack:** C99, ctest, bash.

---

## Task 0: Verify current build/tests pass

**Files:**
- Run in repo root `/Users/rene/Documents/repos/rene/fangs`

- [ ] **Step 0.1: Build the project**

Run: `cmake --build build`
Expected: no errors.

- [ ] **Step 0.2: Run existing tests**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests pass, including `theme_tests` and `ui_theme_tests`.

---

## Task 1: Add new theme definitions to `src/theme.c`

**Files:**
- Modify: `src/theme.c`

Add the 12 new `static const Theme` definitions immediately before the existing `ENTRIES` declaration (after `EVERFORESTLIGHT`). Use the exact ANSI slot ordering: black/red/green/yellow/blue/magenta/cyan/white, then the bright variants.

- [ ] **Step 1.1: Add Fangs Dark (`FANGSDARK`)**

Insert above `ENTRIES`:

```c
// Fangs Dark — brand flagship. Charcoal bg, muted teal/amber accents.
static const Theme FANGSDARK = {
    .bg = { 28,  30,  34}, .fg = {198, 202, 208}, .cursor = { 86, 182, 194},
    .ansi = {
        { 28,  30,  34}, {239,  83,  80}, { 92, 191, 154}, {242, 174,  89},
        { 86, 182, 194}, {184, 124, 204}, {106, 189, 172}, {198, 202, 208},
        { 80,  84,  92}, {255,  92,  87}, {115, 213, 174}, {255, 196, 106},
        {109, 205, 218}, {209, 153, 228}, {130, 211, 191}, {243, 245, 248},
    },
    .is_light = false,
};
```

- [ ] **Step 1.2: Add Fangs Light (`FANGSLIGHT`)**

Insert after `FANGSDARK`:

```c
// Fangs Light — brand flagship. Warm off-white bg, deep slate text.
static const Theme FANGSLIGHT = {
    .bg = {250, 248, 243}, .fg = { 55,  60,  67}, .cursor = { 31, 137, 149},
    .ansi = {
        { 55,  60,  67}, {204,  52,  43}, { 62, 153,  88}, {179, 122,  10},
        { 31, 137, 149}, {149,  74, 165}, { 58, 150, 116}, {150, 154, 160},
        {113, 120, 128}, {230,  67,  55}, { 80, 179,  98}, {211, 151,  34},
        { 54, 164, 178}, {176, 104, 193}, { 82, 174, 139}, { 38,  42,  47},
    },
    .is_light = true,
};
```

- [ ] **Step 1.3: Add five new dark themes**

Insert after `FANGSLIGHT`:

```c
// Dracula.
static const Theme DRACULA = {
    .bg = {40, 42, 54}, .fg = {248, 248, 242}, .cursor = {248, 248, 242},
    .ansi = {
        { 40,  42,  54}, {255,  85,  85}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, {248, 248, 242},
        { 68,  71,  90}, {255, 110, 103}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, {255, 255, 255},
    },
    .is_light = false,
};

// Nord.
static const Theme NORD = {
    .bg = {46, 52, 64}, .fg = {216, 222, 233}, .cursor = {216, 222, 233},
    .ansi = {
        { 59,  66,  82}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        {129, 161, 193}, {180, 142, 173}, {136, 192, 208}, {229, 233, 240},
        { 76,  86, 106}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        {129, 161, 193}, {180, 142, 173}, {143, 188, 187}, {236, 239, 244},
    },
    .is_light = false,
};

// Kanagawa.
static const Theme KANAGAWA = {
    .bg = { 31,  31,  40}, .fg = {220, 215, 186}, .cursor = {255, 160,  38},
    .ansi = {
        { 31,  31,  40}, {255,  91,  93}, {118, 148, 106}, {220, 165,  76},
        {145, 184, 208}, {154, 120, 158}, { 95, 160, 160}, {220, 215, 186},
        { 84,  84, 100}, {255,  91,  93}, {118, 148, 106}, {220, 165,  76},
        {145, 184, 208}, {154, 120, 158}, { 95, 160, 160}, {255, 255, 240},
    },
    .is_light = false,
};

// Everforest Dark.
static const Theme EVERFORESTDARK = {
    .bg = { 50,  57,  53}, .fg = {211, 198, 170}, .cursor = {167, 192, 128},
    .ansi = {
        { 50,  57,  53}, {230, 126, 128}, {167, 192, 128}, {219, 188, 127},
        {127, 187, 179}, {223, 105, 186}, {123, 189, 152}, {211, 198, 170},
        {123, 136, 128}, {230, 126, 128}, {167, 192, 128}, {219, 188, 127},
        {127, 187, 179}, {223, 105, 186}, {123, 189, 152}, {243, 239, 224},
    },
    .is_light = false,
};

// Material Oceanic.
static const Theme MATERIALOCEANIC = {
    .bg = { 38,  50,  56}, .fg = {176, 190, 197}, .cursor = {128, 203, 196},
    .ansi = {
        { 38,  50,  56}, {255,  83, 112}, {195, 232, 141}, {255, 202,  40},
        {130, 170, 255}, {199, 146, 234}, {128, 203, 196}, {176, 190, 197},
        { 84, 110, 122}, {255,  83, 112}, {195, 232, 141}, {255, 202,  40},
        {130, 170, 255}, {199, 146, 234}, {128, 203, 196}, {255, 255, 255},
    },
    .is_light = false,
};
```

- [ ] **Step 1.4: Add five new light themes**

Insert after the new dark themes:

```c
// Kanagawa Lotus (light variant).
static const Theme KANAGAWALOTUS = {
    .bg = {245, 240, 232}, .fg = { 84,  78,  78}, .cursor = {183, 110,  38},
    .ansi = {
        { 84,  78,  78}, {195,  64,  67}, { 92, 128,  79}, {169, 128,  58},
        {117, 154, 184}, {149, 119, 147}, { 84, 146, 146}, { 84,  78,  78},
        {140, 135, 130}, {195,  64,  67}, { 92, 128,  79}, {169, 128,  58},
        {117, 154, 184}, {149, 119, 147}, { 84, 146, 146}, { 46,  42,  42},
    },
    .is_light = true,
};

// One Light Pro (brighter, higher-contrast One Light).
static const Theme ONELIGHTPRO = {
    .bg = {255, 255, 255}, .fg = { 31,  35,  40}, .cursor = { 36, 114, 200},
    .ansi = {
        { 31,  35,  40}, {228,  86,  73}, { 58, 140,  71}, {193, 132,   1},
        { 36, 114, 200}, {166,  38, 164}, {  9, 151, 179}, {140, 145, 153},
        {140, 145, 153}, {228,  86,  73}, { 58, 140,  71}, {193, 132,   1},
        { 36, 114, 200}, {166,  38, 164}, {  9, 151, 179}, { 31,  35,  40},
    },
    .is_light = true,
};

// Dracula Soft (light background variant).
static const Theme DRACULASOFT = {
    .bg = {248, 248, 248}, .fg = { 40,  42,  54}, .cursor = {189, 147, 249},
    .ansi = {
        { 68,  71,  90}, {255,  85,  85}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, { 40,  42,  54},
        { 98, 102, 124}, {255, 110, 103}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, { 25,  26,  34},
    },
    .is_light = true,
};

// Nord Light (polar day variant).
static const Theme NORDLIGHT = {
    .bg = {236, 239, 244}, .fg = { 59,  66,  82}, .cursor = { 94, 129, 172},
    .ansi = {
        { 59,  66,  82}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        { 94, 129, 172}, {180, 142, 173}, {136, 192, 208}, {216, 222, 233},
        {129, 142, 164}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        { 94, 129, 172}, {180, 142, 173}, {143, 188, 187}, { 46,  52,  64},
    },
    .is_light = true,
};

// Everforest Light Hard (higher contrast variant).
static const Theme EVERFORESTLIGHTHARD = {
    .bg = {253, 246, 227}, .fg = { 60,  67,  63}, .cursor = { 92, 120,  67},
    .ansi = {
        { 60,  67,  63}, {230,  85,  87}, {132, 160,  86}, {214, 158,  44},
        { 76, 144, 160}, {210,  87, 174}, { 86, 165, 120}, {160, 159, 145},
        {135, 145, 138}, {230,  85,  87}, {132, 160,  86}, {214, 158,  44},
        { 76, 144, 160}, {210,  87, 174}, { 86, 165, 120}, { 40,  45,  42},
    },
    .is_light = true,
};
```

---

## Task 2: Rebuild the `ENTRIES` registry

**Files:**
- Modify: `src/theme.c` (replace `ENTRIES` body)

Replace the existing `ENTRIES` array with the final catalog, ordered dark then light, with the new house themes first in each section. Remove `AYUMIRAGE`, `ROSEPINE`, `LIGHTMODERN`, `TOKYONIGHTDAY`.

- [ ] **Step 2.1: Replace `ENTRIES`**

Replace the whole `static const ThemeEntry ENTRIES[] = { ... };` block with:

```c
static const ThemeEntry ENTRIES[] = {
    // Dark themes
    {"fangs-dark", "Fangs Dark", &FANGSDARK},
    {"onedark", "One Dark", &ONEDARK},
    {"darkmodern", "Dark Modern", &DARKMODERN},
    {"githubdark", "GitHub Dark", &GITHUBDARK},
    {"gruvbox", "Gruvbox", &GRUVBOX},
    {"monokai", "Monokai", &MONOKAI},
    {"solarizeddark", "Solarized Dark", &SOLARIZEDDARK},
    {"catppuccinmocha", "Catppuccin Mocha", &CATPPUCCINMOCHA},
    {"catppuccinfrappe", "Catppuccin Frappe", &CATPPUCCINFRAPPE},
    {"tokyonight", "Tokyo Night", &TOKYONIGHT},
    {"dracula", "Dracula", &DRACULA},
    {"nord", "Nord", &NORD},
    {"kanagawa", "Kanagawa", &KANAGAWA},
    {"everforestdark", "Everforest Dark", &EVERFORESTDARK},
    {"materialoceanic", "Material Oceanic", &MATERIALOCEANIC},

    // Light themes
    {"fangs-light", "Fangs Light", &FANGSLIGHT},
    {"onelight", "One Light", &ONELIGHT},
    {"githublight", "GitHub Light", &GITHUBLIGHT},
    {"gruvboxlight", "Gruvbox Light", &GRUVBOXLIGHT},
    {"solarizedlight", "Solarized Light", &SOLARIZEDLIGHT},
    {"catppuccinlatte", "Catppuccin Latte", &CATPPUCCINLATTE},
    {"ayulight", "Ayu Light", &AYULIGHT},
    {"rosepinedawn", "Rose Pine Dawn", &ROSEPINEDAWN},
    {"everforestlight", "Everforest Light", &EVERFORESTLIGHT},
    {"kanagawalotus", "Kanagawa Lotus", &KANAGAWALOTUS},
    {"onelightpro", "One Light Pro", &ONELIGHTPRO},
    {"draculasoft", "Dracula Soft", &DRACULASOFT},
    {"nordlight", "Nord Light", &NORDLIGHT},
    {"everforestlighthard", "Everforest Light Hard", &EVERFORESTLIGHTHARD},
};
```

---

## Task 3: Update legacy aliases

**Files:**
- Modify: `src/theme.c` (`theme_index_of`)

- [ ] **Step 3.1: Point `dark` and `light` to the Fangs themes**

Replace the alias lines in `theme_index_of`:

```c
    if (strcmp(slug, "dark") == 0)        // legacy aliases → brand themes
        slug = "fangs-dark";
    else if (strcmp(slug, "light") == 0)
        slug = "fangs-light";
```

---

## Task 4: Update tests for the new catalog

**Files:**
- Modify: `tests/theme_tests.c`

- [ ] **Step 4.1: Update count and legacy assertions**

Change `test_selector` as follows:

1. `EXPECT_TRUE(theme_count() == 20);` → `EXPECT_TRUE(theme_count() == 29);`
2. `EXPECT_TRUE(dark_count == 10);` → `EXPECT_TRUE(dark_count == 15);`
3. `EXPECT_TRUE(light_count == 10);` → `EXPECT_TRUE(light_count == 14);`
4. Replace the two legacy alias assertions:
   ```c
   EXPECT_TRUE(strcmp(theme_slug(theme_index_of("dark")), "fangs-dark") == 0);   // legacy
   EXPECT_TRUE(strcmp(theme_slug(theme_index_of("light")), "fangs-light") == 0);
   ```
5. Remove the expanded-catalog assertions for removed themes:
   - Delete `EXPECT_TRUE(!theme_resolve("ayumirage").is_light);`
   - Delete `EXPECT_TRUE(!theme_resolve("rosepine").is_light);`
   - Delete `EXPECT_TRUE(theme_resolve("lightmodern").is_light);`
   - Delete `EXPECT_TRUE(theme_resolve("tokyonightday").is_light);`
6. Add presence assertions for new themes:
   ```c
   EXPECT_TRUE(!theme_resolve("fangs-dark").is_light);
   EXPECT_TRUE(!theme_resolve("dracula").is_light);
   EXPECT_TRUE(!theme_resolve("nord").is_light);
   EXPECT_TRUE(!theme_resolve("kanagawa").is_light);
   EXPECT_TRUE(!theme_resolve("everforestdark").is_light);
   EXPECT_TRUE(!theme_resolve("materialoceanic").is_light);
   EXPECT_TRUE(!theme_resolve("catppuccinfrappe").is_light);

   EXPECT_TRUE(theme_resolve("fangs-light").is_light);
   EXPECT_TRUE(theme_resolve("kanagawalotus").is_light);
   EXPECT_TRUE(theme_resolve("onelightpro").is_light);
   EXPECT_TRUE(theme_resolve("draculasoft").is_light);
   EXPECT_TRUE(theme_resolve("nordlight").is_light);
   EXPECT_TRUE(theme_resolve("everforestlighthard").is_light);
   ```

- [ ] **Step 4.2: Verify test_resolve still passes**

`theme_resolve("dark")` must still return a dark theme and `theme_resolve("light")` a light theme; the new Fangs themes satisfy the bg/fg brightness checks.

---

## Task 5: Update documentation

**Files:**
- Modify: `README.md` (line 87)
- Modify: `docs/plan.md` (line 197) if it still lists the old first-class themes

- [ ] **Step 5.1: Update README theme list**

Replace:
```markdown
- **First-class theming** — One Dark, Dark Modern, GitHub Dark, Gruvbox, Monokai, and light
```
with:
```markdown
- **First-class theming** — Fangs Dark, One Dark, Dark Modern, GitHub Dark, Gruvbox, Monokai,
  Dracula, Nord, Kanagawa, Tokyo Night, Everforest, Material Oceanic, Catppuccin Mocha /
  Frappe, and light variants (Fangs Light, One Light, One Light Pro, GitHub Light,
  Gruvbox Light, Solarized Light, Catppuccin Latte, Ayu Light, Rose Pine Dawn,
  Kanagawa Lotus, Dracula Soft, Nord Light, Everforest Light). Every theme colors the
  full 256-color palette,
```
(Keep the remainder of the bullet: "so *all* output is colored...")

- [ ] **Step 5.2: Update docs/plan.md if stale**

If `docs/plan.md` still lists the old first-class theme set, replace it with the same expanded summary.

---

## Task 6: Build and test

**Files:**
- Run in repo root

- [ ] **Step 6.1: Rebuild**

Run: `cmake --build build`
Expected: no compile errors.

- [ ] **Step 6.2: Run ctest**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests pass, especially `theme_tests`, `ui_theme_tests`, and `settings_theme_dropdown_tests`.

- [ ] **Step 6.3: Spot-check legacy aliases**

Run the theme test binary directly:
`./build/tests/theme_tests`
Expected: silent exit (return 0).

---

## Task 7: Manual smoke test (optional but recommended)

**Files:**
- Run: `./build/fangs`

- [ ] **Step 7.1: Cycle themes in settings**

Launch `./build/fangs`, open settings (`Ctrl+,` / `Cmd+,`), switch between Dark and Light modes, and select each new theme. Verify the terminal background and UI chrome update instantly with no crashes.

- [ ] **Step 7.2: Confirm default theme on first launch**

If no config theme is set, `theme_resolve("dark")` and `theme_resolve("light")` should now return Fangs Dark / Fangs Light. The default background should be the charcoal/dark or warm off-white defined above.

---

## Self-review checklist

- [ ] Spec coverage: brand themes, 5 new dark, 5 new light, off-brand removals, tests, docs — each has a task.
- [ ] No placeholders: every step shows actual code or exact commands.
- [ ] Type consistency: all new themes are `static const Theme`, all entries use `&THEME`.
- [ ] Counts: `theme_count()` must equal 29 (15 dark + 14 light) after changes.

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-12-theme-refresh.md`.

Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — execute tasks in this session with batch execution and checkpoints.

Which approach?