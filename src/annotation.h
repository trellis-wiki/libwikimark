#ifndef WIKIMARK_ANNOTATION_H
#define WIKIMARK_ANNOTATION_H

#include <cmark-gfm.h>

/**
 * Attach semantic annotations (|...|) to preceding elements.
 */
void wm_attach_annotations(cmark_node *root, cmark_mem *mem);

#endif
