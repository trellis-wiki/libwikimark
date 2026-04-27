/**
 * Semantic annotations (|property key=value|).
 *
 * Handles annotations on template output: when |...|  follows an
 * HTML_INLINE node (from template expansion), wraps the HTML content
 * in a <span> with the annotation attributes.
 */

#include "annotation.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void wm_attach_annotations(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    typedef struct { cmark_node *html_node; cmark_node *text_node; } annot_pair;
    annot_pair *pairs = NULL;
    int count = 0, cap = 0;
    cmark_node *prev_html = NULL;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_HTML_INLINE) {
            prev_html = node;
            continue;
        }
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT && prev_html) {
            const char *lit = cmark_node_get_literal(node);
            if (lit && lit[0] == '|') {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 8;
                    void *tmp = realloc(pairs, cap * sizeof(annot_pair));
                    if (!tmp) break;
                    pairs = tmp;
                }
                pairs[count].html_node = prev_html;
                pairs[count].text_node = node;
                count++;
            }
            prev_html = NULL;
            continue;
        }
        if (ev_type == CMARK_EVENT_ENTER || ev_type == CMARK_EVENT_EXIT)
            prev_html = NULL;
    }
    cmark_iter_free(iter);

    for (int i = 0; i < count; i++) {
        cmark_node *html_node = pairs[i].html_node;
        cmark_node *text_node = pairs[i].text_node;
        const char *html_lit = cmark_node_get_literal(html_node);
        const char *text_lit = cmark_node_get_literal(text_node);
        if (!html_lit || !text_lit) continue;

        /* Parse |...|  from text */
        if (text_lit[0] != '|') continue;
        const char *astart = text_lit + 1;
        const char *aend = strchr(astart, '|');
        if (!aend || aend == astart) continue;

        /* Build data-wm-* attributes */
        char data_attrs[1024] = "";
        const char *p = astart;
        int first = 1;
        while (p < aend) {
            while (p < aend && *p == ' ') p++;
            if (p >= aend) break;

            const char *ts = p;
            while (p < aend && *p != '=' && *p != ' ') p++;

            if (p < aend && *p == '=') {
                int ke = (int)(p - ts); p++;
                char val[256] = "";
                if (p < aend && *p == '"') {
                    p++;
                    const char *vs = p;
                    while (p < aend && *p != '"') p++;
                    snprintf(val, sizeof(val), "%.*s", (int)(p - vs), vs);
                    if (p < aend) p++;
                } else {
                    const char *vs = p;
                    while (p < aend && *p != ' ') p++;
                    snprintf(val, sizeof(val), "%.*s", (int)(p - vs), vs);
                }
                char buf[512];
                snprintf(buf, sizeof(buf), " data-wm-%.*s=\"%s\"", ke, ts, val);
                strcat(data_attrs, buf);
            } else {
                if (first) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), " data-wm-%.*s=\"%.*s\"",
                             (int)(p - ts), ts, (int)(p - ts), ts);
                    /* Actually for bare property: data-wm-property="name" */
                    /* But we use the key=value form for consistency */
                    /* The spec says data-wm-KEY="VALUE" for named, data-wm-property for bare */
                    /* Since this is bare: */
                    snprintf(buf, sizeof(buf), " data-wm-%.*s=\"%.*s\"",
                             (int)(p - ts), ts, (int)(p - ts), ts);
                    strcat(data_attrs, buf);
                }
            }
            first = 0;
        }

        if (!data_attrs[0]) continue;

        /* Wrap the HTML content in <span ...>content</span> */
        size_t new_cap = strlen(html_lit) + strlen(data_attrs) + 30;
        char *new_html = (char *)malloc(new_cap);
        if (!new_html) continue;
        snprintf(new_html, new_cap, "<span%s>%s</span>", data_attrs, html_lit);

        cmark_node_set_literal(html_node, new_html);
        free(new_html);

        /* Remove consumed |...|  from text node */
        const char *remaining = aend + 1;
        if (*remaining) {
            cmark_node_set_literal(text_node, remaining);
        } else {
            cmark_node_free(text_node);
        }
    }

    free(pairs);
}
