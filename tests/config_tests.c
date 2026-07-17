#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_INT(actual, expected) do { \
    int a__ = (actual); \
    int e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); \
    const char *e__ = (expected); \
    if (strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "fopen(%s): %s\n", path, strerror(errno));
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static char *temp_config_path(void)
{
    char templ[] = "/tmp/fangs-config-test-XXXXXX";
    char *dir = mkdtemp(templ);
    if (!dir) {
        fprintf(stderr, "mkdtemp: %s\n", strerror(errno));
        exit(2);
    }

    size_t len = strlen(dir) + strlen("/config") + 1;
    char *path = malloc(len);
    if (!path) {
        fprintf(stderr, "malloc failed\n");
        exit(2);
    }
    snprintf(path, len, "%s/config", dir);
    return path;
}

static void test_defaults(void)
{
    AppConfig cfg;
    memset(&cfg, 0xff, sizeof(cfg));

    config_defaults(&cfg);

    EXPECT_STR(cfg.font_family, "JetBrainsMono Nerd Font");
    EXPECT_INT(cfg.font_size, 16);
    EXPECT_STR(cfg.theme, "dark");
    EXPECT_INT(cfg.scrollback, 1000);
    EXPECT_TRUE(cfg.kitty_images);
    EXPECT_INT(cfg.kitty_image_storage_mb, 64);
    EXPECT_INT(cfg.workspace_rail_width, 260);
    EXPECT_STR(cfg.provider, "openai");
    EXPECT_STR(cfg.endpoint, "https://api.openai.com/v1");
    EXPECT_STR(cfg.model, "gpt-4o-mini");
    EXPECT_STR(cfg.api_key, "");
    EXPECT_TRUE(cfg.stream);
    EXPECT_INT(cfg.max_tokens, 1024);
    EXPECT_INT(cfg.ollama_num_ctx, 0);
    EXPECT_INT(cfg.ollama_num_gpu, -1);
    EXPECT_INT(cfg.ollama_num_thread, 0);
    EXPECT_INT(cfg.ollama_num_batch, 0);
    EXPECT_INT(cfg.window_width, 800);
    EXPECT_INT(cfg.window_height, 600);
    EXPECT_INT(cfg.window_x, -1);
    EXPECT_INT(cfg.window_y, -1);
}

static void test_load_missing_file_creates_defaults(void)
{
    char *path = temp_config_path();
    AppConfig cfg;

    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_INT(access(path, F_OK), 0);
    EXPECT_INT(cfg.font_size, 16);
    EXPECT_STR(cfg.model, "gpt-4o-mini");

    struct stat st;
    EXPECT_INT(stat(path, &st), 0);
    EXPECT_INT(st.st_mode & 0777, 0600);

    free(path);
}

static void test_load_parses_ini_sections(void)
{
    char *path = temp_config_path();
    write_file(path,
        "  # comment before sections\n"
        "[terminal]\n"
        "font_family = Custom Mono\n"
        "font_size   = 21\n"
        "theme = light ; inline comment\n"
        "scrollback=5000\n"
        "kitty_images = false\n"
        "kitty_image_storage_mb = 128\n"
        "\n"
        "[window]\n"
        "width = 1440\n"
        "height = 900\n"
        "x = 120\n"
        "y = 80\n"
        "\n"
        "[ai]\n"
        "provider = custom\n"
        "endpoint = http://localhost:11434/v1/chat/completions\n"
        "model = llama3.2\n"
        "api_key = file-secret\n"
        "stream = false\n"
        "max_tokens = 256\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));

    EXPECT_STR(cfg.font_family, "Custom Mono");
    EXPECT_INT(cfg.font_size, 21);
    EXPECT_STR(cfg.theme, "light");
    EXPECT_INT(cfg.scrollback, 5000);
    EXPECT_TRUE(!cfg.kitty_images);
    EXPECT_INT(cfg.kitty_image_storage_mb, 128);
    EXPECT_INT(cfg.window_width, 1440);
    EXPECT_INT(cfg.window_height, 900);
    EXPECT_INT(cfg.window_x, 120);
    EXPECT_INT(cfg.window_y, 80);
    EXPECT_STR(cfg.provider, "custom");
    EXPECT_STR(cfg.endpoint, "http://localhost:11434/v1/chat/completions");
    EXPECT_STR(cfg.model, "llama3.2");
    EXPECT_STR(cfg.api_key, "file-secret");
    EXPECT_TRUE(!cfg.stream);
    EXPECT_INT(cfg.max_tokens, 256);

    free(path);
}

static void test_load_clamps_kitty_image_storage(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[terminal]\n"
        "kitty_image_storage_mb = 2048\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));

    EXPECT_INT(cfg.kitty_image_storage_mb, 1024);

    write_file(path,
        "[terminal]\n"
        "kitty_image_storage_mb = -4\n");

    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_INT(cfg.kitty_image_storage_mb, 0);

    free(path);
}

static void test_load_clamps_workspace_rail_width(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[ui]\n"
        "workspace_rail_width = 9999\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_INT(cfg.workspace_rail_width, WORKSPACE_RAIL_MAX_WIDTH);

    write_file(path,
        "[ui]\n"
        "workspace_rail_width = 10\n");

    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_INT(cfg.workspace_rail_width, WORKSPACE_RAIL_MIN_WIDTH);

    free(path);
}

static void test_workspace_rail_width_parse_and_round_trip(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[ui]\n"
        "workspace_rail_width = 320\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_INT(cfg.workspace_rail_width, 320);

    cfg.workspace_rail_width = 220;
    EXPECT_TRUE(config_save(&cfg, path));

    AppConfig loaded;
    EXPECT_TRUE(config_load(&loaded, path));
    EXPECT_INT(loaded.workspace_rail_width, 220);

    free(path);
}

static void test_save_round_trips_app_config(void)
{
    char *path = temp_config_path();
    AppConfig cfg;
    config_defaults(&cfg);

    snprintf(cfg.font_family, sizeof(cfg.font_family), "%s", "Saved Mono");
    cfg.font_size = 18;
    snprintf(cfg.theme, sizeof(cfg.theme), "%s", "light");
    cfg.scrollback = 3000;
    cfg.kitty_images = false;
    cfg.kitty_image_storage_mb = 96;
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.window_x = 42;
    cfg.window_y = 84;
    snprintf(cfg.provider, sizeof(cfg.provider), "%s", "ollama");
    snprintf(cfg.endpoint, sizeof(cfg.endpoint), "%s", "http://localhost:11434/v1/chat/completions");
    snprintf(cfg.model, sizeof(cfg.model), "%s", "qwen2.5-coder");
    snprintf(cfg.api_key, sizeof(cfg.api_key), "%s", "saved-secret");
    cfg.stream = false;
    cfg.max_tokens = 4096;
    cfg.ollama_num_ctx = 8192;
    cfg.ollama_num_gpu = 0;
    cfg.ollama_num_thread = 8;
    cfg.ollama_num_batch = 512;

    EXPECT_TRUE(config_save(&cfg, path));

    AppConfig loaded;
    EXPECT_TRUE(config_load(&loaded, path));
    EXPECT_STR(loaded.font_family, "Saved Mono");
    EXPECT_INT(loaded.font_size, 18);
    EXPECT_STR(loaded.theme, "light");
    EXPECT_INT(loaded.scrollback, 3000);
    EXPECT_TRUE(!loaded.kitty_images);
    EXPECT_INT(loaded.kitty_image_storage_mb, 96);
    EXPECT_INT(loaded.window_width, 1280);
    EXPECT_INT(loaded.window_height, 720);
    EXPECT_INT(loaded.window_x, 42);
    EXPECT_INT(loaded.window_y, 84);
    EXPECT_STR(loaded.provider, "ollama");
    EXPECT_STR(loaded.endpoint, "http://localhost:11434/v1/chat/completions");
    EXPECT_STR(loaded.model, "qwen2.5-coder");
    EXPECT_STR(loaded.api_key, "saved-secret");
    EXPECT_TRUE(!loaded.stream);
    EXPECT_INT(loaded.max_tokens, 4096);
    EXPECT_INT(loaded.ollama_num_ctx, 8192);
    EXPECT_INT(loaded.ollama_num_gpu, 0);
    EXPECT_INT(loaded.ollama_num_thread, 8);
    EXPECT_INT(loaded.ollama_num_batch, 512);

    struct stat st;
    EXPECT_INT(stat(path, &st), 0);
    EXPECT_INT(st.st_mode & 0777, 0600);

    free(path);
}

static void test_remote_api_defaults_false(void)
{
    AppConfig cfg;
    config_defaults(&cfg);
    EXPECT_TRUE(!cfg.remote_api);
    EXPECT_TRUE(!cfg.remote_api_send);
}

static void test_remote_api_parse_and_round_trip(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[remote]\n"
        "remote_api = true\n"
        "remote_api_send = true\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_TRUE(cfg.remote_api);
    EXPECT_TRUE(cfg.remote_api_send);

    // Modify and save
    cfg.remote_api = false;
    cfg.remote_api_send = true;
    EXPECT_TRUE(config_save(&cfg, path));

    // Re-load and verify
    AppConfig loaded;
    EXPECT_TRUE(config_load(&loaded, path));
    EXPECT_TRUE(!loaded.remote_api);
    EXPECT_TRUE(loaded.remote_api_send);

    free(path);
}

static void test_workspace_ops_defaults(void)
{
    AppConfig cfg;
    config_defaults(&cfg);
    EXPECT_STR(cfg.workspace_command, "");
    EXPECT_TRUE(cfg.restore_session);
}

static void test_workspace_ops_parse_and_round_trip(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[workspace]\n"
        "workspace_command = claude\n"
        "restore_session = false\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_STR(cfg.workspace_command, "claude");
    EXPECT_TRUE(!cfg.restore_session);

    // Modify and save
    snprintf(cfg.workspace_command, sizeof(cfg.workspace_command), "claude --model sonnet");
    cfg.restore_session = true;
    EXPECT_TRUE(config_save(&cfg, path));

    // Re-load and verify
    AppConfig loaded;
    EXPECT_TRUE(config_load(&loaded, path));
    EXPECT_STR(loaded.workspace_command, "claude --model sonnet");
    EXPECT_TRUE(loaded.restore_session);

    free(path);
}

static void test_tmux_wrap_defaults_false(void)
{
    AppConfig cfg;
    config_defaults(&cfg);
    EXPECT_TRUE(!cfg.tmux_wrap);
}

static void test_tmux_wrap_parse_and_round_trip(void)
{
    char *path = temp_config_path();
    write_file(path,
        "[terminal]\n"
        "tmux_wrap = true\n");

    AppConfig cfg;
    EXPECT_TRUE(config_load(&cfg, path));
    EXPECT_TRUE(cfg.tmux_wrap);

    cfg.tmux_wrap = false;
    EXPECT_TRUE(config_save(&cfg, path));

    AppConfig loaded;
    EXPECT_TRUE(config_load(&loaded, path));
    EXPECT_TRUE(!loaded.tmux_wrap);

    free(path);
}

int main(void)
{
    test_defaults();
    test_load_missing_file_creates_defaults();
    test_load_parses_ini_sections();
    test_load_clamps_kitty_image_storage();
    test_load_clamps_workspace_rail_width();
    test_workspace_rail_width_parse_and_round_trip();
    test_save_round_trips_app_config();
    test_remote_api_defaults_false();
    test_remote_api_parse_and_round_trip();
    test_workspace_ops_defaults();
    test_workspace_ops_parse_and_round_trip();
    test_tmux_wrap_defaults_false();
    test_tmux_wrap_parse_and_round_trip();

    if (failures != 0) {
        fprintf(stderr, "%d config test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
