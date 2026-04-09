#ifndef WIKIMARK_WIKILINK_H
#define WIKIMARK_WIKILINK_H

#include <cmark-gfm.h>

/* Opaque data stored on CMARK_NODE_WM_WIKILINK nodes */
typedef struct {
    char *target;     /* Raw target text (before normalization) */
    char *normalized; /* Normalized URL */
} wikilink_data;

cmark_syntax_extension *create_wikilink_extension(void);

#endif
