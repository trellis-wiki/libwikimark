/**
 * Callouts (admonitions): > [!TYPE] optional title
 *
 * Converts blockquotes whose first paragraph begins with [!TYPE] into
 * callout HTML. Since cmark doesn't have a callout node type, we replace
 * the blockquote with raw HTML.
 */

#include "callout.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/**
 * Check if a text starts with [!TYPE] pattern.
 * Returns the type string (caller must free), or NULL if no match.
 * Sets *has_custom_title to 1 if a space follows ] (indicating inline title).
 * Sets *rest_text to any remaining text after the title indicator space.
 */
static char *parse_callout_marker(const char *text, int *has_custom_title,
                                   const char **rest_text) {
    *has_custom_title = 0;
    *rest_text = NULL;

    if (!text || text[0] != '[' || text[1] != '!')
        return NULL;

    /* Find closing ] */
    const char *end = strchr(text + 2, ']');
    if (!end) return NULL;

    size_t type_len = end - (text + 2);
    if (type_len == 0) return NULL;

    char *type = (char *)malloc(type_len + 1);
    if (!type) return NULL;
    memcpy(type, text + 2, type_len);
    type[type_len] = '\0';

    /* Convert type to lowercase for class name */
    for (size_t i = 0; i < type_len; i++) {
        if (type[i] >= 'A' && type[i] <= 'Z')
            type[i] += 32;
    }

    /* Check what follows ] */
    const char *after = end + 1;
    if (*after == ' ') {
        *has_custom_title = 1;
        after++;
        /* Remaining text in this node (might be empty if title is in sibling nodes) */
        size_t tlen = strlen(after);
        while (tlen > 0 && (after[tlen-1] == '\n' || after[tlen-1] == '\r'))
            tlen--;
        if (tlen > 0) {
            *rest_text = after;
        }
    } else if (*after == '\0' || *after == '\n' || *after == '\r') {
        /* No custom title */
    } else {
        /* Not a valid callout */
        free(type);
        return NULL;
    }

    return type;
}

/**
 * Build the default title from the type name (capitalize first letter).
 */
static char *default_title(const char *type) {
    char *title = strdup(type);
    if (!title) return NULL;
    if (title[0] >= 'a' && title[0] <= 'z')
        title[0] -= 32;
    return title;
}

/**
 * Render a list of inline nodes to HTML by creating a temporary document,
 * moving nodes into it, rendering, then moving them back.
 * Returns malloc'd HTML string with <p></p> wrapper stripped, or NULL.
 */
static char *render_inline_nodes_to_html(cmark_node *first_node,
                                          cmark_node *last_before_softbreak,
                                          cmark_mem *mem) {
    /* Create a temporary document → paragraph to hold the title nodes */
    cmark_node *tmp_doc = cmark_node_new_with_mem(CMARK_NODE_DOCUMENT, mem);
    cmark_node *tmp_para = cmark_node_new_with_mem(CMARK_NODE_PARAGRAPH, mem);
    cmark_node_append_child(tmp_doc, tmp_para);

    /* Move nodes from first_node to last_before_softbreak into tmp_para */
    cmark_node *node = first_node;
    while (node) {
        cmark_node *next = cmark_node_next(node);
        cmark_node_append_child(tmp_para, node);
        if (node == last_before_softbreak) break;
        node = next;
    }

    char *html = cmark_render_html(tmp_doc, CMARK_OPT_DEFAULT, NULL);

    /* Strip <p>...</p>\n wrapper */
    char *result = NULL;
    if (html) {
        size_t hlen = strlen(html);
        if (hlen > 7 && strncmp(html, "<p>", 3) == 0) {
            char *close_p = strstr(html + 3, "</p>");
            if (close_p) {
                size_t inner_len = close_p - (html + 3);
                result = (char *)malloc(inner_len + 1);
                if (!result) { free(html); cmark_node_free(tmp_doc); return NULL; }
                memcpy(result, html + 3, inner_len);
                result[inner_len] = '\0';
            }
        }
        free(html);
    }

    /* The nodes are now owned by tmp_doc; freeing tmp_doc frees them.
     * Since we consumed the title nodes, they should not be in the original tree. */
    cmark_node_free(tmp_doc);

    return result;
}

void wm_convert_callouts(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    /* Collect blockquotes to check */
    cmark_node **quotes = NULL;
    int count = 0, cap = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER &&
            node->type == CMARK_NODE_BLOCK_QUOTE) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 8;
                void *tmp = realloc(quotes, cap * sizeof(cmark_node *));
                if (!tmp) break;
                quotes = tmp;
            }
            quotes[count++] = node;
        }
    }
    cmark_iter_free(iter);

    for (int i = 0; i < count; i++) {
        cmark_node *bq = quotes[i];
        cmark_node *first_child = cmark_node_first_child(bq);
        if (!first_child || first_child->type != CMARK_NODE_PARAGRAPH)
            continue;

        /* Get the first text node in the paragraph */
        cmark_node *text_node = cmark_node_first_child(first_child);
        if (!text_node || text_node->type != CMARK_NODE_TEXT)
            continue;

        const char *text = cmark_node_get_literal(text_node);
        int has_custom_title = 0;
        const char *rest_text = NULL;
        char *type = parse_callout_marker(text, &has_custom_title, &rest_text);
        if (!type) continue;

        /* Build the title HTML */
        char *title_html = NULL;

        if (has_custom_title) {
            /* The custom title consists of:
             * 1. Any remaining text in the marker text node after "] "
             * 2. All sibling nodes until the first SOFTBREAK
             *
             * For plain titles like "> [!WARNING] Plain title", all title text
             * is in the marker text node.
             * For formatted titles like "> [!WARNING] **Bold title**", cmark
             * splits it: TEXT "[!WARNING] " + STRONG "Bold title"
             */

            /* Find the softbreak or end of first paragraph children */
            cmark_node *after_marker = cmark_node_next(text_node);
            cmark_node *softbreak = NULL;
            cmark_node *last_title_node = NULL;

            /* Check if there are nodes between the marker and softbreak */
            cmark_node *scan = after_marker;
            while (scan) {
                if (scan->type == CMARK_NODE_SOFTBREAK) {
                    softbreak = scan;
                    break;
                }
                last_title_node = scan;
                scan = cmark_node_next(scan);
            }

            if (rest_text && !after_marker) {
                /* Simple case: entire title is plain text in the marker node */
                title_html = strdup(rest_text);
            } else if (after_marker && last_title_node) {
                /* Complex case: title includes AST nodes (formatted text).
                 * Create a text node for any remaining marker text, then
                 * render all title nodes through cmark. */

                /* If there's remaining text after [!TYPE] , create a text node */
                cmark_node *title_start = after_marker;
                if (rest_text && strlen(rest_text) > 0) {
                    cmark_node *rest_node = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                    cmark_node_set_literal(rest_node, rest_text);
                    cmark_node_insert_before(after_marker, rest_node);
                    title_start = rest_node;
                }

                title_html = render_inline_nodes_to_html(title_start, last_title_node, mem);
            } else if (rest_text) {
                /* Only text after marker, no formatted siblings */
                title_html = strdup(rest_text);
            }
        }

        if (!title_html) {
            char *def = default_title(type);
            title_html = def;
        }

        /* Build opening HTML */
        size_t html_cap = 256 + strlen(type) + strlen(title_html);
        char *open_html = (char *)malloc(html_cap);
        if (!open_html) { free(title_html); free(type); continue; }
        snprintf(open_html, html_cap,
            "<div class=\"wm-callout wm-callout-%s\">\n"
            "<p class=\"wm-callout-title\">%s</p>\n",
            type, title_html);

        /* Replace the blockquote with HTML:
         * 1. Insert open HTML before blockquote
         * 2. Move remaining children out of blockquote
         * 3. Insert close HTML
         * 4. Remove blockquote
         */
        cmark_node *open_node = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
        cmark_node_set_literal(open_node, open_html);
        cmark_node_insert_before(bq, open_node);

        /* Remove the callout marker text node */
        cmark_node_free(text_node);

        /* Remove any leading softbreak after the title nodes were consumed */
        cmark_node *next_child = cmark_node_first_child(first_child);
        if (next_child && next_child->type == CMARK_NODE_SOFTBREAK) {
            cmark_node_free(next_child);
        }

        /* If the first paragraph is now empty, remove it */
        if (!cmark_node_first_child(first_child)) {
            cmark_node_free(first_child);
        }

        /* Move remaining blockquote children to be siblings after open_node */
        cmark_node *insert_point = open_node;
        while (cmark_node_first_child(bq)) {
            cmark_node *child = cmark_node_first_child(bq);
            cmark_node_insert_after(insert_point, child);
            insert_point = child;
        }

        /* Add closing HTML */
        cmark_node *close_node = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
        cmark_node_set_literal(close_node, "</div>\n");
        cmark_node_insert_after(insert_point, close_node);

        /* Remove the now-empty blockquote */
        cmark_node_free(bq);

        free(open_html);
        free(title_html);
        free(type);
    }

    free(quotes);
}
