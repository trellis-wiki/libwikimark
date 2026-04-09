/**
 * Redirect handling (frontmatter redirect: key only).
 */

#include "redirect.h"
#include "normalize.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int wm_handle_redirect(cmark_node *root, const wm_fm_node *frontmatter,
                        const char *body, cmark_mem *mem) {
    (void)body; /* Inline #REDIRECT removed — frontmatter only */

    if (!frontmatter) return 0;

    const char *target = wm_frontmatter_get(frontmatter, "redirect");
    if (!target) return 0;

    char *normalized = wikimark_normalize_title(target, 0, mem);
    if (!normalized) return 0;

    /* Replace document content with redirect notice */
    while (cmark_node_first_child(root)) {
        cmark_node_free(cmark_node_first_child(root));
    }

    size_t html_len = strlen(target) + strlen(normalized) + 100;
    char *html = (char *)mem->calloc(1, html_len);
    snprintf(html, html_len, "<p>Redirecting to <a href=\"%s\">%s</a>.</p>\n",
             normalized, target);

    cmark_node *p = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
    cmark_node_set_literal(p, html);
    cmark_node_append_child(root, p);

    mem->free(html);
    mem->free(normalized);
    return 1;
}
