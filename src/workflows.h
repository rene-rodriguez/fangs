// workflows — Local runbook registry for command-palette entries.
#ifndef FANGS_WORKFLOWS_H
#define FANGS_WORKFLOWS_H

#include <stdbool.h>

#define WORKFLOW_MAX_ITEMS   64
#define WORKFLOW_ID_MAX      64
#define WORKFLOW_LABEL_MAX   96
#define WORKFLOW_DETAIL_MAX  192
#define WORKFLOW_COMMAND_MAX 1024
#define WORKFLOW_VAR_MAX     8
#define WORKFLOW_VAR_NAME_MAX 64
#define WORKFLOW_VAR_VALUE_MAX 256

typedef struct {
    char id[WORKFLOW_ID_MAX];
    char label[WORKFLOW_LABEL_MAX];
    char command[WORKFLOW_COMMAND_MAX];
    char detail[WORKFLOW_DETAIL_MAX];
} Workflow;

typedef struct {
    Workflow items[WORKFLOW_MAX_ITEMS];
    int count;
} WorkflowRegistry;

typedef struct {
    char name[WORKFLOW_VAR_NAME_MAX];
    char default_value[WORKFLOW_VAR_VALUE_MAX];
    bool has_default;
} WorkflowVar;

typedef struct {
    char name[WORKFLOW_VAR_NAME_MAX];
    char value[WORKFLOW_VAR_VALUE_MAX];
} WorkflowValue;

void workflows_init(WorkflowRegistry *reg);
int workflows_count(const WorkflowRegistry *reg);
const Workflow *workflows_get(const WorkflowRegistry *reg, int index);

bool workflows_add(WorkflowRegistry *reg, const char *id, const char *label,
                   const char *command, const char *detail);
bool workflows_load_file(WorkflowRegistry *reg, const char *path);

int workflows_collect_vars(const char *command, WorkflowVar *vars, int max_vars);
bool workflows_expand_command(const char *command,
                              const WorkflowValue *values, int value_count,
                              char *out, int out_size);
bool workflows_append_saved_command(const char *path, const char *command,
                                    char *out_id, int out_id_size);

#endif // FANGS_WORKFLOWS_H
