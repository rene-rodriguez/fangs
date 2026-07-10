// crash_log — best-effort crash telemetry (Tier 1 of docs/crash-resilience-plan.md).
//
// Installs signal handlers for the common crash signals that append a
// timestamped marker plus a backtrace to a local log file, then let the
// process terminate exactly as it would have without this handler (core
// dump, exit status, etc. all unchanged). This is a diagnostic tap, not a
// recovery mechanism: it does not save the process or its child shells from
// dying — see docs/crash-resilience-plan.md for why that's a separate,
// much larger decision (Tier 3).
#ifndef FANGS_CRASH_LOG_H
#define FANGS_CRASH_LOG_H

// Installs the crash signal handlers, appending future crash reports to
// `log_path`. Safe to call once at startup; a later call replaces the
// target path. Pass NULL to disable (handlers stay installed but log
// nothing and just re-raise).
void crash_log_install(const char *log_path);

#endif // FANGS_CRASH_LOG_H
