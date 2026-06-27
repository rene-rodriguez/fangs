// ai_block — Format a command block's command+output+exit_code into a context
// body the sidebar prepends to the next AI send. Pure; no ghostty dependency.
#ifndef FANGS_AI_BLOCK_H
#define FANGS_AI_BLOCK_H

// Build a context string describing a command block: the command, its output,
// and its exit status. Writes into `out` (up to `cap` bytes, NUL-terminated).
// Returns the number of bytes written (excluding NUL), or -1 if cap is too
// small to hold the boilerplate + at least a meaningful portion of output.
int ai_block_build_context(const char *command, const char *output, int exit_code,
                           char *out, int cap);

// Return a default editable question for the input box, by exit status.
// Returns a string literal; caller must not free.
//   exit_code == 0  → "Explain this output."
//   exit_code  > 0  → "Why did this command fail?"
//   exit_code  < 0  → "Explain this command."  (unknown / no exit code)
const char *ai_block_default_question(int exit_code);

#endif // FANGS_AI_BLOCK_H
