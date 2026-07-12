# Theme Catalog Refresh — Design Spec

Date: 2026-07-12
Status: Approved by user

## Goal

Expand the Fangs terminal theme catalog by five new dark themes and five new light themes, introduce brand-aligned "Fangs Dark" / "Fangs Light" flagship themes, and remove off-brand themes that clash with Fangs' cmux-style, developer-first identity.

## Current catalog

Dark (10):
- One Dark, Dark Modern, GitHub Dark, Gruvbox, Monokai, Solarized Dark, Catppuccin Mocha, Ayu Mirage, Tokyo Night, Rose Pine

Light (10):
- One Light, Light Modern, GitHub Light, Gruvbox Light, Solarized Light, Catppuccin Latte, Ayu Light, Tokyo Night Day, Rose Pine Dawn, Everforest Light

## Proposed new catalog

### New house themes

Two new brand-aligned themes become the defaults and take over the legacy `dark`/`light` config aliases:

- **Fangs Dark** (`fangs-dark`) — charcoal background, muted teal/amber accents. Tuned for long agent sessions, rail readability, and attention-dot visibility.
- **Fangs Light** (`fangs-light`) — warm off-white background, deep slate text, same accent family.

### Dark themes (15 total)

Add:
1. Dracula (`dracula`)
2. Nord (`nord`)
3. Kanagawa (`kanagawa`)
4. Everforest Dark (`everforestdark`)
5. Material Oceanic (`materialoceanic`)

Move from light (plan correction):
- Catppuccin Frappe — canonically a dark Catppuccin variant; moved to the dark section and added One Light Pro as the replacement true light theme.

Remove:
- Ayu Mirage — warm/beige palette feels off-brand for a terminal with urgency signals.
- Rose Pine — pastel purple/brown tones mute the rail attention dots and exit-status badges.

Remaining dark list:
Fangs Dark, One Dark, Dark Modern, GitHub Dark, Gruvbox, Monokai, Solarized Dark, Catppuccin Mocha, Catppuccin Frappe, Tokyo Night, Dracula, Nord, Kanagawa, Everforest Dark, Material Oceanic

(Started with 10; add 5 new plus Fangs Dark, move Frappe from light, drop 2 off-brand = 15.)

### Light themes (14 total)

Add:
1. Kanagawa Lotus (`kanagawalotus`)
2. One Light Pro (`onelightpro`)
3. Dracula Soft (`draculasoft`)
4. Nord Light (`nordlight`)
5. Everforest Light Hard (`everforestlighthard`)

Remove:
- Light Modern — generic VS Code default, weak brand identity and low contrast.
- Tokyo Night Day — blue-on-light text strains readability and feels off-brand.
- Catppuccin Frappe — moved to the dark section (see above).

Remaining light list:
Fangs Light, One Light, GitHub Light, Gruvbox Light, Solarized Light, Catppuccin Latte, Ayu Light, Rose Pine Dawn, Everforest Light, Kanagawa Lotus, One Light Pro, Dracula Soft, Nord Light, Everforest Light Hard

(Started with 10; add 5 new plus Fangs Light, drop 2 off-brand, move Frappe to dark = 14.)

## Implementation scope

All changes are in `src/theme.c`:
- Define new `Theme` structs for the 12 new themes (2 Fangs + 5 dark + 5 light).
- Update the `ENTRIES` registry to reflect the final catalog and ordering.
- Update `theme_index_of` so the legacy slugs `dark` and `light` map to `fangs-dark` and `fangs-light` instead of `onedark` and `onelight`.

Secondary changes:
- Update `README.md` first-class theme list.
- Update any tests that assert theme count (expected: 29 total: 15 dark + 14 light).

## Validation

- `cmake --build build` succeeds.
- `ctest` passes (in particular the theme test if it checks theme count or resolution).
- `./build/fangs` launches and cycles through all new themes in the settings modal without visual regressions.

## Out of scope

- No changes to the UI derivation logic in `ui_theme.c`; it already derives chrome colors from each `Theme`.
- No new config keys or settings UI work; the existing theme dropdown is populated from the registry automatically.
