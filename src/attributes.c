/**
 * Presentation attributes ({.class #id key=value}).
 *
 * Attaches attributes to preceding link/image nodes by modifying
 * the HTML output. Since cmark's node model doesn't support arbitrary
 * attributes on nodes, we convert the link/image + attribute text into
 * an HTML_INLINE node with the correct HTML.
 *
 * This is a pragmatic approach — a full implementation would require
 * extending cmark's node model.
 */

#include "attributes.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Stub — attributes are complex and will be implemented incrementally */
void wm_attach_attributes(cmark_node *root, cmark_mem *mem) {
    /* TODO: Implement presentation attribute attachment.
     * For now, {.class} text following links is left as literal text.
     * This is needed for §7.8, §10.2-10.8.
     */
    (void)root;
    (void)mem;
}
