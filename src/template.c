/**
 * Template transclusion ({{name args}}).
 * Uses engine callbacks for resolution, or produces error indicators.
 */

#include "template.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void wm_process_templates_with_context(cmark_node *root, cmark_mem *mem,
                                        const wikimark_context *ctx) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

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

    for (int idx = 0; idx < count; idx++) {
        const char *lit = cmark_node_get_literal(nodes[idx]);
        size_t len = strlen(lit);
        size_t pos = 0;
        int found = 0;
        cmark_node *insert_after = nodes[idx];
        size_t last_end = 0;

        while (pos + 1 < len) {
            if (lit[pos] == '{' && lit[pos+1] == '{') {
                /* Find closing }} with nesting */
                size_t close = pos + 2;
                int depth = 1;
                while (close + 1 < len) {
                    if (lit[close] == '{' && lit[close+1] == '{') {
                        depth++; close += 2; continue;
                    }
                    if (lit[close] == '}' && lit[close+1] == '}') {
                        depth--;
                        if (depth == 0) break;
                        close += 2; continue;
                    }
                    close++;
                }

                if (depth == 0) {
                    found = 1;
                    size_t tmpl_start = pos + 2;
                    size_t tmpl_end = close;
                    size_t tmpl_len = tmpl_end - tmpl_start;

                    /* Extract template name and args */
                    char *content = (char *)malloc(tmpl_len + 1);
                    memcpy(content, lit + tmpl_start, tmpl_len);
                    content[tmpl_len] = '\0';

                    /* Split name from args (first space) */
                    char *name = content;
                    char *args = NULL;
                    char *space = strchr(content, ' ');
                    if (space) {
                        *space = '\0';
                        args = space + 1;
                    }

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

                    /* Try engine callback for resolution */
                    const char *resolved = NULL;
                    if (ctx && ctx->resolve_template) {
                        resolved = ctx->resolve_template(name, args, ctx->user_data);
                    }

                    if (resolved) {
                        /* Insert resolved HTML, stripping outer <p>...</p> for inline use */
                        const char *inner = resolved;
                        size_t inner_len = strlen(resolved);
                        if (inner_len > 7 &&
                            strncmp(inner, "<p>", 3) == 0 &&
                            strncmp(inner + inner_len - 4, "</p>", 4) == 0) {
                            /* Strip <p> and </p> */
                            char *stripped = (char *)mem->calloc(1, inner_len - 6);
                            memcpy(stripped, inner + 3, inner_len - 7);
                            stripped[inner_len - 7] = '\0';
                            cmark_node *html = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                            cmark_node_set_literal(html, stripped);
                            mem->free(stripped);
                            cmark_node_insert_after(insert_after, html);
                            insert_after = html;
                        } else {
                            cmark_node *html = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                            cmark_node_set_literal(html, resolved);
                            cmark_node_insert_after(insert_after, html);
                            insert_after = html;
                        }
                    } else {
                        /* Error indicator */
                        size_t full_len = close + 2 - pos;
                        size_t buf_len = full_len + 50;
                        char *err_buf = (char *)mem->calloc(1, buf_len);
                        snprintf(err_buf, buf_len,
                            "<span class=\"wm-error\">%.*s</span>",
                            (int)full_len, lit + pos);
                        cmark_node *err = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                        cmark_node_set_literal(err, err_buf);
                        mem->free(err_buf);
                        cmark_node_insert_after(insert_after, err);
                        insert_after = err;
                    }

                    free(content);
                    last_end = close + 2;
                    pos = last_end;
                    continue;
                }
            }
            pos++;
        }

        if (found) {
            if (last_end < len) {
                cmark_node *after = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                char *t = (char *)mem->calloc(1, len - last_end + 1);
                memcpy(t, lit + last_end, len - last_end);
                cmark_node_set_literal(after, t);
                mem->free(t);
                cmark_node_insert_after(insert_after, after);
            }
            cmark_node_free(nodes[idx]);
        }
    }

    free(nodes);
}
