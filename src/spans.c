/**
 * Span processing: [text]{.class key=val} and [text]|property key=val|
 *
 * These are text-node patterns where cmark left bare [text] as literal.
 * We detect [text] followed by { or | and convert to HTML spans.
 */

#include "spans.h"
#include "attributes.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Parse consecutive {..} and |..| modifier blocks starting at position p
 * in string s of length len. Fills attrs and annot. Returns new position
 * past all consumed modifiers (unchanged if no modifiers found).
 */
static int parse_modifiers(const char *s, int len, int p,
                           wm_parsed_attrs *attrs, wm_parsed_annotation *annot) {
    while (p < len && s[p] == '{') {
        if (!wm_parse_attr_block(s, len, &p, attrs)) break;
    }
    if (p < len && s[p] == '|') {
        wm_parse_annotation_block(s, len, &p, annot);
    }
    return p;
}

/**
 * Build an opening <span ...> tag from parsed attrs/annotation.
 * Returns malloc'd string. Caller must free.
 */
static char *build_span_open_tag(const wm_parsed_attrs *attrs,
                                  const wm_parsed_annotation *annot) {
    /* Calculate required size */
    size_t cap = 32;
    for (int i = 0; i < attrs->class_count; i++)
        cap += strlen(attrs->classes[i]) + 1;
    if (attrs->id) cap += strlen(attrs->id) + 8;
    for (int i = 0; i < attrs->kv_count; i++)
        cap += strlen(attrs->keys[i]) + strlen(attrs->values[i]) + 5;
    if (annot->property) cap += strlen(annot->property) + 22;
    for (int i = 0; i < annot->kv_count; i++)
        cap += strlen(annot->keys[i]) + strlen(annot->values[i]) + 12;

    char *tag = (char *)malloc(cap);
    if (!tag) return NULL;
    size_t n = 0;

    n += snprintf(tag + n, cap - n, "<span");

    if (attrs->class_count > 0) {
        n += snprintf(tag + n, cap - n, " class=\"");
        for (int i = 0; i < attrs->class_count; i++) {
            if (i > 0) n += snprintf(tag + n, cap - n, " ");
            n += snprintf(tag + n, cap - n, "%s", attrs->classes[i]);
        }
        n += snprintf(tag + n, cap - n, "\"");
    }
    if (attrs->id)
        n += snprintf(tag + n, cap - n, " id=\"%s\"", attrs->id);
    for (int i = 0; i < attrs->kv_count; i++)
        n += snprintf(tag + n, cap - n, " %s=\"%s\"", attrs->keys[i], attrs->values[i]);
    if (annot->property)
        n += snprintf(tag + n, cap - n, " data-wm-property=\"%s\"", annot->property);
    for (int i = 0; i < annot->kv_count; i++)
        n += snprintf(tag + n, cap - n, " data-wm-%s=\"%s\"", annot->keys[i], annot->values[i]);

    n += snprintf(tag + n, cap - n, ">");
    return tag;
}

/**
 * Build a complete <span ...>text</span> for a single-node span.
 * Returns malloc'd string. Caller must free.
 */
static char *build_span_html(const char *text, int text_len,
                              const wm_parsed_attrs *attrs,
                              const wm_parsed_annotation *annot) {
    int is_silent = (text_len == 0 && (annot->property || annot->kv_count > 0));

    if (is_silent) {
        size_t cap = 64;
        if (annot->property) cap += strlen(annot->property) + 22;
        for (int i = 0; i < annot->kv_count; i++)
            cap += strlen(annot->keys[i]) + strlen(annot->values[i]) + 12;
        if (attrs->id) cap += strlen(attrs->id) + 8;

        char *html = (char *)malloc(cap);
        if (!html) return NULL;
        size_t n = 0;
        n += snprintf(html + n, cap - n, "<span");
        if (annot->property)
            n += snprintf(html + n, cap - n, " data-wm-property=\"%s\"", annot->property);
        for (int i = 0; i < annot->kv_count; i++)
            n += snprintf(html + n, cap - n, " data-wm-%s=\"%s\"", annot->keys[i], annot->values[i]);
        if (attrs->id)
            n += snprintf(html + n, cap - n, " id=\"%s\"", attrs->id);
        n += snprintf(html + n, cap - n, " class=\"wm-silent\"></span>");
        return html;
    }

    char *open = build_span_open_tag(attrs, annot);
    if (!open) return NULL;

    size_t cap = strlen(open) + text_len + 16;
    char *html = (char *)malloc(cap);
    if (!html) { free(open); return NULL; }
    snprintf(html, cap, "%s%.*s</span>", open, text_len, text);
    free(open);
    return html;
}

/**
 * Try to parse a [text]{...} or [text]|...| span in a single text node.
 * Returns position past the construct, or -1 if no match.
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

    if (after_bracket >= len) return -1;
    if (s[after_bracket] != '{' && s[after_bracket] != '|') return -1;

    wm_parsed_attrs attrs;
    memset(&attrs, 0, sizeof(attrs));
    wm_parsed_annotation annot;
    memset(&annot, 0, sizeof(annot));

    int p = parse_modifiers(s, len, after_bracket, &attrs, &annot);
    if (p == after_bracket) {
        wm_free_attrs(&attrs);
        wm_free_annotation(&annot);
        return -1;
    }

    *html_out = build_span_html(s + text_start, text_len, &attrs, &annot);
    wm_free_attrs(&attrs);
    wm_free_annotation(&annot);
    return *html_out ? p : -1;
}

/**
 * Try to find a multi-node span: [text with **formatting**]{.class}
 * where cmark split the content across multiple AST nodes.
 */
static int try_multinode_span(cmark_node *text_node, int bracket_pos,
                               cmark_mem *mem) {
    const char *lit = cmark_node_get_literal(text_node);
    if (!lit) return 0;

    /* Walk forward through siblings looking for ]{ or ]| in a text node */
    cmark_node *sibling = cmark_node_next(text_node);
    cmark_node *close_node = NULL;
    int close_bracket_pos = -1;

    while (sibling) {
        if (sibling->type == CMARK_NODE_TEXT) {
            const char *slit = cmark_node_get_literal(sibling);
            if (slit) {
                for (int si = 0; slit[si]; si++) {
                    if (slit[si] == ']' && (slit[si+1] == '{' || slit[si+1] == '|')) {
                        close_node = sibling;
                        close_bracket_pos = si;
                        break;
                    }
                }
                if (close_node) break;
            }
        }
        sibling = cmark_node_next(sibling);
    }

    if (!close_node) return 0;

    const char *close_lit = cmark_node_get_literal(close_node);
    int close_len = (int)strlen(close_lit);

    /* Parse modifiers using shared parsers */
    wm_parsed_attrs attrs;
    memset(&attrs, 0, sizeof(attrs));
    wm_parsed_annotation annot;
    memset(&annot, 0, sizeof(annot));

    int p = parse_modifiers(close_lit, close_len, close_bracket_pos + 1, &attrs, &annot);
    if (p <= close_bracket_pos + 1) {
        wm_free_attrs(&attrs);
        wm_free_annotation(&annot);
        return 0;
    }

    char *open_tag = build_span_open_tag(&attrs, &annot);
    wm_free_attrs(&attrs);
    wm_free_annotation(&annot);
    if (!open_tag) return 0;

    /* Restructure the AST:
     * 1. Trim "[" from text_node
     * 2. Insert <span> opening tag
     * 3. Insert remaining text after "[" as content
     * 4. Content nodes between text_node and close_node stay as-is
     * 5. Insert content before "]" from close_node, then </span>
     * 6. Keep any text after modifiers in close_node */

    int lit_len = (int)strlen(lit);

    if (bracket_pos > 0) {
        char *before = (char *)mem->calloc(1, bracket_pos + 1);
        if (before) {
            memcpy(before, lit, bracket_pos);
            cmark_node_set_literal(text_node, before);
            mem->free(before);
        }
    }

    cmark_node *open_node = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
    cmark_node_set_literal(open_node, open_tag);

    int after_bracket = bracket_pos + 1;
    if (bracket_pos > 0) {
        cmark_node_insert_after(text_node, open_node);
    } else {
        cmark_node_insert_before(text_node, open_node);
    }

    if (after_bracket < lit_len) {
        cmark_node *content_text = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
        char *ct = (char *)mem->calloc(1, lit_len - after_bracket + 1);
        if (ct) {
            memcpy(ct, lit + after_bracket, lit_len - after_bracket);
            cmark_node_set_literal(content_text, ct);
            mem->free(ct);
        }
        cmark_node_insert_after(open_node, content_text);
    }

    if (close_bracket_pos > 0) {
        cmark_node *pre_close = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
        char *pc = (char *)mem->calloc(1, close_bracket_pos + 1);
        if (pc) {
            memcpy(pc, close_lit, close_bracket_pos);
            cmark_node_set_literal(pre_close, pc);
            mem->free(pc);
        }
        cmark_node_insert_before(close_node, pre_close);
    }

    cmark_node *close_tag_node = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
    cmark_node_set_literal(close_tag_node, "</span>");
    cmark_node_insert_before(close_node, close_tag_node);

    if (p < close_len) {
        char *remaining = (char *)mem->calloc(1, close_len - p + 1);
        if (remaining) {
            memcpy(remaining, close_lit + p, close_len - p);
            cmark_node_set_literal(close_node, remaining);
            mem->free(remaining);
        }
    } else {
        cmark_node_free(close_node);
    }

    if (bracket_pos == 0) {
        cmark_node_free(text_node);
    }

    free(open_tag);
    return 1;
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
            if (lit && strchr(lit, '[')) {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 16;
                    cmark_node **tmp = realloc(nodes, cap * sizeof(cmark_node *));
                    if (!tmp) break;
                    nodes = tmp;
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
                    if (pos > last_end) {
                        cmark_node *before = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                        char *t = (char *)mem->calloc(1, pos - last_end + 1);
                        if (t) {
                            memcpy(t, lit + last_end, pos - last_end);
                            cmark_node_set_literal(before, t);
                            mem->free(t);
                        }
                        cmark_node_insert_after(insert_after, before);
                        insert_after = before;
                    }
                    cmark_node *html_node = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
                    cmark_node_set_literal(html_node, html);
                    cmark_node_insert_after(insert_after, html_node);
                    insert_after = html_node;
                    free(html);
                    last_end = end;
                    pos = end;
                    continue;
                }
                if (try_multinode_span(nodes[i], pos, mem)) {
                    found = 2;
                    break;
                }
            }
            pos++;
        }

        if (found == 1) {
            lit = cmark_node_get_literal(nodes[i]);
            if (!lit) continue;
            len = (int)strlen(lit);
            if (last_end > 0 && last_end < len) {
                cmark_node *after = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                char *t = (char *)mem->calloc(1, len - last_end + 1);
                if (t) {
                    memcpy(t, lit + last_end, len - last_end);
                    cmark_node_set_literal(after, t);
                    mem->free(t);
                }
                cmark_node_insert_after(insert_after, after);
            }
            if (last_end > 0) {
                cmark_node_free(nodes[i]);
            }
        }
    }
    free(nodes);
}
