#ifndef WIKIMARK_FRONTMATTER_H
#define WIKIMARK_FRONTMATTER_H

#include <cmark-gfm.h>
#include <stddef.h>

/**
 * Parsed frontmatter stored as a simple key-value tree.
 * Supports nested access via dot notation (e.g., "star.name").
 */
typedef struct wm_fm_node {
    char *key;
    char *value;               /* Scalar value (NULL for map/list) */
    struct wm_fm_node *children; /* First child (for map/list) */
    struct wm_fm_node *next;     /* Next sibling */
    int is_list;               /* 1 if this is a sequence */
} wm_fm_node;

/**
 * Extract YAML frontmatter from the beginning of a document.
 * Returns the frontmatter tree, or NULL if no frontmatter found.
 * Sets *body_start to the offset past the closing --- fence.
 */
wm_fm_node *wm_frontmatter_parse(const char *text, size_t len,
                                  size_t *body_start);

/**
 * Look up a value by dot-notation path (e.g., "star.name", "moons.0").
 * Returns the scalar value string, or NULL if not found.
 */
const char *wm_frontmatter_get(const wm_fm_node *root, const char *path);

/**
 * Free a frontmatter tree.
 */
void wm_frontmatter_free(wm_fm_node *root);

/**
 * Promote input defaults to top-level keys.
 * For each input in the "inputs" block, if no top-level key exists
 * with that name, create one from the input's "default" value.
 */
void wm_frontmatter_promote_input_defaults(wm_fm_node *fm);

#endif
