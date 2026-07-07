#include "remote_proto.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool remote_proto_parse(const char *line, RemoteRequest *out,
                        char *err, int err_size)
{
    memset(out, 0, sizeof(*out));
    out->index = -1;
    out->pane = -1;
    out->lines = -1;

    size_t len = strlen(line);
    if (len > REMOTE_LINE_MAX) {
        if (err) snprintf(err, err_size, "line exceeds %d bytes", REMOTE_LINE_MAX);
        return false;
    }

    cJSON *json = cJSON_Parse(line);
    if (!json) {
        if (err) {
            const char *e = cJSON_GetErrorPtr();
            snprintf(err, err_size, "JSON parse error: %s", e ? e : "unknown");
        }
        return false;
    }

    // Extract id (best-effort, may be 0)
    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    if (id_item && cJSON_IsNumber(id_item))
        out->id = (long)id_item->valuedouble;

    // Extract cmd (required)
    cJSON *cmd_item = cJSON_GetObjectItem(json, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        if (err) snprintf(err, err_size, "missing 'cmd' field");
        cJSON_Delete(json);
        return false;
    }

    const char *cmd = cmd_item->valuestring;
    if (strcmp(cmd, "list") == 0) {
        out->cmd = REMOTE_CMD_LIST;
    } else if (strcmp(cmd, "new") == 0) {
        out->cmd = REMOTE_CMD_NEW;
    } else if (strcmp(cmd, "focus") == 0) {
        out->cmd = REMOTE_CMD_FOCUS;
    } else if (strcmp(cmd, "rename") == 0) {
        out->cmd = REMOTE_CMD_RENAME;
    } else if (strcmp(cmd, "send") == 0) {
        out->cmd = REMOTE_CMD_SEND;
    } else if (strcmp(cmd, "read") == 0) {
        out->cmd = REMOTE_CMD_READ;
    } else if (strcmp(cmd, "ring") == 0) {
        out->cmd = REMOTE_CMD_RING;
    } else {
        if (err) snprintf(err, err_size, "unknown cmd: '%s'", cmd);
        cJSON_Delete(json);
        return false;
    }

    // Extract optional fields
    cJSON *index_item = cJSON_GetObjectItem(json, "index");
    if (index_item && cJSON_IsNumber(index_item))
        out->index = index_item->valueint;

    cJSON *pane_item = cJSON_GetObjectItem(json, "pane");
    if (pane_item && cJSON_IsNumber(pane_item))
        out->pane = pane_item->valueint;

    cJSON *lines_item = cJSON_GetObjectItem(json, "lines");
    if (lines_item && cJSON_IsNumber(lines_item))
        out->lines = lines_item->valueint;

    cJSON *worktree_item = cJSON_GetObjectItem(json, "worktree");
    if (worktree_item && cJSON_IsBool(worktree_item))
        out->worktree = worktree_item->valueint != 0;

    // String fields
    cJSON *str_item;

    str_item = cJSON_GetObjectItem(json, "cwd");
    if (str_item && cJSON_IsString(str_item))
        snprintf(out->cwd, sizeof(out->cwd), "%s", str_item->valuestring);

    str_item = cJSON_GetObjectItem(json, "name");
    if (str_item && cJSON_IsString(str_item))
        snprintf(out->name, sizeof(out->name), "%s", str_item->valuestring);

    str_item = cJSON_GetObjectItem(json, "run");
    if (str_item && cJSON_IsString(str_item))
        snprintf(out->run, sizeof(out->run), "%s", str_item->valuestring);

    str_item = cJSON_GetObjectItem(json, "text");
    if (str_item && cJSON_IsString(str_item))
        snprintf(out->text, sizeof(out->text), "%s", str_item->valuestring);

    str_item = cJSON_GetObjectItem(json, "message");
    if (str_item && cJSON_IsString(str_item))
        snprintf(out->message, sizeof(out->message), "%s", str_item->valuestring);

    cJSON_Delete(json);
    return true;
}

// ---- Response builders ----

static char *build_json(cJSON *obj)
{
    char *printed = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return printed;
}

char *remote_proto_error(long id, const char *msg)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)id);
    cJSON_AddBoolToObject(obj, "ok", 0);
    cJSON_AddStringToObject(obj, "error", msg);
    return build_json(obj);
}

char *remote_proto_ok(long id)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)id);
    cJSON_AddBoolToObject(obj, "ok", 1);
    return build_json(obj);
}

char *remote_proto_ok_obj(long id, void *fields)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)id);
    cJSON_AddBoolToObject(obj, "ok", 1);
    if (fields) {
        cJSON *f = (cJSON *)fields;
        cJSON *child = f->child;
        while (child) {
            cJSON_AddItemToObject(obj, child->string, cJSON_Duplicate(child, 1));
            child = child->next;
        }
    }
    return build_json(obj);
}
