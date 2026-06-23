#ifndef NOVA_UI_SIDEBAR_MODEL_H
#define NOVA_UI_SIDEBAR_MODEL_H

#include <stdbool.h>

bool ui_sidebar_should_submit(bool enter_pressed_before_textbox,
                              bool send_clicked,
                              const char *input_text);
bool ui_sidebar_allows_pty_input(bool child_exited,
                                 bool settings_open,
                                 bool sidebar_layout_visible,
                                 bool sidebar_focused,
                                 bool settings_shortcut_consumed,
                                 bool sidebar_chord_consumed);

#endif // NOVA_UI_SIDEBAR_MODEL_H
