#include "ui_sidebar.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "raygui.h"
#include "ui_sidebar_model.h"
#include "cmdextract.h"

#define MAX_MESSAGES 256
#define MESSAGE_TEXT_SIZE 8192
#define INPUT_TEXT_SIZE 1024
#define COMMAND_SIZE 1024

typedef struct {
    MsgRole role;
    char text[MESSAGE_TEXT_SIZE];        // user/system text, or assistant ANSWER
    char reasoning[MESSAGE_TEXT_SIZE];   // assistant thinking (empty otherwise)
    bool streaming;
    bool has_command;
    char command[COMMAND_SIZE];
} SidebarMessage;

static bool sidebar_visible = false;
static bool sidebar_focused = false;
static bool input_editing = false;
static char input_text[INPUT_TEXT_SIZE] = "";
static SidebarMessage messages[MAX_MESSAGES];
static int  message_count = 0;
static int  streaming_index = -1;
static float scroll_offset = 0.0f;

static bool point_in_rect(Vector2 p, Rect r)
{
    return p.x >= (float)r.x && p.x < (float)(r.x + r.w)
        && p.y >= (float)r.y && p.y < (float)(r.y + r.h);
}

static const char *role_label(MsgRole role)
{
    switch (role) {
    case MSG_USER: return "You";
    case MSG_ASSISTANT: return "Assistant";
    case MSG_SYSTEM: return "System";
    default: return "Message";
    }
}

static Color role_color(MsgRole role)
{
    switch (role) {
    case MSG_USER: return (Color){225, 235, 255, 255};
    case MSG_ASSISTANT: return (Color){230, 255, 235, 255};
    case MSG_SYSTEM: return (Color){210, 210, 210, 255};
    default: return RAYWHITE;
    }
}

static void copy_text(char *dst, int dst_size, const char *src)
{
    if (dst_size <= 0)
        return;
    snprintf(dst, (size_t)dst_size, "%s", src ? src : "");
}

// Append `src` to `dst` (a fixed buffer of dst_size), truncating if full.
static void append_bounded(char *dst, int dst_size, const char *src)
{
    if (!src || dst_size <= 0)
        return;
    size_t cur = strlen(dst);
    if (cur + 1 >= (size_t)dst_size)
        return;
    size_t room = (size_t)dst_size - 1 - cur;
    size_t n = strlen(src);
    if (n > room)
        n = room;
    memcpy(dst + cur, src, n);
    dst[cur + n] = '\0';
}

// Word-wrap `text` to max_width, honoring embedded '\n' as hard breaks.
// When draw is false, just advances *y (used to measure height).
static void wrapped_text(Font font, const char *text, float x, float *y,
                         float max_width, float font_size,
                         float line_spacing, Color color, bool draw)
{
    const char *p = text;
    char line[1024] = "";
    int line_len = 0;

    while (*p) {
        if (*p == '\n') {                       // hard line break
            if (draw)
                DrawTextEx(font, line, (Vector2){x, *y}, font_size, 0, color);
            *y += line_spacing;
            line[0] = '\0';
            line_len = 0;
            p++;
            continue;
        }
        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }

        char word[256];
        int word_len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n'
               && word_len < (int)sizeof(word) - 1)
            word[word_len++] = *p++;
        word[word_len] = '\0';
        if (word_len == 0)
            continue;

        char candidate[1024];
        if (line_len == 0)
            snprintf(candidate, sizeof(candidate), "%s", word);
        else
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);

        Vector2 size = MeasureTextEx(font, candidate, font_size, 0);
        if (line_len > 0 && size.x > max_width) {
            if (draw)
                DrawTextEx(font, line, (Vector2){x, *y}, font_size, 0, color);
            *y += line_spacing;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        line_len = (int)strlen(line);
    }

    if (line_len > 0) {
        if (draw)
            DrawTextEx(font, line, (Vector2){x, *y}, font_size, 0, color);
        *y += line_spacing;
    }
}

static float measure_wrapped_text(Font font, const char *text, float max_width,
                                  float font_size, float line_spacing)
{
    float y = 0.0f;
    wrapped_text(font, text, 0.0f, &y, max_width, font_size,
                 line_spacing, BLANK, false);
    return y;
}

void ui_sidebar_toggle(void)
{
    sidebar_visible = !sidebar_visible;
    sidebar_focused = sidebar_visible;
    input_editing = sidebar_visible;
}

bool ui_sidebar_visible(void) { return sidebar_visible; }
bool ui_sidebar_focused(void) { return sidebar_focused; }

void ui_sidebar_focus(bool on)
{
    sidebar_focused = on;
    input_editing = on;
}

// Reserve the next message slot, evicting the oldest if full. Keeps
// streaming_index pointing at the right message after an eviction shift.
static int alloc_slot(void)
{
    if (message_count == MAX_MESSAGES) {
        memmove(&messages[0], &messages[1],
                sizeof(messages[0]) * (MAX_MESSAGES - 1));
        message_count--;
        if (streaming_index > 0)
            streaming_index--;
        else if (streaming_index == 0)
            streaming_index = -1;
    }
    return message_count++;
}

void ui_sidebar_push(MsgRole role, const char *text)
{
    if (!text || text[0] == '\0')
        return;
    int i = alloc_slot();
    messages[i].role = role;
    copy_text(messages[i].text, (int)sizeof(messages[i].text), text);
    messages[i].reasoning[0] = '\0';
    messages[i].streaming = false;
    messages[i].has_command = false;
    messages[i].command[0] = '\0';
    scroll_offset = 1e9f;   // jump to bottom (clamped next draw)
}

void ui_sidebar_begin_assistant(void)
{
    int i = alloc_slot();
    messages[i].role = MSG_ASSISTANT;
    messages[i].text[0] = '\0';
    messages[i].reasoning[0] = '\0';
    messages[i].streaming = true;
    messages[i].has_command = false;
    messages[i].command[0] = '\0';
    streaming_index = i;
    scroll_offset = 1e9f;
}

void ui_sidebar_append_assistant(const char *delta, bool is_reasoning)
{
    if (streaming_index < 0 || !delta)
        return;
    SidebarMessage *m = &messages[streaming_index];
    if (is_reasoning)
        append_bounded(m->reasoning, (int)sizeof(m->reasoning), delta);
    else
        append_bounded(m->text, (int)sizeof(m->text), delta);
    scroll_offset = 1e9f;   // follow the stream to the bottom
}

void ui_sidebar_end_assistant(void)
{
    if (streaming_index < 0)
        return;
    SidebarMessage *m = &messages[streaming_index];
    m->streaming = false;
    m->has_command = command_extract(m->text, m->command, (int)sizeof(m->command));
    streaming_index = -1;
}

bool ui_sidebar_is_streaming(void) { return streaming_index >= 0; }

int ui_sidebar_count(void) { return message_count; }

MsgRole ui_sidebar_role(int index)
{
    return (index >= 0 && index < message_count) ? messages[index].role : MSG_SYSTEM;
}

const char *ui_sidebar_text(int index)
{
    return (index >= 0 && index < message_count) ? messages[index].text : "";
}

bool ui_sidebar_draw(Font font, Rect bounds, char *out_prompt, int out_prompt_size,
                     char *out_run, int out_run_size, float scale)
{
    if (out_prompt && out_prompt_size > 0)
        out_prompt[0] = '\0';
    if (out_run && out_run_size > 0)
        out_run[0] = '\0';
    if (!sidebar_visible || bounds.w <= 0 || bounds.h <= 0)
        return false;

    float s = (scale > 0.1f) ? scale : 1.0f;   // HiDPI widget scale

    Vector2 mouse = GetMousePosition();
    bool mouse_inside = point_in_rect(mouse, bounds);

    DrawRectangle(bounds.x, bounds.y, bounds.w, bounds.h, (Color){28, 30, 34, 255});

    float font_size = 16.0f * s;
    float line_spacing = 20.0f * s;
    int margin = (int)(16 * s);
    int input_h = (int)(38 * s);
    int send_w = (int)(78 * s);
    int gap = (int)(8 * s);
    int header_h = (int)(46 * s);

    Rect history = {
        bounds.x + margin,
        bounds.y + header_h,
        bounds.w - margin * 2,
        bounds.h - header_h - input_h - margin * 2
    };
    if (history.h < (int)(40 * s))
        history.h = (int)(40 * s);

    DrawTextEx(font, "Chat", (Vector2){bounds.x + margin, bounds.y + 16*s},
               18.0f*s, 0, RAYWHITE);
    const char *subtitle = ui_sidebar_is_streaming() ? "streaming…" : "context-aware";
    DrawTextEx(font, subtitle, (Vector2){bounds.x + margin + 56*s, bounds.y + 19*s},
               13.0f*s, 0, (Color){150, 155, 165, 255});

    float text_w = (float)history.w - 20.0f*s;
    float button_h = 22.0f * s;

    // Total content height (label + optional reasoning + answer + optional Run button).
    float content_h = 0.0f;
    for (int i = 0; i < message_count; i++) {
        content_h += line_spacing;       // role label
        if (messages[i].reasoning[0])
            content_h += measure_wrapped_text(font, messages[i].reasoning, text_w,
                                              14.0f*s, 18.0f*s) + 4.0f*s;
        content_h += measure_wrapped_text(font, messages[i].text, text_w,
                                          font_size, line_spacing);
        if (messages[i].has_command)
            content_h += button_h + 6.0f*s;
        content_h += 14.0f*s;
    }

    if (mouse_inside) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            scroll_offset -= wheel * 32.0f*s;
    }

    float max_scroll = content_h - (float)history.h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (scroll_offset < 0.0f) scroll_offset = 0.0f;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;

    bool run_clicked = false;
    char run_cmd[COMMAND_SIZE] = "";

    BeginScissorMode(history.x, history.y, history.w, history.h);
    float y = (float)history.y - scroll_offset;
    for (int i = 0; i < message_count; i++) {
        Color color = role_color(messages[i].role);
        DrawTextEx(font, role_label(messages[i].role),
                   (Vector2){history.x, y}, 14.0f*s, 0, color);
        y += line_spacing;

        if (messages[i].reasoning[0]) {
            wrapped_text(font, messages[i].reasoning, (float)history.x, &y,
                         text_w, 14.0f*s, 18.0f*s, (Color){140, 145, 155, 255}, true);
            y += 4.0f*s;
        }

        wrapped_text(font, messages[i].text, (float)history.x, &y,
                     text_w, font_size, line_spacing, color, true);

        if (messages[i].has_command) {
            Rectangle btn = {(float)history.x, y, 60.0f*s, button_h};
            bool hover = CheckCollisionPointRec(mouse, btn)
                      && point_in_rect(mouse, history);
            DrawRectangleRec(btn, hover ? (Color){70, 120, 80, 255}
                                        : (Color){50, 90, 60, 255});
            DrawTextEx(font, "Run", (Vector2){btn.x + 14.0f*s, btn.y + 3.0f*s},
                       15.0f*s, 0, RAYWHITE);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                run_clicked = true;
                copy_text(run_cmd, (int)sizeof(run_cmd), messages[i].command);
            }
            y += button_h + 6.0f*s;
        }

        y += 14.0f*s;
    }
    EndScissorMode();

    // Responsive input row: show the Send button only when the sidebar is wide
    // enough for both a usable field and the button; otherwise the input takes
    // the whole row (Enter still submits). Clamp the field to a positive width
    // so GuiTextBox is never handed a degenerate (<=0) box — see ui_sidebar.c
    // history: a negative width here used to crash raygui's edit-mode loop.
    float row_w = (float)bounds.w - margin * 2;
    bool show_send = row_w >= (float)(send_w + gap) + 120.0f * s;
    float field_w = show_send ? row_w - send_w - gap : row_w;
    if (field_w < 24.0f * s) field_w = 24.0f * s;

    Rectangle input_bounds = {
        (float)bounds.x + margin,
        (float)(bounds.y + bounds.h - margin - input_h),
        field_w,
        (float)input_h
    };
    Rectangle send_bounds = {
        input_bounds.x + input_bounds.width + gap,
        input_bounds.y, (float)send_w, (float)input_h
    };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        bool clicked_input = CheckCollisionPointRec(mouse, input_bounds);
        sidebar_focused = clicked_input || (mouse_inside && sidebar_focused);
        input_editing = clicked_input || (mouse_inside && input_editing);
    }

    if (sidebar_focused && IsKeyPressed(KEY_ESCAPE)) {
        ui_sidebar_focus(false);
        if (out_run && out_run_size > 0 && run_clicked)
            copy_text(out_run, out_run_size, run_cmd);
        return false;
    }

    bool enter_pressed = sidebar_focused && IsKeyPressed(KEY_ENTER);

    // JetBrains Mono is finer than RayGUI's default font — size the widgets to
    // match the message body (16px). Save/restore so the settings modal is
    // unaffected by this frame's change.
    int prev_text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(16 * s));
    if (GuiTextBox(input_bounds, input_text, INPUT_TEXT_SIZE, input_editing)) {
        input_editing = !input_editing;
        sidebar_focused = input_editing;
    }
    bool send_clicked = show_send ? GuiButton(send_bounds, "Send") : false;
    GuiSetStyle(DEFAULT, TEXT_SIZE, prev_text_size);

    if (run_clicked && out_run && out_run_size > 0)
        copy_text(out_run, out_run_size, run_cmd);

    if (ui_sidebar_should_submit(enter_pressed, send_clicked, input_text)) {
        copy_text(out_prompt, out_prompt_size, input_text);
        input_text[0] = '\0';
        ui_sidebar_focus(true);
        scroll_offset = 1e9f;
        return true;
    }

    return false;
}
