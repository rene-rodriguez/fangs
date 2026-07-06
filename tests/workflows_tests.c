#include "workflows.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); \
    const char *e__ = (expected); \
    if (!a__ || strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e__, a__ ? a__ : "(null)"); \
        failures++; \
    } \
} while (0)

static char *temp_path(void)
{
    char templ[] = "/tmp/fangs-workflows-test-XXXXXX";
    char *dir = mkdtemp(templ);
    if (!dir) {
        fprintf(stderr, "mkdtemp: %s\n", strerror(errno));
        exit(2);
    }

    size_t len = strlen(dir) + strlen("/workflows") + 1;
    char *path = malloc(len);
    if (!path) {
        fprintf(stderr, "malloc failed\n");
        exit(2);
    }
    snprintf(path, len, "%s/workflows", dir);
    return path;
}

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

static void test_missing_file_is_empty_success(void)
{
    char *path = temp_path();
    WorkflowRegistry reg;
    workflows_init(&reg);

    EXPECT_TRUE(workflows_load_file(&reg, path));
    EXPECT_INT(workflows_count(&reg), 0);

    free(path);
}

static void test_loads_valid_workflows_and_defaults_label(void)
{
    char *path = temp_path();
    write_file(path,
        "# Global Fangs runbooks\n"
        "[workflow.test]\n"
        "label = Run Tests\n"
        "command = cmake --build build && ctest --test-dir build --output-on-failure\n"
        "detail = Build and run the full test suite\n"
        "\n"
        "[workflow.git_status]\n"
        "command = git status --short\n"
        "\n"
        "[workflow.incomplete]\n"
        "label = Missing Command\n");

    WorkflowRegistry reg;
    workflows_init(&reg);

    EXPECT_TRUE(workflows_load_file(&reg, path));
    EXPECT_INT(workflows_count(&reg), 2);

    const Workflow *first = workflows_get(&reg, 0);
    const Workflow *second = workflows_get(&reg, 1);
    EXPECT_STR(first->id, "test");
    EXPECT_STR(first->label, "Run Tests");
    EXPECT_STR(first->command, "cmake --build build && ctest --test-dir build --output-on-failure");
    EXPECT_STR(first->detail, "Build and run the full test suite");

    EXPECT_STR(second->id, "git_status");
    EXPECT_STR(second->label, "git status");
    EXPECT_STR(second->command, "git status --short");
    EXPECT_STR(second->detail, "git status --short");

    free(path);
}

static void test_multiple_files_append_in_order(void)
{
    char *a = temp_path();
    char *b = temp_path();
    write_file(a,
        "[workflow.one]\n"
        "command = echo one\n");
    write_file(b,
        "[workflow.two]\n"
        "command = echo two\n");

    WorkflowRegistry reg;
    workflows_init(&reg);

    EXPECT_TRUE(workflows_load_file(&reg, a));
    EXPECT_TRUE(workflows_load_file(&reg, b));
    EXPECT_INT(workflows_count(&reg), 2);
    EXPECT_STR(workflows_get(&reg, 0)->command, "echo one");
    EXPECT_STR(workflows_get(&reg, 1)->command, "echo two");

    free(a);
    free(b);
}

static void test_command_value_preserves_shell_comment_characters(void)
{
    char *path = temp_path();
    write_file(path,
        "[workflow.shell]\n"
        "command = printf 'a # b' ; echo done\n"
        "detail = shell punctuation # inline doc comment\n");

    WorkflowRegistry reg;
    workflows_init(&reg);

    EXPECT_TRUE(workflows_load_file(&reg, path));
    EXPECT_INT(workflows_count(&reg), 1);
    EXPECT_STR(workflows_get(&reg, 0)->command, "printf 'a # b' ; echo done");
    EXPECT_STR(workflows_get(&reg, 0)->detail, "shell punctuation");

    free(path);
}

static void test_collects_unique_workflow_variables(void)
{
    WorkflowVar vars[WORKFLOW_VAR_MAX];
    int n = workflows_collect_vars("pytest {{path=tests}} -k {{filter}} && echo {{filter}}",
                                   vars, WORKFLOW_VAR_MAX);

    EXPECT_INT(n, 2);
    EXPECT_STR(vars[0].name, "path");
    EXPECT_TRUE(vars[0].has_default);
    EXPECT_STR(vars[0].default_value, "tests");
    EXPECT_STR(vars[1].name, "filter");
    EXPECT_TRUE(!vars[1].has_default);
}

static void test_expands_variables_with_values_and_defaults(void)
{
    WorkflowValue values[] = {
        { "target", "all" },
    };
    char out[256];

    EXPECT_TRUE(workflows_expand_command("make {{target}} MODE={{mode=debug}}",
                                         values, 1, out, (int)sizeof(out)));
    EXPECT_STR(out, "make all MODE=debug");
}

static void test_expand_rejects_missing_required_value(void)
{
    WorkflowValue values[] = {
        { "other", "value" },
    };
    char out[256];

    EXPECT_TRUE(!workflows_expand_command("make {{target}}",
                                          values, 1, out, (int)sizeof(out)));
}

static void test_expand_rejects_truncation(void)
{
    WorkflowValue values[] = {
        { "target", "a-very-long-value" },
    };
    char out[8];

    EXPECT_TRUE(!workflows_expand_command("echo {{target}}",
                                          values, 1, out, (int)sizeof(out)));
}

int main(void)
{
    test_missing_file_is_empty_success();
    test_loads_valid_workflows_and_defaults_label();
    test_multiple_files_append_in_order();
    test_command_value_preserves_shell_comment_characters();
    test_collects_unique_workflow_variables();
    test_expands_variables_with_values_and_defaults();
    test_expand_rejects_missing_required_value();
    test_expand_rejects_truncation();

    if (failures != 0) {
        fprintf(stderr, "%d workflow test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
