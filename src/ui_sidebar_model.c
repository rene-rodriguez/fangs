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
