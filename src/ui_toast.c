// ui_toast — Non-blocking notification toast overlay.
#include "ui_toast.h"

#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define TOAST_RING 16

typedef struct {
    ToastLevel level;
    char msg[TOAST_MAX_MSG];
    double ttl;       // remaining seconds
    double max_ttl;   // initial TTL (for fade calc)
    double birth;     // monotonic age in seconds (0 at push, increases via tick)
} ToastEntry;

static struct {
    ToastEntry ring[TOAST_RING];
    int head;         // oldest index
    int count;        // active entries (wrap after head)
} g_toast = {0};

void toast_push(ToastLevel level, const char *msg)
{
    double ttl = 0.0;
    switch (level) {
        case TOAST_INFO:  ttl = TOAST_TTL_INFO;  break;
        case TOAST_WARN:  ttl = TOAST_TTL_WARN;  break;
        case TOAST_ERROR: ttl = TOAST_TTL_ERROR; break;
    }

    int idx = (g_toast.head + g_toast.count) % TOAST_RING;
    if (g_toast.count == TOAST_RING) {
        // Ring full: overwrite oldest, advance head.
        idx = g_toast.head;
        g_toast.head = (g_toast.head + 1) % TOAST_RING;
    } else {
        g_toast.count++;
    }

    g_toast.ring[idx].level   = level;
    g_toast.ring[idx].ttl     = ttl;
    g_toast.ring[idx].max_ttl = ttl;
    g_toast.ring[idx].birth   = 0.0;
    snprintf(g_toast.ring[idx].msg, TOAST_MAX_MSG, "%s", msg ? msg : "");
}

void toast_tick(double dt)
{
    // Walk active entries backwards so removal is O(n).
    int n = g_toast.count;
    for (int i = n - 1; i >= 0; i--) {
        int idx = (g_toast.head + i) % TOAST_RING;
        g_toast.ring[idx].ttl -= dt;
        g_toast.ring[idx].birth += dt;
        if (g_toast.ring[idx].ttl <= 0.0) {
            // Remove this entry: shift remaining forwards.
            for (int j = i; j < g_toast.count - 1; j++) {
                int src = (g_toast.head + j + 1) % TOAST_RING;
                int dst = (g_toast.head + j) % TOAST_RING;
                g_toast.ring[dst] = g_toast.ring[src];
            }
            g_toast.count--;
        }
    }

    // If count dropped to zero, reset head to keep it clean.
    if (g_toast.count == 0) g_toast.head = 0;
}

int toast_count(void)
{
    return g_toast.count;
}

bool toast_get(int i, ToastLevel *level, const char **msg, float *alpha)
{
    if (i < 0 || i >= g_toast.count) return false;
    int idx = (g_toast.head + g_toast.count - 1 - i) % TOAST_RING;
    if (level) *level = g_toast.ring[idx].level;
    if (msg)   *msg   = g_toast.ring[idx].msg;
    if (alpha) {
        double remain = g_toast.ring[idx].ttl;
        double max    = g_toast.ring[idx].max_ttl;
        // Hold at full opacity, then ease out over the trailing fraction of
        // the toast's life so it doesn't start dimming the instant it appears.
        const double fade_frac = 0.25;
        double fade_window = max * fade_frac;
        if (fade_window > 0.0 && remain < fade_window) {
            float t = (float)(remain / fade_window);
            *alpha = t * t; // ease-out (quadratic)
        } else {
            *alpha = 1.0f;
        }
        if (*alpha < 0.0f) *alpha = 0.0f;
        if (*alpha > 1.0f) *alpha = 1.0f;
    }
    return true;
}

void toast_clear(void)
{
    g_toast.head  = 0;
    g_toast.count = 0;
}

#include "raylib.h"

void toast_draw(Font font, float scale, double dt)
{
    (void)dt; // age is advanced by toast_tick(); passed for API symmetry
    int n = g_toast.count;
    if (n == 0) return;

    int sw = GetRenderWidth();
    int sh = GetRenderHeight();
    fprintf(stderr, "[toast_draw] render sw=%d sh=%d\n", sw, sh);

    // Unscaled design constants (logical px). Multiplied by scale for output.
    const int margin_x      = 12;
    const int margin_y      = 12;
    const int toast_w       = 320;
    const int min_h         = 44;
    const int corner_radius = 8;
    const int shadow_off    = 3;
    const int shadow_alpha  = 45;
    const int accent_w      = 4;
    const int icon_size     = 18;
    const int icon_pad      = 10;
    const int text_pad      = 12;
    const int bar_h         = 2;
    const double enter_ms   = 150.0;
    const double exit_ms    = 250.0;

    float font_size = 14.0f * scale;

    int y = margin_y; // start at top-right, newest drawn first at the top
    for (int i = n - 1; i >= 0; i--) {
        int idx = (g_toast.head + i) % TOAST_RING;
        ToastEntry *te = &g_toast.ring[idx];

        double age = te->birth;
        double ttl = te->ttl;
        double max_ttl = te->max_ttl;

        // Per-toast fade/anim alpha independent of toast_get() hold+fade.
        float anim_alpha = 1.0f;
        float enter_t = 0.0f;
        if (age * 1000.0 < enter_ms) {
            enter_t = (float)(age * 1000.0 / enter_ms);
            if (enter_t < 0.0f) enter_t = 0.0f;
            if (enter_t > 1.0f) enter_t = 1.0f;
            // ease-out cubic
            float et = 1.0f - enter_t;
            anim_alpha = 1.0f - (et * et * et);
        }

        float exit_t = 0.0f;
        if (ttl * 1000.0 < exit_ms && max_ttl > 0.0) {
            exit_t = (float)(ttl / (exit_ms / 1000.0));
            if (exit_t < 0.0f) exit_t = 0.0f;
            if (exit_t > 1.0f) exit_t = 1.0f;
            float et = 1.0f - exit_t;
            anim_alpha = 1.0f - (et * et); // ease-in quadratic
        }

        if (anim_alpha <= 0.0f) continue;

        // Compute text size (wrap not needed for short toasts; clamp to toast width).
        Vector2 tsz = MeasureTextEx(font, te->msg, font_size, 0);
        int text_w = (int)tsz.x;
        int text_h = (int)tsz.y;
        (void)text_w;

        int w = (int)(toast_w * scale);
        int h = (int)fmaxf(min_h * scale, text_h + (int)(20 * scale));
        int x = sw - w - (int)(margin_x * scale);
        (void)x;

        // Progress along remaining TTL (0..1).
        float progress = (max_ttl > 0.0) ? (float)(ttl / max_ttl) : 0.0f;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        Color level_color;
        switch (te->level) {
            case TOAST_WARN:  level_color = g_ui_theme.warn.a ? UI2RAY(g_ui_theme.warn)
                                                              : (Color){235, 160, 40, 255}; break;
            case TOAST_ERROR: level_color = g_ui_theme.danger.a ? UI2RAY(g_ui_theme.danger)
                                                                : (Color){235, 80, 70, 255}; break;
            default:          level_color = UI2RAY(g_ui_theme.accent); break;
        }

        // Panel fill with near-opaque alpha modulated by animation.
        Color fill = UI2RAY(g_ui_theme.panel_bg);
        fill.a = (unsigned char)(240.0f * anim_alpha);

        Color text = UI2RAY(g_ui_theme.text);
        text.a = (unsigned char)(255.0f * anim_alpha);

        Color accent = level_color;
        accent.a = (unsigned char)(255.0f * anim_alpha);

        Color shadow = (Color){0, 0, 0, (unsigned char)(shadow_alpha * anim_alpha)};

        // Enter scale pop: 0.95 -> 1.0.  Exit slight scale up + fade is
        // handled by anim_alpha and draw offset.
        float toast_scale = 1.0f;
        if (enter_t < 1.0f) {
            toast_scale = 0.95f + 0.05f * enter_t;
        } else if (exit_t > 0.0f) {
            toast_scale = 1.0f + 0.02f * (1.0f - exit_t);
        }

        // Compute final rectangle after optional scale (pivot at top-right).
        float draw_w = w * toast_scale;
        float draw_h = h * toast_scale;
        float draw_x = (float)(sw - (int)(margin_x * scale) - (int)draw_w);
        float draw_y = (float)y;

        // Drop shadow (offset rectangle, slightly larger).
        Rectangle shadow_rec = {
            draw_x + shadow_off * scale,
            draw_y + shadow_off * scale,
            draw_w,
            draw_h
        };
        DrawRectangleRounded(shadow_rec, (float)corner_radius / 8.0f, 8, shadow);

        // Card background.
        Rectangle card_rec = { draw_x, draw_y, draw_w, draw_h };
        DrawRectangleRounded(card_rec, (float)corner_radius / 8.0f, 8, fill);

        // Left accent strip.
        Rectangle accent_rec = { draw_x, draw_y, accent_w * scale, draw_h };
        DrawRectangleRounded(accent_rec, 0.5f, 4, accent);

        // Icon area.
        int ic = (int)(icon_size * scale);
        float icon_cx = draw_x + (accent_w + icon_pad) * scale + ic * 0.5f;
        float icon_cy = draw_y + draw_h * 0.5f;
        float icon_radius = ic * 0.5f;
        Color icon_bg = accent;

        switch (te->level) {
            case TOAST_WARN: {
                Vector2 v1 = { icon_cx, icon_cy - icon_radius + 2 * scale };
                Vector2 v2 = { icon_cx - icon_radius + 2 * scale, icon_cy + icon_radius - 2 * scale };
                Vector2 v3 = { icon_cx + icon_radius - 2 * scale, icon_cy + icon_radius - 2 * scale };
                DrawTriangle(v1, v2, v3, icon_bg);
                Vector2 ex = MeasureTextEx(font, "!", font_size, 0);
                DrawTextEx(font, "!",
                           (Vector2){ icon_cx - ex.x * 0.5f, icon_cy - ex.y * 0.5f },
                           font_size, 0, (Color){255, 255, 255, (unsigned char)(255 * anim_alpha)});
                break;
            }
            case TOAST_ERROR: {
                DrawCircleV((Vector2){icon_cx, icon_cy}, icon_radius, icon_bg);
                Vector2 ex = MeasureTextEx(font, "x", font_size, 0);
                DrawTextEx(font, "x",
                           (Vector2){ icon_cx - ex.x * 0.5f, icon_cy - ex.y * 0.5f },
                           font_size, 0, (Color){255, 255, 255, (unsigned char)(255 * anim_alpha)});
                break;
            }
            default: { // INFO
                DrawCircleV((Vector2){icon_cx, icon_cy}, icon_radius, icon_bg);
                Vector2 ex = MeasureTextEx(font, "i", font_size, 0);
                DrawTextEx(font, "i",
                           (Vector2){ icon_cx - ex.x * 0.5f, icon_cy - ex.y * 0.5f },
                           font_size, 0, (Color){255, 255, 255, (unsigned char)(255 * anim_alpha)});
                break;
            }
        }

        // Text label, vertically centered.
        float text_x = icon_cx + icon_radius + text_pad * scale;
        float text_y = draw_y + (draw_h - text_h) * 0.5f;
        DrawTextEx(font, te->msg, (Vector2){text_x, text_y}, font_size, 0, text);

        // Progress bar along bottom edge.
        float bar_y = draw_y + draw_h - bar_h * scale;
        if (bar_y > draw_y) {
            float bar_w = draw_w * progress;
            Rectangle bar_rec = { draw_x, bar_y, bar_w, bar_h * scale };
            DrawRectangleRounded(bar_rec, 0.3f, 4, accent);
        }

        y += (int)(draw_h + 8 * scale);
        if (y > sh - (int)(margin_y * scale)) break; // avoid running off screen
    }
}
