#ifndef WIKIMARK_PRIVATE_H
#define WIKIMARK_PRIVATE_H

#include "wikimark.h"
#include "frontmatter.h"

typedef struct {
    wikimark_config config;
    const wikimark_context *context;  /* Engine callbacks (may be NULL) */
    wm_fm_node *frontmatter;          /* Parsed YAML frontmatter (owned by do_convert) */
} wm_parse_state;

#endif
