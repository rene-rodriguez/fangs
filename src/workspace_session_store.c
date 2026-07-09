#include "workspace_session_store.h"

#include "cJSON.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

bool workspace_session_default_path(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0)
        return false;
    const char *app_dir = config_default_app_dir();
    int written = snprintf(buf, (size_t)buf_size, "%s/session.json", app_dir);
    return written > 0 && written < buf_size;
}

static char *read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || size > (16 * 1024 * 1024)) {  // sanity cap: 16 MiB
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_n] = '\0';
    return buf;
}

bool workspace_session_load(const char *path, WorkspaceSessionState *out)
{
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    if (!path)
        return false;

    char *text = read_whole_file(path);
    if (!text)
        return false;

    cJSON *json = cJSON_Parse(text);
    free(text);
    if (!json)
        return false;

    bool ok = false;
    cJSON *tabs = cJSON_GetObjectItem(json, "tabs");
    if (tabs && cJSON_IsArray(tabs)) {
        int i = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, tabs) {
            if (i >= WORKSPACE_SESSION_MAX_TABS)
                break;
            if (!cJSON_IsObject(item))
                continue;
            cJSON *cwd = cJSON_GetObjectItem(item, "cwd");
            cJSON *name = cJSON_GetObjectItem(item, "name");
            if (!cwd || !cJSON_IsString(cwd) || cwd->valuestring[0] == '\0')
                continue;
            snprintf(out->tabs[i].cwd, sizeof(out->tabs[i].cwd), "%s", cwd->valuestring);
            if (name && cJSON_IsString(name))
                snprintf(out->tabs[i].name, sizeof(out->tabs[i].name), "%s", name->valuestring);
            i++;
        }
        out->count = i;
        ok = true;
    }

    cJSON *active = cJSON_GetObjectItem(json, "active");
    if (active && cJSON_IsNumber(active))
        out->active = (int)active->valuedouble;
    if (out->active < 0 || out->active >= out->count)
        out->active = 0;

    cJSON_Delete(json);
    return ok && out->count > 0;
}

bool workspace_session_save(const char *path, const WorkspaceSessionState *state)
{
    if (!path || !state)
        return false;

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return false;

    int active = state->active;
    if (active < 0 || active >= state->count)
        active = 0;
    cJSON_AddNumberToObject(root, "active", active);

    cJSON *tabs = cJSON_CreateArray();
    int count = state->count;
    if (count > WORKSPACE_SESSION_MAX_TABS)
        count = WORKSPACE_SESSION_MAX_TABS;
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "cwd", state->tabs[i].cwd);
        cJSON_AddStringToObject(item, "name", state->tabs[i].name);
        cJSON_AddItemToArray(tabs, item);
    }
    cJSON_AddItemToObject(root, "tabs", tabs);

    char *printed = cJSON_Print(root);
    cJSON_Delete(root);
    if (!printed)
        return false;

    char tmp_path[4160];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        free(printed);
        return false;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        free(printed);
        return false;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        free(printed);
        return false;
    }

    size_t len = strlen(printed);
    size_t written = fwrite(printed, 1, len, f);
    bool write_ok = (written == len) && !ferror(f);
    fclose(f);
    free(printed);

    if (!write_ok) {
        unlink(tmp_path);
        return false;
    }

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return false;
    }

    return true;
}
