#ifndef WIKIMARK_TEMPLATE_H
#define WIKIMARK_TEMPLATE_H

#include <cmark-gfm.h>
#include "wikimark.h"

/**
 * Process {{...}} template transclusions in text nodes.
 * Uses engine callbacks to resolve templates, or produces error indicators.
 */
void wm_process_templates_with_context(cmark_node *root, cmark_mem *mem,
                                        const wikimark_context *ctx);

#endif
