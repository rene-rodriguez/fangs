#include "workflows.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void copy_string(char *dst, int dst_size, const char *src)
{
    if (dst_size <= 0)
        return;
    snprintf(dst, (size_t)dst_size, "%s", src ? src : "");
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return s;
}

static void strip_inline_comment(char *s)
{
    for (char *p = s; *p; p++) {
        if ((*p == ';' || *p == '#') && (p == s || isspace((unsigned char)p[-1]))) {
            *p = '\0';
            return;
        }
    }
}

static void label_from_id(char *dst, int dst_size, const char *id)
{
    int n = 0;
    for (int i = 0; id && id[i] && n < dst_size - 1; i++) {
        char c = id[i];
        dst[n++] = (c == '_' || c == '-') ? ' ' : c;
    }
    dst[n] = '\0';
}

static bool valid_var_name(const char *name)
{
    if (!name || !name[0])
        return false;
    for (int i = 0; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return true;
}

static bool parse_placeholder_at(const char *p,
                                 char *name, int name_size,
                                 char *default_value, int default_size,
                                 bool *has_default,
                                 const char **after)
{
    if (!p || p[0] != '{' || p[1] != '{')
        return false;

    const char *close = strstr(p + 2, "}}");
    if (!close)
        return false;

    char inner[WORKFLOW_VAR_NAME_MAX + WORKFLOW_VAR_VALUE_MAX + 8];
    size_t n = (size_t)(close - (p + 2));
    if (n >= sizeof(inner))
        return false;
    memcpy(inner, p + 2, n);
    inner[n] = '\0';

    char *body = trim(inner);
    char *eq = strchr(body, '=');
    char *def = NULL;
    if (eq) {
        *eq = '\0';
        def = trim(eq + 1);
    }

    char *var_name = trim(body);
    if (!valid_var_name(var_name))
        return false;

    copy_string(name, name_size, var_name);
    if (eq) {
        copy_string(default_value, default_size, def);
        *has_default = true;
    } else {
        copy_string(default_value, default_size, "");
        *has_default = false;
    }
    if (after)
        *after = close + 2;
    return true;
}

static int find_var(const WorkflowVar *vars, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(vars[i].name, name) == 0)
            return i;
    }
    return -1;
}

static const char *find_value(const WorkflowValue *values, int count,
                              const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(values[i].name, name) == 0 && values[i].value[0])
            return values[i].value;
    }
    return NULL;
}

static bool append_text(char *out, int out_size, int *pos,
                        const char *text, int len)
{
    if (!out || out_size <= 0 || !pos || !text || len < 0)
        return false;
    if (*pos + len >= out_size)
        return false;
    memcpy(out + *pos, text, (size_t)len);
    *pos += len;
    out[*pos] = '\0';
    return true;
}

void workflows_init(WorkflowRegistry *reg)
{
    if (!reg)
        return;
    memset(reg, 0, sizeof(*reg));
}

int workflows_count(const WorkflowRegistry *reg)
{
    return reg ? reg->count : 0;
}

const Workflow *workflows_get(const WorkflowRegistry *reg, int index)
{
    if (!reg || index < 0 || index >= reg->count)
        return NULL;
    return &reg->items[index];
}

bool workflows_add(WorkflowRegistry *reg, const char *id, const char *label,
                   const char *command, const char *detail)
{
    if (!reg || reg->count >= WORKFLOW_MAX_ITEMS || !command || command[0] == '\0')
        return false;

    Workflow *w = &reg->items[reg->count];
    memset(w, 0, sizeof(*w));
    copy_string(w->id, sizeof(w->id), id && id[0] ? id : "workflow");
    if (label && label[0])
        copy_string(w->label, sizeof(w->label), label);
    else
        label_from_id(w->label, sizeof(w->label), w->id);
    copy_string(w->command, sizeof(w->command), command);
    copy_string(w->detail, sizeof(w->detail),
                detail && detail[0] ? detail : command);
    reg->count++;
    return true;
}

typedef struct {
    bool active;
    char id[WORKFLOW_ID_MAX];
    char label[WORKFLOW_LABEL_MAX];
    char command[WORKFLOW_COMMAND_MAX];
    char detail[WORKFLOW_DETAIL_MAX];
} PendingWorkflow;

static void pending_clear(PendingWorkflow *p)
{
    memset(p, 0, sizeof(*p));
}

static void pending_commit(WorkflowRegistry *reg, PendingWorkflow *p)
{
    if (!p->active)
        return;
    workflows_add(reg, p->id, p->label, p->command, p->detail);
    pending_clear(p);
}

bool workflows_load_file(WorkflowRegistry *reg, const char *path)
{
    if (!reg || !path || path[0] == '\0')
        return false;

    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            return true;
        return false;
    }

    PendingWorkflow pending;
    pending_clear(&pending);

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '\0' || *s == ';' || *s == '#')
            continue;

        if (*s == '[') {
            pending_commit(reg, &pending);
            char *close = strchr(s, ']');
            if (!close)
                continue;
            *close = '\0';
            char *section = trim(s + 1);
            const char prefix[] = "workflow.";
            if (strncmp(section, prefix, sizeof(prefix) - 1) == 0
                && section[sizeof(prefix) - 1] != '\0') {
                pending.active = true;
                copy_string(pending.id, sizeof(pending.id),
                            section + sizeof(prefix) - 1);
            }
            continue;
        }

        if (!pending.active)
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);

        if (strcmp(key, "command") != 0) {
            strip_inline_comment(value);
            value = trim(value);
        }

        if (strcmp(key, "label") == 0)
            copy_string(pending.label, sizeof(pending.label), value);
        else if (strcmp(key, "command") == 0)
            copy_string(pending.command, sizeof(pending.command), value);
        else if (strcmp(key, "detail") == 0)
            copy_string(pending.detail, sizeof(pending.detail), value);
    }
    pending_commit(reg, &pending);

    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

int workflows_collect_vars(const char *command, WorkflowVar *vars, int max_vars)
{
    if (!command || !vars || max_vars <= 0)
        return 0;

    int count = 0;
    const char *p = command;
    while (*p) {
        const char *open = strstr(p, "{{");
        if (!open)
            break;

        char name[WORKFLOW_VAR_NAME_MAX];
        char default_value[WORKFLOW_VAR_VALUE_MAX];
        bool has_default = false;
        const char *after = NULL;
        if (!parse_placeholder_at(open, name, sizeof(name),
                                  default_value, sizeof(default_value),
                                  &has_default, &after)) {
            p = open + 2;
            continue;
        }

        int idx = find_var(vars, count, name);
        if (idx < 0 && count < max_vars) {
            copy_string(vars[count].name, sizeof(vars[count].name), name);
            copy_string(vars[count].default_value,
                        sizeof(vars[count].default_value), default_value);
            vars[count].has_default = has_default;
            count++;
        } else if (idx >= 0 && !vars[idx].has_default && has_default) {
            copy_string(vars[idx].default_value,
                        sizeof(vars[idx].default_value), default_value);
            vars[idx].has_default = true;
        }

        p = after ? after : open + 2;
    }

    return count;
}

bool workflows_expand_command(const char *command,
                              const WorkflowValue *values, int value_count,
                              char *out, int out_size)
{
    if (!command || !out || out_size <= 0)
        return false;

    int pos = 0;
    out[0] = '\0';
    const char *p = command;
    while (*p) {
        const char *open = strstr(p, "{{");
        if (!open)
            return append_text(out, out_size, &pos, p, (int)strlen(p));

        if (!append_text(out, out_size, &pos, p, (int)(open - p)))
            return false;

        char name[WORKFLOW_VAR_NAME_MAX];
        char default_value[WORKFLOW_VAR_VALUE_MAX];
        bool has_default = false;
        const char *after = NULL;
        if (!parse_placeholder_at(open, name, sizeof(name),
                                  default_value, sizeof(default_value),
                                  &has_default, &after)) {
            if (!append_text(out, out_size, &pos, open, 2))
                return false;
            p = open + 2;
            continue;
        }

        const char *value = find_value(values, value_count, name);
        if (!value) {
            if (!has_default)
                return false;
            value = default_value;
        }
        if (!append_text(out, out_size, &pos, value, (int)strlen(value)))
            return false;
        p = after;
    }

    return true;
}
