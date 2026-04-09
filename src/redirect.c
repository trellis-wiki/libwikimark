/**
 * Redirect handling (#REDIRECT [[Page]] or frontmatter redirect:).
 */

#include "redirect.h"
#include "normalize.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

int wm_handle_redirect(cmark_node *root, const wm_fm_node *frontmatter,
                        const char *body, cmark_mem *mem) {
    const char *target = NULL;
    char *target_buf = NULL;

    /* Check frontmatter redirect: key first (takes precedence) */
    if (frontmatter) {
        target = wm_frontmatter_get(frontmatter, "redirect");
    }

    /* Check for inline #REDIRECT [[Page]] */
    if (!target && body) {
        /* Skip leading whitespace */
        const char *p = body;
        while (*p == ' ' || *p == '\t') p++;

        /* Case-insensitive #REDIRECT match */
        if ((*p == '#') &&
            (p[1] == 'R' || p[1] == 'r') &&
            (p[2] == 'E' || p[2] == 'e') &&
            (p[3] == 'D' || p[3] == 'd') &&
            (p[4] == 'I' || p[4] == 'i') &&
            (p[5] == 'R' || p[5] == 'r') &&
            (p[6] == 'E' || p[6] == 'e') &&
            (p[7] == 'C' || p[7] == 'c') &&
            (p[8] == 'T' || p[8] == 't')) {
            p += 9;
            /* Skip whitespace */
            while (*p == ' ' || *p == '\t') p++;
            /* Expect [[ */
            if (p[0] == '[' && p[1] == '[') {
                p += 2;
                const char *start = p;
                while (*p && *p != ']' && *p != '\n') p++;
                if (p[0] == ']' && p[1] == ']') {
                    size_t tlen = p - start;
                    target_buf = (char *)malloc(tlen + 1);
                    memcpy(target_buf, start, tlen);
                    target_buf[tlen] = '\0';
                    target = target_buf;
                }
            }
        }
    }

    if (!target) return 0;

    /* Save display target before freeing */
    char *display_target = strdup(target);

    /* Normalize the target */
    char *normalized = wikimark_normalize_title(target, 0, mem);
    if (target_buf) free(target_buf);
    if (!normalized) { free(display_target); return 0; }

    /* Replace document content with redirect notice */
    while (cmark_node_first_child(root)) {
        cmark_node_free(cmark_node_first_child(root));
    }

    char *html = (char *)mem->calloc(1, strlen(display_target) + strlen(normalized) + 100);
    sprintf(html, "<p>Redirecting to <a href=\"%s\">%s</a>.</p>\n",
            normalized, display_target);

    cmark_node *p = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
    cmark_node_set_literal(p, html);
    cmark_node_append_child(root, p);

    mem->free(html);
    mem->free(normalized);
    free(display_target);
    return 1;
}
