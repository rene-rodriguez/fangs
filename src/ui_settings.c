#include "ui_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "theme.h"
#include "ui_theme.h"

typedef enum {
    EDIT_FONT_FAMILY,
    EDIT_FONT_SIZE,
    EDIT_SCROLLBACK,
    EDIT_ENDPOINT,
    EDIT_MODEL,
    EDIT_API_KEY,
    EDIT_MAX_TOKENS,
    EDIT_OLLAMA_NUM_CTX,
    EDIT_OLLAMA_NUM_GPU,
    EDIT_OLLAMA_NUM_THREAD,
    EDIT_OLLAMA_NUM_BATCH,
    EDIT_COUNT
} EditField;

typedef enum {
    SETTINGS_TAB_GENERAL,
    SETTINGS_TAB_AI,
    SETTINGS_TAB_ADVANCED,
    SETTINGS_TAB_COUNT
} SettingsTab;

static bool settings_open = false;
static bool draft_valid = false;
static bool theme_mode_dropdown_open = false;
static bool theme_name_dropdown_open = false;
static AppConfig draft;
static bool editing[EDIT_COUNT] = {0};
static SettingsTab settings_tab = SETTINGS_TAB_GENERAL;

static const char *settings_tab_label(SettingsTab tab)
{
    switch (tab) {
        case SETTINGS_TAB_GENERAL:  return "General";
        case SETTINGS_TAB_AI:         return "AI";
        case SETTINGS_TAB_ADVANCED:   return "Advanced";
        default:                      return "";
    }
}

static void clear_editing(void)
{
    for (int i = 0; i < EDIT_COUNT; i++)
        editing[i] = false;
}

static void reset_settings_state(void)
{
    draft_valid = false;
    theme_mode_dropdown_open = false;
    theme_name_dropdown_open = false;
    settings_tab = SETTINGS_TAB_GENERAL;
    clear_editing();
}

static int provider_index(const char *provider)
{
    if (strcmp(provider, "anthropic") == 0) return 1;
    if (strcmp(provider, "ollama") == 0) return 2;
    if (strcmp(provider, "custom") == 0) return 3;
    return 0;
}

static const char *provider_from_index(int index)
{
    switch (index) {
    case 1: return "anthropic";
    case 2: return "ollama";
    case 3: return "custom";
    default: return "openai";
    }
}

// When the user switches provider, prefill endpoint + model with that
// provider's defaults so the request shape and URL stay consistent. "custom"
// keeps whatever the user already typed.
static void apply_provider_defaults(AppConfig *c, const char *provider)
{
    if (strcmp(provider, "anthropic") == 0) {
        snprintf(c->endpoint, sizeof(c->endpoint),
                 "https://api.anthropic.com/v1/messages");
        snprintf(c->model, sizeof(c->model), "claude-opus-4-8");
    } else if (strcmp(provider, "ollama") == 0) {
        snprintf(c->endpoint, sizeof(c->endpoint),
                 "http://localhost:11434/api/chat");
        snprintf(c->model, sizeof(c->model), "llama3.1");
    } else if (strcmp(provider, "openai") == 0) {
        snprintf(c->endpoint, sizeof(c->endpoint),
                 "https://api.openai.com/v1/chat/completions");
        snprintf(c->model, sizeof(c->model), "gpt-4o-mini");
    }
}

static bool theme_index_is_light(int index)
{
    return theme_resolve(theme_slug(index)).is_light;
}

static int theme_registry_index_for_mode(bool want_light, int mode_index)
{
    int seen = 0;
    for (int i = 0; i < theme_count(); i++) {
        if (theme_index_is_light(i) != want_light)
            continue;
        if (seen == mode_index)
            return i;
        seen++;
    }

    for (int i = 0; i < theme_count(); i++)
        if (theme_index_is_light(i) == want_light)
            return i;
    return 0;
}

static int theme_mode_index_of(const char *slug, bool want_light)
{
    int registry_index = theme_index_of(slug);
    int mode_index = 0;
    for (int i = 0; i < theme_count(); i++) {
        if (theme_index_is_light(i) != want_light)
            continue;
        if (i == registry_index)
            return mode_index;
        mode_index++;
    }
    return 0;
}

static const char *theme_slug_for_mode_index(bool want_light, int mode_index)
{
    return theme_slug(theme_registry_index_for_mode(want_light, mode_index));
}

// Build a ';'-joined list of theme display names for the selected mode.
static const char *theme_combo_list(bool want_light)
{
    static char buf[256];
    buf[0] = '\0';
    for (int i = 0; i < theme_count(); i++) {
        if (theme_index_is_light(i) != want_light)
            continue;
        if (buf[0] != '\0')
            strncat(buf, ";", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, theme_name(i), sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}

static void begin_edit(EditField field)
{
    bool was_editing = editing[field];
    clear_editing();
    editing[field] = !was_editing;
}

static Color with_alpha(Color c, unsigned char a)
{
    c.a = a;
    return c;
}

static void draw_labeled_text_box(const char *label, Rectangle bounds,
                                  char *text, int text_size, EditField field,
                                  float s)
{
    GuiLabel((Rectangle){bounds.x, bounds.y - 22*s, bounds.width, 18*s}, label);
    if (CheckCollisionPointRec(GetMousePosition(), bounds))
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
    if (GuiTextBox(bounds, text, text_size, editing[field]))
        begin_edit(field);
}

static void draw_labeled_spinner(const char *label, Rectangle bounds,
                                 int *value, int min_value, int max_value,
                                 EditField field, float s)
{
    GuiLabel((Rectangle){bounds.x, bounds.y - 22*s, bounds.width, 18*s}, label);
    if (CheckCollisionPointRec(GetMousePosition(), bounds))
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (GuiSpinner(bounds, NULL, value, min_value, max_value, editing[field]))
        begin_edit(field);
}

static void settings_draw_tabs(float x, float y, float w, float tab_h,
                               float gap, float s)
{
    Color accent = UI2RAY(g_ui_theme.accent);
    Color subtitle = UI2RAY(g_ui_theme.subtitle);
    Color text = UI2RAY(g_ui_theme.text);

    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        Rectangle r = { x, y + (float)i * (tab_h + gap), w, tab_h };
        bool hovered = CheckCollisionPointRec(GetMousePosition(), r);
        bool active = (i == (int)settings_tab);

        if (active) {
            DrawRectangleRounded(r, 0.25f, 8, with_alpha(accent, 30));
        } else if (hovered) {
            DrawRectangleRounded(r, 0.25f, 8, with_alpha(accent, 12));
        }

        Color fg = active ? accent : (hovered ? text : subtitle);
        const char *label = settings_tab_label((SettingsTab)i);
        Vector2 sz = MeasureTextEx(GetFontDefault(), label, 14.0f * s, 0);
        float tx = r.x + 14.0f * s;
        float ty = r.y + (r.height - sz.y) * 0.5f;
        DrawTextEx(GetFontDefault(), label, (Vector2){tx, ty}, 14.0f * s, 0, fg);

        if (hovered || active)
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            settings_tab = (SettingsTab)i;
            clear_editing();
            theme_mode_dropdown_open = false;
            theme_name_dropdown_open = false;
        }
    }
}

bool ui_settings_open(void)
{
    return settings_open;
}

void ui_settings_toggle(void)
{
    settings_open = !settings_open;
    reset_settings_state();
}

void ui_settings_draw(AppConfig *cfg, bool *out_saved, float scale)
{
    if (out_saved)
        *out_saved = false;
    if (!settings_open)
        return;

    float s = (scale > 0.1f) ? scale : 1.0f;

    if (!draft_valid) {
        draft = *cfg;
        draft_valid = true;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        settings_open = false;
        reset_settings_state();
        return;
    }

    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    DrawRectangle(0, 0, screen_w, screen_h, UI2RAY(g_ui_theme.modal_overlay));

    bool ollama_selected = (provider_index(draft.provider) == 2);

    float panel_w = 620.0f * s;
    float panel_h = 520.0f * s + (ollama_selected ? 140.0f * s : 0.0f);
    if (panel_w > screen_w - 32.0f*s) panel_w = (float)screen_w - 32.0f*s;
    if (panel_h > screen_h - 32.0f*s) panel_h = (float)screen_h - 32.0f*s;
    Rectangle panel = {
        ((float)screen_w - panel_w) / 2.0f,
        ((float)screen_h - panel_h) / 2.0f,
        panel_w,
        panel_h
    };

    GuiPanel(panel, "Settings");

    const float margin = 20.0f * s;
    const float row_h = 32.0f * s;
    const float gap = 22.0f * s;
    const float col_gap = 18.0f * s;
    const float sidebar_w = 130.0f * s;
    const float tab_h = 34.0f * s;
    const float tab_gap = 4.0f * s;

    float sidebar_x = panel.x + margin;
    float sidebar_y = panel.y + 48.0f * s;

    settings_draw_tabs(sidebar_x, sidebar_y, sidebar_w, tab_h, tab_gap, s);

    float content_x = sidebar_x + sidebar_w + margin * 0.5f;
    float content_w = panel.width - sidebar_w - margin * 2.5f;
    float x = content_x;
    float y = panel.y + 48.0f * s;
    float full_w = content_w;
    float half_w = (full_w - col_gap) / 2.0f;

    bool any_dropdown_open = theme_mode_dropdown_open || theme_name_dropdown_open;
    if (any_dropdown_open)
        GuiLock();

    if (settings_tab == SETTINGS_TAB_GENERAL) {
        GuiLabel((Rectangle){x, y, full_w, 20*s}, "Terminal");
        y += 32.0f*s;

        draw_labeled_text_box("Font family", (Rectangle){x, y, half_w, row_h},
                              draft.font_family, (int)sizeof(draft.font_family),
                              EDIT_FONT_FAMILY, s);
        draw_labeled_spinner("Font size", (Rectangle){x + half_w + col_gap, y, half_w, row_h},
                             &draft.font_size, 8, 72, EDIT_FONT_SIZE, s);
        y += row_h + gap + 18.0f*s;

        bool theme_mode_light = theme_resolve(draft.theme).is_light;
        int active_theme_mode = theme_mode_light ? 1 : 0;
        int prev_theme_mode = active_theme_mode;
        int active_theme = theme_mode_index_of(draft.theme, theme_mode_light);
        Rectangle theme_mode_bounds = {x, y, half_w, row_h};
        Rectangle theme_bounds = {x + half_w + col_gap, y, half_w, row_h};
        GuiLabel((Rectangle){theme_mode_bounds.x, y - 22.0f*s, half_w, 18*s}, "Theme mode");
        GuiLabel((Rectangle){theme_bounds.x, y - 22.0f*s, half_w, 18*s}, "Theme");
        if (CheckCollisionPointRec(GetMousePosition(), theme_mode_bounds)
            || CheckCollisionPointRec(GetMousePosition(), theme_bounds))
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        y += row_h + gap + 18.0f*s;

        draw_labeled_spinner("Scrollback", (Rectangle){x, y, half_w, row_h},
                             &draft.scrollback, 100, 100000, EDIT_SCROLLBACK, s);
        y += row_h + gap + 18.0f*s;

        if (any_dropdown_open)
            GuiUnlock();

        if (GuiDropdownBox(theme_bounds, theme_combo_list(theme_mode_light), &active_theme, theme_name_dropdown_open)) {
            theme_name_dropdown_open = !theme_name_dropdown_open;
            if (theme_name_dropdown_open) {
                theme_mode_dropdown_open = false;
                clear_editing();
            }
        }
        if (GuiDropdownBox(theme_mode_bounds, "Dark;Light", &active_theme_mode, theme_mode_dropdown_open)) {
            theme_mode_dropdown_open = !theme_mode_dropdown_open;
            if (theme_mode_dropdown_open) {
                theme_name_dropdown_open = false;
                clear_editing();
            }
        }

        if (active_theme_mode != prev_theme_mode) {
            theme_mode_light = (active_theme_mode == 1);
            active_theme = 0;
        }
        snprintf(draft.theme, sizeof(draft.theme), "%s",
                 theme_slug_for_mode_index(theme_mode_light, active_theme));
    } else if (settings_tab == SETTINGS_TAB_AI) {
        GuiLabel((Rectangle){x, y, full_w, 20*s}, "AI");
        y += 32.0f*s;

        GuiLabel((Rectangle){x, y - 22.0f*s, full_w, 18*s}, "Provider");
        int active_provider = provider_index(draft.provider);
        int prev_provider = active_provider;
        Rectangle provider_bounds = {x, y, 110*s, row_h};
        if (CheckCollisionPointRec(GetMousePosition(), provider_bounds))
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        GuiToggleGroup(provider_bounds, "openai;anthropic;ollama;custom", &active_provider);
        snprintf(draft.provider, sizeof(draft.provider), "%s", provider_from_index(active_provider));
        if (active_provider != prev_provider)
            apply_provider_defaults(&draft, draft.provider);
        y += row_h + gap + 18.0f*s;

        draw_labeled_text_box("Endpoint", (Rectangle){x, y, full_w, row_h},
                              draft.endpoint, (int)sizeof(draft.endpoint),
                              EDIT_ENDPOINT, s);
        y += row_h + gap + 18.0f*s;

        draw_labeled_text_box("Model", (Rectangle){x, y, half_w, row_h},
                              draft.model, (int)sizeof(draft.model),
                              EDIT_MODEL, s);
        draw_labeled_spinner("Max tokens", (Rectangle){x + half_w + col_gap, y, half_w, row_h},
                             &draft.max_tokens, 1, 200000, EDIT_MAX_TOKENS, s);
        y += row_h + gap + 18.0f*s;

        const char *env_key = getenv("FANGS_API_KEY");
        bool key_from_env = env_key && env_key[0] != '\0';
        GuiLabel((Rectangle){x, y - 22.0f*s, half_w, 18*s}, "API key");
        if (key_from_env)
            GuiDisable();
        Rectangle api_key_bounds = {x, y, half_w, row_h};
        if (!key_from_env && CheckCollisionPointRec(GetMousePosition(), api_key_bounds))
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
        if (GuiTextBox(api_key_bounds, draft.api_key,
                       (int)sizeof(draft.api_key), editing[EDIT_API_KEY])) {
            begin_edit(EDIT_API_KEY);
        }
        if (key_from_env) {
            GuiEnable();
            GuiLabel((Rectangle){x + half_w + 12.0f*s, y + 6.0f*s, 120.0f*s, 18.0f*s}, "(from env)");
        }

        bool stream_checked = draft.stream;
        Rectangle stream_bounds = {x + half_w + col_gap, y + 6.0f*s, 18.0f*s, 18.0f*s};
        if (CheckCollisionPointRec(GetMousePosition(), stream_bounds))
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        GuiCheckBox(stream_bounds, "Stream responses", &stream_checked);
        draft.stream = stream_checked;
    } else if (settings_tab == SETTINGS_TAB_ADVANCED) {
        if (ollama_selected) {
            GuiLabel((Rectangle){x, y, full_w, 20*s}, "Ollama options");
            y += 32.0f*s;

            draw_labeled_spinner("Context (0=default)", (Rectangle){x, y, half_w, row_h},
                                 &draft.ollama_num_ctx, 0, 131072, EDIT_OLLAMA_NUM_CTX, s);
            draw_labeled_spinner("GPU layers (0=CPU, -1=auto)", (Rectangle){x + half_w + col_gap, y, half_w, row_h},
                                 &draft.ollama_num_gpu, -1, 999, EDIT_OLLAMA_NUM_GPU, s);
            y += row_h + gap + 18.0f*s;

            draw_labeled_spinner("Threads (0=auto)", (Rectangle){x, y, half_w, row_h},
                                 &draft.ollama_num_thread, 0, 128, EDIT_OLLAMA_NUM_THREAD, s);
            draw_labeled_spinner("Batch (0=default)", (Rectangle){x + half_w + col_gap, y, half_w, row_h},
                                 &draft.ollama_num_batch, 0, 8192, EDIT_OLLAMA_NUM_BATCH, s);
            y += row_h + gap + 18.0f*s;
        } else {
            GuiLabel((Rectangle){x, y, full_w, row_h},
                     "Advanced options are available when Ollama is selected.");
            y += row_h + gap;
        }
    }

    if (any_dropdown_open && settings_tab != SETTINGS_TAB_GENERAL)
        GuiUnlock();

    Rectangle cancel = {panel.x + panel.width - 202.0f*s, panel.y + panel.height - 54.0f*s, 82.0f*s, 30.0f*s};
    Rectangle save = {panel.x + panel.width - 106.0f*s, panel.y + panel.height - 54.0f*s, 82.0f*s, 30.0f*s};

    if (CheckCollisionPointRec(GetMousePosition(), cancel)
        || CheckCollisionPointRec(GetMousePosition(), save))
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

    if (GuiButton(cancel, "Cancel")) {
        settings_open = false;
        reset_settings_state();
    }

    if (GuiButton(save, "Save")) {
        *cfg = draft;
        settings_open = false;
        reset_settings_state();
        if (out_saved)
            *out_saved = true;
    }
}
