// UI chrome theming — derive chrome colors from the terminal Theme.
// Pure + testable (no raylib/ghostty types). Mirror of theme.h.
#ifndef FANGS_UI_THEME_H
#define FANGS_UI_THEME_H

#include "theme.h"

typedef struct { unsigned char r, g, b, a; } UiColor;

typedef struct {
    UiColor terminal_bg;                 // terminal content surface
    UiColor panel_bg, panel_border;      // sidebar / settings / inline overlay
    UiColor selection;                    // terminal text selection wash
    UiColor search_bg, search_border, search_hit;
    UiColor search_text, search_count;    // search box label / match-count
    UiColor scrollbar;                    // thumb
    unsigned char cursor_alpha;           // block-cursor fill alpha
    UiColor msg_user, msg_assistant, msg_system;  // sidebar role tints
    UiColor text;                         // sidebar / inline normal text
    UiColor subtitle;                     // sidebar subtitle ("streaming…")
    UiColor reasoning;                    // sidebar "thinking" text
    UiColor run_button, run_button_hover; // sidebar Run button
    UiColor accent;                       // buttons, focus, ⚡ Explain error
    UiColor warn;                         // toasts / warnings
    UiColor danger;                       // toasts / errors
    UiColor inline_bg, inline_border;     // Ctrl+Space floating prompt

    // Chrome overlays (derived from theme, never hardcoded).
    UiColor modal_overlay;                // settings-modal semi-transparent backdrop
    UiColor inline_error;                 // inline-prompt error text (red-ish)
    UiColor focus_border;                 // focused-pane highlight border
    UiColor sidebar_separator;            // vertical divider between grid and sidebar
    UiColor exit_banner_bg;               // process-exit banner background
    UiColor exit_banner_text;             // process-exit banner text

    UiColor pane_header_bg;               // title bar background
    UiColor pane_header_text;             // session/cwd text
    UiColor pane_header_detail;           // branch / dim detail
    UiColor pane_status_running;          // green dot
    UiColor pane_status_idle;             // amber dot
    UiColor pane_status_error;            // red dot
    UiColor shadow;                       // drop shadow fill
    UiColor gutter_hover;                 // resize hint line
} UiTheme;

// Convenience: convert UiColor (our struct) to raylib Color.
#define UI2RAY(uc) ((Color){ (uc).r, (uc).g, (uc).b, (uc).a })

// Semantic color constants for UiMenuItem tint (assigned from the current
// UiTheme at popover-build time via the g_ui_theme global).
#define UI_COLOR_TEXT          g_ui_theme.text
#define UI_COLOR_SUBTITLE      g_ui_theme.subtitle
#define UI_COLOR_INLINE_ERROR  g_ui_theme.inline_error
#define UI_COLOR_ACCENT        g_ui_theme.accent

// Derive chrome colors from a terminal Theme. Pure function.
UiTheme ui_theme_derive(const Theme *t);

// Global derived UI theme — updated by main.c on theme change.
extern UiTheme g_ui_theme;

#endif // FANGS_UI_THEME_H
