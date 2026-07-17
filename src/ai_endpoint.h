#ifndef FANGS_AI_ENDPOINT_H
#define FANGS_AI_ENDPOINT_H

#include <stdbool.h>
#include <stddef.h>

// Resolve a provider base URL or an already-complete endpoint into the exact
// request URL used by the provider transport. Existing full endpoints are
// preserved for backward compatibility.
bool ai_endpoint_resolve(const char *provider, const char *configured,
                         char *out, size_t out_size);

#endif // FANGS_AI_ENDPOINT_H
