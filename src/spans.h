#ifndef WIKIMARK_SPANS_H
#define WIKIMARK_SPANS_H

#include <cmark-gfm.h>

/**
 * Convert [text]{...} and [text]|...| patterns in text nodes to
 * HTML spans with attributes/annotations.
 */
void wm_process_spans(cmark_node *root, cmark_mem *mem);

#endif
