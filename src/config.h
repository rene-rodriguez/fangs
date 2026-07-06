#ifndef FANGS_CONFIG_H
#define FANGS_CONFIG_H

#include <stdbool.h>

typedef struct {
    char font_family[128];
    int font_size;
    char theme[32];
    int scrollback;
    bool kitty_images;
    int kitty_image_storage_mb;

    char provider[32];
    char endpoint[256];
    char model[128];
    char api_key[256];
    bool stream;
    int max_tokens;

    // E4: cursor configuration
    int cursor_style;    // 0=block, 1=bar, 2=underline (default 0)
    bool cursor_blink;   // default true

    // E7: persisted window geometry. x/y of -1 means "let the OS place it".
    int window_width;
    int window_height;
    int window_x;
    int window_y;

    // Workspace rail (left-side vertical tabs and panes).
    bool workspace_rail;
} AppConfig;

void config_defaults(AppConfig *c);
bool config_load(AppConfig *c, const char *path);
bool config_save(const AppConfig *c, const char *path);
const char *config_default_path(void);

#endif // FANGS_CONFIG_H
