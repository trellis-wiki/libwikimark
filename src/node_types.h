/**
 * Internal node type storage for WikiMark extensions.
 * Node type IDs are assigned at runtime via cmark_syntax_extension_add_node().
 */

#ifndef WIKIMARK_NODE_TYPES_H
#define WIKIMARK_NODE_TYPES_H

#include <cmark-gfm.h>

/* Assigned in create_wikilink_extension() */
extern cmark_node_type CMARK_NODE_WM_WIKILINK;

#endif /* WIKIMARK_NODE_TYPES_H */
