#ifndef WIKIMARK_TEMPLATE_H
#define WIKIMARK_TEMPLATE_H

#include <cmark-gfm.h>

/**
 * Detect {{...}} template transclusions in text nodes and convert them
 * to error indicators (since template resolution requires a page store).
 */
void wm_process_templates(cmark_node *root, cmark_mem *mem);

#endif
