#ifndef FANGS_UI_SIDEBAR_MODEL_H
#define FANGS_UI_SIDEBAR_MODEL_H

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

float ui_sidebar_apply_wheel_scroll(float offset,
                                    float wheel,
                                    float step,
                                    float max_scroll,
                                    bool *user_scrolled_up);
float ui_sidebar_smooth_follow_scroll(float offset,
                                      float max_scroll,
                                      bool user_scrolled_up,
                                      float frame_time);

#endif // FANGS_UI_SIDEBAR_MODEL_H
