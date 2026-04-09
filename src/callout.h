#ifndef WIKIMARK_CALLOUT_H
#define WIKIMARK_CALLOUT_H

#include <cmark-gfm.h>

/**
 * Convert blockquotes beginning with [!TYPE] into callout divs.
 */
void wm_convert_callouts(cmark_node *root, cmark_mem *mem);

#endif
