#ifndef WIKIMARK_ATTRIBUTES_H
#define WIKIMARK_ATTRIBUTES_H

#include <cmark-gfm.h>

/* Max number of attributes/annotations per element */
#define WM_MAX_ATTRS 32

typedef struct {
    char *classes[WM_MAX_ATTRS];
    int class_count;
    char *id;
    char *keys[WM_MAX_ATTRS];
    char *values[WM_MAX_ATTRS];
    int kv_count;
} wm_parsed_attrs;

typedef struct {
    char *property;
    char *keys[WM_MAX_ATTRS];
    char *values[WM_MAX_ATTRS];
    int kv_count;
} wm_parsed_annotation;

void wm_free_attrs(wm_parsed_attrs *a);
void wm_free_annotation(wm_parsed_annotation *a);

/**
 * Parse a {.class #id key=value} block from s starting at *pos.
 * Returns 1 on success, 0 on failure. Advances *pos past the }.
 */
int wm_parse_attr_block(const char *s, int len, int *pos, wm_parsed_attrs *out);

/**
 * Parse a |property key=value| block from s starting at *pos.
 * Returns 1 on success, 0 on failure. Advances *pos past the closing |.
 */
int wm_parse_annotation_block(const char *s, int len, int *pos, wm_parsed_annotation *out);

/**
 * Attach presentation attributes ({...}) to preceding elements.
 */
void wm_attach_attributes(cmark_node *root, cmark_mem *mem);

#endif
