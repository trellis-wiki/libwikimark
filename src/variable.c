/**
 * Variable reference expansion (${...}).
 */

#include "variable.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>

/**
 * Expand ${...} references in a string, returning a new string.
 * Undefined variables become empty strings.
 * \${ is treated as literal ${.
 */
static char *expand_string(const char *s, const wm_fm_node *fm, cmark_mem *mem) {
    size_t len = strlen(s);
    size_t cap = len * 2 + 1;
    char *result = (char *)mem->calloc(1, cap);
    size_t j = 0;

    for (size_t i = 0; i < len; ) {
        /* Check for escaped \${ */
        if (i + 2 < len && s[i] == '\\' && s[i+1] == '$' && s[i+2] == '{') {
            /* Literal ${ */
            if (j + 2 >= cap) { cap *= 2; result = realloc(result, cap); }
            result[j++] = '$';
            result[j++] = '{';
            i += 3;
            continue;
        }

        /* Check for ${ */
        if (i + 1 < len && s[i] == '$' && s[i+1] == '{') {
            /* Find closing } */
            size_t start = i + 2;
            size_t end = start;
            while (end < len && s[end] != '}' && s[end] != '\n') end++;

            if (end < len && s[end] == '}') {
                /* Extract path */
                size_t path_len = end - start;
                char *path = (char *)malloc(path_len + 1);
                memcpy(path, s + start, path_len);
                path[path_len] = '\0';

                /* Resolve */
                const char *value = wm_frontmatter_get(fm, path);
                if (value) {
                    size_t vlen = strlen(value);
                    while (j + vlen >= cap) { cap *= 2; result = realloc(result, cap); }
                    memcpy(result + j, value, vlen);
                    j += vlen;
                }
                /* Undefined → empty string (nothing appended) */

                free(path);
                i = end + 1;
                continue;
            }
            /* Unclosed ${ — treat as literal */
        }

        if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
        result[j++] = s[i++];
    }

    result[j] = '\0';
    return result;
}

void wm_expand_variables(cmark_node *root, const wm_fm_node *frontmatter,
                          cmark_mem *mem) {
    if (!frontmatter) return;

    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    /* Collect text nodes with ${  */
    cmark_node **nodes = NULL;
    int count = 0, cap = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT) {
            const char *lit = cmark_node_get_literal(node);
            if (lit && strstr(lit, "${")) {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 16;
                    nodes = realloc(nodes, cap * sizeof(cmark_node *));
                }
                nodes[count++] = node;
            }
        }
    }
    cmark_iter_free(iter);

    for (int i = 0; i < count; i++) {
        const char *lit = cmark_node_get_literal(nodes[i]);
        char *expanded = expand_string(lit, frontmatter, mem);
        cmark_node_set_literal(nodes[i], expanded);
        mem->free(expanded);
    }

    free(nodes);
}
