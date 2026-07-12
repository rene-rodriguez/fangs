#ifndef FANGS_CONFIG_H
#define FANGS_CONFIG_H

#include <stdbool.h>

// Workspace rail width bounds (px) — shared by config parsing (clamps on
// load/save) and the rail's drag-to-resize handle in main.c.
#define WORKSPACE_RAIL_MIN_WIDTH 180
#define WORKSPACE_RAIL_MAX_WIDTH 480

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

    // Ollama-native runtime knobs (speed tuning; only applied when
    // provider=="ollama"). Same sentinel convention as window_x/window_y
    // below: unset means "don't send it, let Ollama use its own default".
    // num_gpu uses -1 as its sentinel since 0 is a meaningful choice (force
    // CPU-only inference), unlike the other three fields where 0 means unset.
    int ollama_num_ctx;
    int ollama_num_gpu;
    int ollama_num_thread;
    int ollama_num_batch;

    // E4: cursor configuration
    int cursor_style;    // 0=block, 1=bar, 2=underline (default 0)
    bool cursor_blink;   // default true

    // Tier 2 of docs/crash-resilience-plan.md: wrap new shells in a named
    // tmux session so a Fangs crash doesn't kill them too. Off by default.
    bool tmux_wrap;

    // E7: persisted window geometry. x/y of -1 means "let the OS place it".
    int window_width;
    int window_height;
    int window_x;
    int window_y;

    // Workspace rail (left-side vertical tabs and panes).
    bool workspace_rail;
    int workspace_rail_width;  // px; user-resizable via the rail's edge handle

    // Remote control API (Unix socket JSON protocol).
    bool remote_api;       // enables the socket + read-only/benign commands
    bool remote_api_send;  // additionally enables send and new --run

    // Workspace ops: auto-launch + session restore.
    char workspace_command[512];  // typed into new worktree workspaces; "" = disabled
    bool restore_session;         // reopen last session's tabs on launch
} AppConfig;

void config_defaults(AppConfig *c);
bool config_load(AppConfig *c, const char *path);
bool config_save(const AppConfig *c, const char *path);
const char *config_default_path(void);
const char *config_default_app_dir(void);

#endif // FANGS_CONFIG_H
