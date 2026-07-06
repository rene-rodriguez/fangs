// desktop_notify — macOS desktop notification helper for agent rings.
//
// Compiles to a no-op on non-macOS platforms.
#ifndef FANGS_DESKTOP_NOTIFY_H
#define FANGS_DESKTOP_NOTIFY_H

#include <stdbool.h>

// Escape a string for safe embedding in AppleScript string literals
// (backslash and double-quote are prefixed with backslash).
// Returns false if the output would be truncated.
bool desktop_notify_escape_applescript(const char *input, char *out, int out_size);

// Fire a macOS notification for an agent BEL / OSC ring.
// Workspace is the display name for the subtitle, message is the OSC text
// (defaults to "needs attention" when empty or NULL).
// Returns true on macOS when osascript was launched successfully,
// false on other platforms or if osascript is unavailable.
bool desktop_notify_agent_ring(const char *workspace, const char *message);

#endif
