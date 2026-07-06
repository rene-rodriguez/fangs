#ifndef FANGS_WORKSPACE_INFO_H
#define FANGS_WORKSPACE_INFO_H

#include <stdbool.h>

void workspace_cwd_label(const char *cwd, const char *home, char *out, int out_size);
bool workspace_git_branch(const char *cwd, char *out, int out_size);

#endif
