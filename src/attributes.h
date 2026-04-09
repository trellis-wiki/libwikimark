#ifndef WIKIMARK_ATTRIBUTES_H
#define WIKIMARK_ATTRIBUTES_H

#include <cmark-gfm.h>

/**
 * Attach presentation attributes ({...}) to preceding elements.
 * Scans text nodes for {...} patterns following links/images/spans.
 */
void wm_attach_attributes(cmark_node *root, cmark_mem *mem);

#endif
