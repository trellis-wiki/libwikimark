/**
 * Internal per-parse state stored on the wikilink extension's private data.
 * Set before parsing, cleared after.
 */

#ifndef WIKIMARK_PRIVATE_H
#define WIKIMARK_PRIVATE_H

#include "wikimark.h"
#include "frontmatter.h"

typedef struct {
    wikimark_config config;
    wm_fm_node *frontmatter;  /* Parsed YAML frontmatter (owned) */
    const char *body;         /* Document body after frontmatter (not owned) */
} wm_parse_state;

#endif
