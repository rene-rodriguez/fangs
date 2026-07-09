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

// One-time startup hook: on macOS, requests Notification Center authorization
// and registers the delegate that activates Fangs when an agent-ring banner
// is clicked. No-op (and safe to call unconditionally) on other platforms,
// and when the process has no bundle identity (e.g. a raw binary launch).
void desktop_notify_startup(void);

#endif
