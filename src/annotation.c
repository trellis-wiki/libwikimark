/**
 * Semantic annotations (|property key=value|).
 */

#include "annotation.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>

/* Stub — annotations will be implemented incrementally */
void wm_attach_annotations(cmark_node *root, cmark_mem *mem) {
    /* TODO: Implement semantic annotation attachment.
     * For now, |...| text following links is left as literal text.
     * This is needed for §12.
     */
    (void)root;
    (void)mem;
}
