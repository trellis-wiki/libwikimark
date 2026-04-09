#ifndef WIKIMARK_VARIABLE_H
#define WIKIMARK_VARIABLE_H

#include <cmark-gfm.h>
#include "wikimark.h"

/**
 * Expand ${...} variable references in a string using engine callbacks.
 * Returns a newly allocated string (caller must free), or NULL if no expansions.
 */
char *wm_expand_variables_str(const char *s, size_t len,
                               const wikimark_context *ctx);

#endif
