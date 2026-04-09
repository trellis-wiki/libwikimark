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
 * Returns the type string (caller must free) and sets *rest to the
 * remaining text after the marker, or NULL if no match.
 */
static char *parse_callout_marker(const char *text, const char **rest,
                                   char **custom_title) {
    *rest = NULL;
    *custom_title = NULL;

    if (!text || text[0] != '[' || text[1] != '!')
        return NULL;

    /* Find closing ] */
    const char *end = strchr(text + 2, ']');
    if (!end) return NULL;

    size_t type_len = end - (text + 2);
    if (type_len == 0) return NULL;

    char *type = (char *)malloc(type_len + 1);
    memcpy(type, text + 2, type_len);
    type[type_len] = '\0';

    /* Convert type to lowercase for class name */
    for (size_t i = 0; i < type_len; i++) {
        if (type[i] >= 'A' && type[i] <= 'Z')
            type[i] += 32;
    }

    /* Check for custom title after ] */
    const char *after = end + 1;
    if (*after == ' ') {
        after++;
        /* Rest of the line is custom title */
        size_t tlen = strlen(after);
        /* Trim trailing newline */
        while (tlen > 0 && (after[tlen-1] == '\n' || after[tlen-1] == '\r'))
            tlen--;
        if (tlen > 0) {
            *custom_title = (char *)malloc(tlen + 1);
            memcpy(*custom_title, after, tlen);
            (*custom_title)[tlen] = '\0';
        }
        *rest = NULL; /* title consumed the line */
    } else if (*after == '\0' || *after == '\n' || *after == '\r') {
        *rest = NULL;
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
    if (title[0] >= 'a' && title[0] <= 'z')
        title[0] -= 32;
    return title;
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
                quotes = realloc(quotes, cap * sizeof(cmark_node *));
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
        const char *rest = NULL;
        char *custom_title = NULL;
        char *type = parse_callout_marker(text, &rest, &custom_title);
        if (!type) continue;

        /* Build the title */
        char *title = custom_title ? custom_title : default_title(type);

        /* Build opening HTML */
        size_t html_cap = 256 + strlen(type) + strlen(title);
        char *open_html = (char *)malloc(html_cap);
        snprintf(open_html, html_cap,
            "<div class=\"wm-callout wm-callout-%s\">\n"
            "<p class=\"wm-callout-title\">%s</p>\n",
            type, title);

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

        /* Also remove any leading softbreak after the marker */
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
        if (!custom_title) free(title);
        free(type);
        if (custom_title) free(custom_title);
    }

    free(quotes);
}
