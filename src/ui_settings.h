#ifndef FANGS_UI_SETTINGS_H
#define FANGS_UI_SETTINGS_H

#include <stdbool.h>

#include "config.h"

bool ui_settings_open(void);
// Returns the opening theme when closing a previewed modal, otherwise NULL.
const char *ui_settings_toggle(void);
// scale: HiDPI content scale; all widget dimensions are multiplied by it so the
// modal matches the (scaled) terminal font.
// out_preview_theme is set only when a theme should be applied immediately.
// The caller owns applying the preview; cfg is updated only when out_saved is true.
void ui_settings_draw(AppConfig *cfg, bool *out_saved,
                      const char **out_preview_theme, float scale);

#endif // FANGS_UI_SETTINGS_H
