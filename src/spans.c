/**
 * Span processing: [text]{.class key=val} and [text]|property key=val|
 *
 * These are text-node patterns where cmark left bare [text] as literal.
 * We detect [text] followed by { or | and convert to HTML spans.
 */

#include "spans.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Try to parse a [text]{...} or [text]|...| span starting at pos.
 * Returns position past the construct, or -1 if no match.
 * Sets *text_start, *text_len, *html (caller must free html).
 */
static int try_parse_span(const char *s, int len, int pos, char **html_out) {
    if (pos >= len || s[pos] != '[') return -1;

    /* Find closing ] */
    int bracket_start = pos + 1;
    int bracket_end = bracket_start;
    while (bracket_end < len && s[bracket_end] != ']' && s[bracket_end] != '\n')
        bracket_end++;
    if (bracket_end >= len || s[bracket_end] != ']') return -1;

    int text_start = bracket_start;
    int text_len = bracket_end - bracket_start;
    int after_bracket = bracket_end + 1;

    /* Must be immediately followed by { or | */
    if (after_bracket >= len) return -1;
    if (s[after_bracket] != '{' && s[after_bracket] != '|') return -1;

    /* Parse modifiers */
    char classes[512] = "";
    char id_attr[128] = "";
    char kv_attrs[1024] = "";
    char data_attrs[1024] = "";
    int p = after_bracket;

    /* Parse {..} blocks */
    while (p < len && s[p] == '{') {
        int bstart = p + 1;
        int in_q = 0;
        int bend = bstart;
        while (bend < len) {
            if (in_q) {
                if (s[bend] == '\\' && bend + 1 < len) { bend += 2; continue; }
                if (s[bend] == '"') in_q = 0;
                bend++; continue;
            }
            if (s[bend] == '"') { in_q = 1; bend++; continue; }
            if (s[bend] == '}') break;
            if (s[bend] == '\n') return -1;
            bend++;
        }
        if (bend >= len || s[bend] != '}') return -1;

        /* Parse tokens inside {...} */
        int tp = bstart;
        while (tp < bend) {
            while (tp < bend && s[tp] == ' ') tp++;
            if (tp >= bend) break;

            if (s[tp] == '.') {
                tp++;
                int cs = tp;
                while (tp < bend && s[tp] != ' ') tp++;
                if (classes[0]) strcat(classes, " ");
                strncat(classes, s + cs, tp - cs);
            } else if (s[tp] == '#') {
                tp++;
                int cs = tp;
                while (tp < bend && s[tp] != ' ') tp++;
                snprintf(id_attr, sizeof(id_attr), " id=\"%.*s\"", tp - cs, s + cs);
            } else {
                int ks = tp;
                while (tp < bend && s[tp] != '=' && s[tp] != ' ') tp++;
                if (tp < bend && s[tp] == '=') {
                    int ke = tp; tp++;
                    char val[256] = "";
                    if (tp < bend && s[tp] == '"') {
                        tp++;
                        int vs = tp;
                        while (tp < bend && s[tp] != '"') {
                            if (s[tp] == '\\' && tp + 1 < bend) tp++;
                            tp++;
                        }
                        snprintf(val, sizeof(val), "%.*s", tp - vs, s + vs);
                        if (tp < bend) tp++;
                    } else {
                        int vs = tp;
                        while (tp < bend && s[tp] != ' ') tp++;
                        snprintf(val, sizeof(val), "%.*s", tp - vs, s + vs);
                    }
                    char buf[512];
                    snprintf(buf, sizeof(buf), " %.*s=\"%s\"", ke - ks, s + ks, val);
                    strcat(kv_attrs, buf);
                } else {
                    while (tp < bend && s[tp] != ' ') tp++;
                }
            }
        }
        p = bend + 1;
    }

    /* Parse |..| block */
    if (p < len && s[p] == '|') {
        int astart = p + 1;
        int in_q = 0;
        int aend = astart;
        while (aend < len) {
            if (in_q) {
                if (s[aend] == '\\' && aend + 1 < len) { aend += 2; continue; }
                if (s[aend] == '"') in_q = 0;
                aend++; continue;
            }
            if (s[aend] == '"') { in_q = 1; aend++; continue; }
            if (s[aend] == '|') break;
            if (s[aend] == '\n') break;
            aend++;
        }
        if (aend < len && s[aend] == '|' && aend > astart) {
            /* Parse annotation tokens */
            int ap = astart;
            int first = 1;
            while (ap < aend) {
                while (ap < aend && s[ap] == ' ') ap++;
                if (ap >= aend) break;

                int ts = ap;
                while (ap < aend && s[ap] != '=' && s[ap] != ' ') ap++;

                if (ap < aend && s[ap] == '=') {
                    int ke = ap; ap++;
                    char val[256] = "";
                    if (ap < aend && s[ap] == '"') {
                        ap++;
                        int vs = ap;
                        while (ap < aend && s[ap] != '"') {
                            if (s[ap] == '\\' && ap + 1 < aend) ap++;
                            ap++;
                        }
                        snprintf(val, sizeof(val), "%.*s", ap - vs, s + vs);
                        if (ap < aend) ap++;
                    } else {
                        int vs = ap;
                        while (ap < aend && s[ap] != ' ') ap++;
                        snprintf(val, sizeof(val), "%.*s", ap - vs, s + vs);
                    }
                    char buf[512];
                    snprintf(buf, sizeof(buf), " data-wm-%.*s=\"%s\"",
                             ke - ts, s + ts, val);
                    strcat(data_attrs, buf);
                } else {
                    /* Bare word = property name */
                    if (first) {
                        char buf[512];
                        snprintf(buf, sizeof(buf), " data-wm-property=\"%.*s\"",
                                 ap - ts, s + ts);
                        strcat(data_attrs, buf);
                    }
                }
                first = 0;
            }
            p = aend + 1;
        }
    }

    /* Must have consumed at least one modifier */
    if (p == after_bracket) return -1;

    /* Build HTML */
    int is_silent = (text_len == 0 && data_attrs[0]);
    size_t html_cap = text_len + strlen(classes) + strlen(id_attr) +
                      strlen(kv_attrs) + strlen(data_attrs) + 100;
    char *html = (char *)malloc(html_cap);

    if (is_silent) {
        snprintf(html, html_cap, "<span%s%s%s%s class=\"wm-silent\"></span>",
                 classes[0] ? " class=\"" : "", classes, classes[0] ? "\"" : "",
                 data_attrs);
        /* Fixup: put data_attrs properly */
        snprintf(html, html_cap, "<span%s%s class=\"wm-silent\"></span>",
                 data_attrs,
                 id_attr);
    } else {
        char class_str[600] = "";
        if (classes[0]) snprintf(class_str, sizeof(class_str), " class=\"%s\"", classes);

        snprintf(html, html_cap, "<span%s%s%s%s>%.*s</span>",
                 class_str, id_attr, kv_attrs, data_attrs,
                 text_len, s + text_start);
    }

    *html_out = html;
    return p;
}

void wm_process_spans(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    cmark_node **nodes = NULL;
    int count = 0, cap = 0;
    int inside_link = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (node->type == CMARK_NODE_LINK || node->type == CMARK_NODE_IMAGE) {
            if (ev_type == CMARK_EVENT_ENTER) inside_link++;
            else if (ev_type == CMARK_EVENT_EXIT) inside_link--;
        }
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT
            && !inside_link) {
            const char *lit = cmark_node_get_literal(node);
            /* Look for [text]{ or [text]| pattern */
            if (lit && strchr(lit, '[')) {
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
        if (!lit) continue;
        int len = (int)strlen(lit);
        int pos = 0;
        int found = 0;
        cmark_node *insert_after = nodes[i];
        int last_end = 0;

        while (pos < len) {
            if (lit[pos] == '[') {
                char *html = NULL;
                int end = try_parse_span(lit, len, pos, &html);
                if (end >= 0 && html) {
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
                    /* HTML span */
                    cmark_node *html_node = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                    cmark_node_set_literal(html_node, html);
                    cmark_node_insert_after(insert_after, html_node);
                    insert_after = html_node;
                    free(html);
                    last_end = end;
                    pos = end;
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
            cmark_node_free(nodes[i]);
        }
    }
    free(nodes);
}
