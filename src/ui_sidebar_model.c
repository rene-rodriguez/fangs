#include "ui_sidebar_model.h"

bool ui_sidebar_should_submit(bool enter_pressed_before_textbox,
                              bool send_clicked,
                              const char *input_text)
{
    return (enter_pressed_before_textbox || send_clicked)
        && input_text
        && input_text[0] != '\0';
}

bool ui_sidebar_allows_pty_input(bool child_exited,
                                 bool settings_open,
                                 bool sidebar_layout_visible,
                                 bool sidebar_focused,
                                 bool settings_shortcut_consumed,
                                 bool sidebar_chord_consumed)
{
    return !child_exited
        && !settings_open
        && !(sidebar_layout_visible && sidebar_focused)
        && !settings_shortcut_consumed
        && !sidebar_chord_consumed;
}

static float clamp_scroll(float offset, float max_scroll)
{
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (offset < 0.0f)
        return 0.0f;
    if (offset > max_scroll)
        return max_scroll;
    return offset;
}

float ui_sidebar_apply_wheel_scroll(float offset,
                                    float wheel,
                                    float step,
                                    float max_scroll,
                                    bool *user_scrolled_up)
{
    offset = clamp_scroll(offset - wheel * step, max_scroll);

    if (user_scrolled_up) {
        if (max_scroll <= 1.0f)
            *user_scrolled_up = false;
        else
            *user_scrolled_up = offset < max_scroll - 1.0f;
    }

    return offset;
}

float ui_sidebar_smooth_follow_scroll(float offset,
                                      float max_scroll,
                                      bool user_scrolled_up,
                                      float frame_time)
{
    offset = clamp_scroll(offset, max_scroll);
    if (user_scrolled_up)
        return offset;

    float target = max_scroll < 0.0f ? 0.0f : max_scroll;
    float alpha = frame_time > 0.0f ? frame_time * 12.0f : 1.0f;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    float next = offset + (target - offset) * alpha;
    if (target - next < 0.5f && target - next > -0.5f)
        next = target;
    return clamp_scroll(next, max_scroll);
}
