// Pure text helpers for building AI context — no terminal/ghostty dependency so
// they unit-test cleanly. context.c composes these over term_engine_dump_text().
//
// redact_secrets() is a SECURITY boundary: terminal output is about to be sent
// to a third-party model, so known secret shapes (API keys, tokens, Bearer
// headers, key=secret assignments) are replaced with "<redacted>" first.
#ifndef FANGS_REDACT_H
#define FANGS_REDACT_H

// Return the last `max_lines` lines of `text`, further capped to at most
// `max_bytes` bytes (aligned to a line boundary). malloc'd; caller frees.
// max_lines/max_bytes <= 0 disable that limit.
char *text_tail(const char *text, int max_lines, int max_bytes);

// Return a redacted copy of `text` with secrets replaced by "<redacted>".
// malloc'd; caller frees.
char *redact_secrets(const char *text);

#endif // FANGS_REDACT_H
