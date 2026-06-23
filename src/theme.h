// Terminal color themes. Pure + testable (no raylib/ghostty types).
//
// Applied through the term_engine seam (term_engine_apply_theme), which pushes
// the default fg/bg/cursor AND the full 256-color palette into libghostty-vt via
// ghostty_terminal_set(GHOSTTY_TERMINAL_OPT_COLOR_*). Because the engine resolves
// palette-indexed cell colors, this themes ALL output — ls --color, vim, prompts
// — not just unstyled text.
#ifndef NOVA_THEME_H
#define NOVA_THEME_H

#include <stdbool.h>

typedef struct { unsigned char r, g, b; } ThemeColor;

typedef struct {
    ThemeColor bg;        // default background
    ThemeColor fg;        // default foreground
    ThemeColor cursor;    // cursor
    ThemeColor ansi[16];  // 16 ANSI colors (0-7 normal, 8-15 bright)
    bool is_light;
} Theme;

// Resolve a theme by config slug (e.g. "gruvbox"). Legacy "dark"/"light" map to
// One Dark / One Light. Unknown → the first theme (One Dark).
Theme theme_resolve(const char *slug);

// Theme registry, for building a selector. Index 0..theme_count()-1.
int         theme_count(void);
const char *theme_name(int index);   // display name, e.g. "Gruvbox"
const char *theme_slug(int index);   // config value, e.g. "gruvbox"
int         theme_index_of(const char *slug);   // slug → index (legacy-aware); 0 if unknown

// Fill out[256] with the full xterm 256-color palette: [0..15] from the theme's
// ANSI colors, [16..231] the 6x6x6 color cube, [232..255] the grayscale ramp.
void theme_build_palette256(const Theme *theme, ThemeColor out[256]);

#endif // NOVA_THEME_H
