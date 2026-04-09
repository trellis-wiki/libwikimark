/**
 * Template transclusion ({{name args}}).
 *
 * Templates require a page/file resolution system that is implementation-defined.
 * Without a configured template_dir, all templates render as error indicators.
 */

#include "template.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Process text nodes containing {{...}}, replacing with error indicators
 * (template resolution not yet implemented).
 */
void wm_process_templates(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    /* Collect text nodes with {{ */
    cmark_node **nodes = NULL;
    int count = 0, cap = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT) {
            const char *lit = cmark_node_get_literal(node);
            if (lit && strstr(lit, "{{")) {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 8;
                    nodes = realloc(nodes, cap * sizeof(cmark_node *));
                }
                nodes[count++] = node;
            }
        }
    }
    cmark_iter_free(iter);

    for (int i = 0; i < count; i++) {
        const char *lit = cmark_node_get_literal(nodes[i]);
        size_t len = strlen(lit);

        /* Find {{ and matching }} */
        size_t pos = 0;
        int found = 0;
        cmark_node *insert_after = nodes[i];
        size_t last_end = 0;

        while (pos + 1 < len) {
            if (lit[pos] == '{' && lit[pos+1] == '{') {
                /* Find closing }} */
                size_t close = pos + 2;
                int depth = 1;
                while (close + 1 < len) {
                    if (lit[close] == '{' && lit[close+1] == '{') { depth++; close += 2; continue; }
                    if (lit[close] == '}' && lit[close+1] == '}') {
                        depth--;
                        if (depth == 0) break;
                        close += 2;
                        continue;
                    }
                    close++;
                }

                if (depth == 0) {
                    found = 1;

                    /* Text before */
                    if (pos > last_end) {
                        cmark_node *before = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                        char *t = (char *)mem->calloc(1, pos - last_end + 1);
                        memcpy(t, lit + last_end, pos - last_end);
                        cmark_node_set_literal(before, t);
                        mem->free(t);
                        cmark_node_insert_after(insert_after, before);
                        insert_after = before;
                    }

                    /* Error indicator for the template */
                    size_t tmpl_len = close + 2 - pos;
                    char *err_buf = (char *)mem->calloc(1, tmpl_len + 50);
                    snprintf(err_buf, tmpl_len + 50,
                        "<span class=\"wm-error\">%.*s</span>",
                        (int)tmpl_len, lit + pos);

                    cmark_node *err = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                    cmark_node_set_literal(err, err_buf);
                    mem->free(err_buf);
                    cmark_node_insert_after(insert_after, err);
                    insert_after = err;

                    last_end = close + 2;
                    pos = last_end;
                    continue;
                }
            }
            pos++;
        }

        if (found) {
            /* Remaining text */
            if (last_end < len) {
                cmark_node *after = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                char *t = (char *)mem->calloc(1, len - last_end + 1);
                memcpy(t, lit + last_end, len - last_end);
                cmark_node_set_literal(after, t);
                mem->free(t);
                cmark_node_insert_after(insert_after, after);
            }
            cmark_node_free(nodes[i]);
        }
    }

    free(nodes);
}
