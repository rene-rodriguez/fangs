#ifndef FANGS_UI_SETTINGS_H
#define FANGS_UI_SETTINGS_H

#include <stdbool.h>

#include "config.h"

bool ui_settings_open(void);
void ui_settings_toggle(void);
// scale: HiDPI content scale; all widget dimensions are multiplied by it so the
// modal matches the (scaled) terminal font.
void ui_settings_draw(AppConfig *cfg, bool *out_saved, float scale);

#endif // FANGS_UI_SETTINGS_H
