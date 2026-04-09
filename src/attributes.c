/**
 * Presentation attributes ({.class #id key=value}) and
 * semantic annotations (|property key=value|).
 *
 * These attach to the immediately preceding link, image, or wiki link node.
 * Since cmark doesn't support arbitrary attributes on nodes, we convert
 * the element + modifiers into HTML_INLINE nodes with the correct markup.
 */

#include "attributes.h"
#include "annotation.h"
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Max number of attributes/annotations per element */
#define MAX_ATTRS 32

typedef struct {
    char *classes[MAX_ATTRS];
    int class_count;
    char *id;
    char *keys[MAX_ATTRS];
    char *values[MAX_ATTRS];
    int kv_count;
} parsed_attrs;

typedef struct {
    char *property;  /* Bare property name (first token) */
    char *keys[MAX_ATTRS];
    char *values[MAX_ATTRS];
    int kv_count;
} parsed_annotation;

static void free_attrs(parsed_attrs *a) {
    for (int i = 0; i < a->class_count; i++) free(a->classes[i]);
    free(a->id);
    for (int i = 0; i < a->kv_count; i++) { free(a->keys[i]); free(a->values[i]); }
}

static void free_annotation(parsed_annotation *a) {
    free(a->property);
    for (int i = 0; i < a->kv_count; i++) { free(a->keys[i]); free(a->values[i]); }
}

/**
 * Try to parse a {....} block from text starting at *pos.
 * Returns 1 on success, 0 on failure. Advances *pos past the }.
 */
static int parse_attr_block(const char *s, int len, int *pos, parsed_attrs *out) {
    if (*pos >= len || s[*pos] != '{') return 0;
    int start = *pos + 1;

    /* Find closing } (quote-aware) */
    int i = start;
    int in_quotes = 0;
    while (i < len) {
        if (in_quotes) {
            if (s[i] == '\\' && i + 1 < len) { i += 2; continue; }
            if (s[i] == '"') in_quotes = 0;
            i++; continue;
        }
        if (s[i] == '"') { in_quotes = 1; i++; continue; }
        if (s[i] == '}') break;
        if (s[i] == '\n') return 0; /* Can't span lines */
        i++;
    }
    if (i >= len || s[i] != '}') return 0;

    /* Parse tokens between { and } */
    int end = i;
    *pos = i + 1;

    int p = start;
    while (p < end) {
        while (p < end && s[p] == ' ') p++;
        if (p >= end) break;

        if (s[p] == '.') {
            /* Class: .classname */
            p++;
            int cs = p;
            while (p < end && s[p] != ' ' && s[p] != '}') p++;
            if (out->class_count < MAX_ATTRS) {
                out->classes[out->class_count] = strndup(s + cs, p - cs);
                out->class_count++;
            }
        } else if (s[p] == '#') {
            /* ID: #idname */
            p++;
            int cs = p;
            while (p < end && s[p] != ' ' && s[p] != '}') p++;
            free(out->id);
            out->id = strndup(s + cs, p - cs);
        } else {
            /* key=value or key="value" */
            int ks = p;
            while (p < end && s[p] != '=' && s[p] != ' ') p++;
            int ke = p;
            if (p < end && s[p] == '=') {
                p++;
                char *key = strndup(s + ks, ke - ks);
                char *val = NULL;
                if (p < end && s[p] == '"') {
                    p++;
                    int vs = p;
                    while (p < end && s[p] != '"') {
                        if (s[p] == '\\' && p + 1 < end) p++;
                        p++;
                    }
                    val = strndup(s + vs, p - vs);
                    if (p < end) p++; /* skip closing " */
                } else {
                    int vs = p;
                    while (p < end && s[p] != ' ') p++;
                    val = strndup(s + vs, p - vs);
                }
                if (out->kv_count < MAX_ATTRS) {
                    out->keys[out->kv_count] = key;
                    out->values[out->kv_count] = val;
                    out->kv_count++;
                } else {
                    free(key); free(val);
                }
            } else {
                /* Bare word — skip */
                while (p < end && s[p] != ' ') p++;
            }
        }
    }
    return 1;
}

/**
 * Try to parse a |....| annotation from text starting at *pos.
 * Returns 1 on success, 0 on failure. Advances *pos past the closing |.
 */
static int parse_annotation_block(const char *s, int len, int *pos, parsed_annotation *out) {
    if (*pos >= len || s[*pos] != '|') return 0;
    int start = *pos + 1;

    /* Find closing | (quote-aware) */
    int i = start;
    int in_quotes = 0;
    while (i < len) {
        if (in_quotes) {
            if (s[i] == '\\' && i + 1 < len) { i += 2; continue; }
            if (s[i] == '"') in_quotes = 0;
            i++; continue;
        }
        if (s[i] == '"') { in_quotes = 1; i++; continue; }
        if (s[i] == '|') break;
        if (s[i] == '\n') return 0;
        i++;
    }
    if (i >= len || s[i] != '|') return 0;

    int end = i;
    *pos = i + 1;

    /* Empty annotation ||  is a no-op */
    if (end == start) return 0;

    /* Parse tokens */
    int p = start;
    int first_token = 1;
    while (p < end) {
        while (p < end && s[p] == ' ') p++;
        if (p >= end) break;

        int ts = p;
        /* Check for key=value */
        while (p < end && s[p] != '=' && s[p] != ' ') p++;

        if (p < end && s[p] == '=') {
            int ke = p;
            p++;
            char *key = strndup(s + ts, ke - ts);
            char *val = NULL;
            if (p < end && s[p] == '"') {
                p++;
                int vs = p;
                while (p < end && s[p] != '"') {
                    if (s[p] == '\\' && p + 1 < end) p++;
                    p++;
                }
                val = strndup(s + vs, p - vs);
                if (p < end) p++;
            } else {
                int vs = p;
                while (p < end && s[p] != ' ') p++;
                val = strndup(s + vs, p - vs);
            }
            if (out->kv_count < MAX_ATTRS) {
                out->keys[out->kv_count] = key;
                out->values[out->kv_count] = val;
                out->kv_count++;
            } else {
                free(key); free(val);
            }
        } else {
            /* Bare word */
            char *word = strndup(s + ts, p - ts);
            if (first_token && !out->property) {
                out->property = word;
            } else {
                free(word);
            }
        }
        first_token = 0;
    }
    return 1;
}

/**
 * Check if a text node immediately follows a link/image and starts with { or |.
 * If so, parse the modifiers and rebuild the preceding element with attributes.
 */
void wm_attach_attributes(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    /* Collect (preceding_element, text_node) pairs to process */
    typedef struct { cmark_node *elem; cmark_node *text; } pair;
    pair *pairs = NULL;
    int count = 0, cap = 0;
    cmark_node *prev_elem = NULL;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);

        if (ev_type == CMARK_EVENT_EXIT &&
            (node->type == CMARK_NODE_LINK || node->type == CMARK_NODE_IMAGE)) {
            prev_elem = node;
            continue;
        }

        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT && prev_elem) {
            const char *lit = cmark_node_get_literal(node);
            if (lit && (lit[0] == '{' || lit[0] == '|')) {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 16;
                    pairs = realloc(pairs, cap * sizeof(pair));
                }
                pairs[count].elem = prev_elem;
                pairs[count].text = node;
                count++;
            }
            prev_elem = NULL;
            continue;
        }

        if (ev_type == CMARK_EVENT_ENTER || ev_type == CMARK_EVENT_EXIT) {
            prev_elem = NULL;
        }
    }
    cmark_iter_free(iter);

    /* Process collected pairs: parse modifiers from text node,
     * render the element + modifiers as HTML_INLINE, replace both nodes. */
    for (int i = 0; i < count; i++) {
        cmark_node *elem = pairs[i].elem;
        cmark_node *text = pairs[i].text;
        const char *lit = cmark_node_get_literal(text);
        if (!lit) continue;

        int len = (int)strlen(lit);
        int pos = 0;

        /* Parse all {..} and |..| blocks */
        parsed_attrs attrs;
        memset(&attrs, 0, sizeof(attrs));
        parsed_annotation annot;
        memset(&annot, 0, sizeof(annot));

        /* Parse consecutive {..} blocks (merged) */
        while (pos < len && lit[pos] == '{') {
            parsed_attrs more;
            memset(&more, 0, sizeof(more));
            if (!parse_attr_block(lit, len, &pos, &more)) break;
            /* Merge into attrs */
            for (int j = 0; j < more.class_count && attrs.class_count < MAX_ATTRS; j++)
                attrs.classes[attrs.class_count++] = more.classes[j];
            if (more.id) { free(attrs.id); attrs.id = more.id; }
            for (int j = 0; j < more.kv_count && attrs.kv_count < MAX_ATTRS; j++) {
                attrs.keys[attrs.kv_count] = more.keys[j];
                attrs.values[attrs.kv_count] = more.values[j];
                attrs.kv_count++;
            }
        }

        /* Parse |..| block */
        if (pos < len && lit[pos] == '|') {
            parse_annotation_block(lit, len, &pos, &annot);
        }

        /* Build HTML for the element with attributes */
        int is_link = (elem->type == CMARK_NODE_LINK);
        int is_image = (elem->type == CMARK_NODE_IMAGE);
        const char *url = cmark_node_get_url(elem);
        const char *title = is_image ? cmark_node_get_literal(cmark_node_first_child(elem)) : NULL;
        /* For links, get child text */
        char *link_text = NULL;
        if (is_link) {
            /* Render children to get display text — just grab first text child */
            cmark_node *child = cmark_node_first_child(elem);
            if (child && child->type == CMARK_NODE_TEXT) {
                link_text = strdup(cmark_node_get_literal(child));
            }
        }

        /* Build the HTML string */
        size_t html_cap = 1024;
        char *html = (char *)malloc(html_cap);
        size_t hlen = 0;

        #define APPEND(s) do { \
            size_t _l = strlen(s); \
            while (hlen + _l >= html_cap) { html_cap *= 2; html = realloc(html, html_cap); } \
            memcpy(html + hlen, s, _l); hlen += _l; \
        } while(0)

        /* Standalone images (sole content of paragraph) become <figure>.
         * This follows Pandoc's convention — no magic class names needed. */
        int is_figure = 0;
        if (is_image) {
            cmark_node *parent = cmark_node_parent(elem);
            if (parent && parent->type == CMARK_NODE_PARAGRAPH) {
                /* Check if image + text node are the only children */
                cmark_node *first = cmark_node_first_child(parent);
                cmark_node *after_elem = cmark_node_next(elem);
                /* Standalone: image is first child, only followed by the modifier text */
                if (first == elem && (!after_elem || after_elem == text)) {
                    is_figure = 1;
                }
            }
        }

        const char *alt = "";
        if (is_image) {
            cmark_node *alt_node = cmark_node_first_child(elem);
            if (alt_node) alt = cmark_node_get_literal(alt_node);
            if (!alt) alt = "";
        }

        /* Determine caption for figures */
        const char *caption = NULL;
        if (is_figure) {
            /* Check for explicit caption= attribute */
            for (int j = 0; j < attrs.kv_count; j++) {
                if (strcmp(attrs.keys[j], "caption") == 0) {
                    caption = attrs.values[j];
                    break;
                }
            }
            if (!caption) caption = alt; /* Fall back to alt text */
        }

        if (is_figure) {
            /* <figure class="thumb ..."><img ... /><figcaption>...</figcaption></figure> */
            APPEND("<figure");
            /* Classes on the figure */
            if (attrs.class_count > 0) {
                APPEND(" class=\"");
                for (int j = 0; j < attrs.class_count; j++) {
                    if (j > 0) APPEND(" ");
                    APPEND(attrs.classes[j]);
                }
                APPEND("\"");
            }
            /* Semantic annotations on figure */
            if (annot.property) {
                APPEND(" data-wm-property=\""); APPEND(annot.property); APPEND("\"");
            }
            for (int j = 0; j < annot.kv_count; j++) {
                APPEND(" data-wm-"); APPEND(annot.keys[j]); APPEND("=\"");
                APPEND(annot.values[j]); APPEND("\"");
            }
            APPEND(">");
            /* <img> inside figure */
            APPEND("<img src=\"");
            if (url) APPEND(url);
            APPEND("\" alt=\""); APPEND(alt); APPEND("\"");
            /* Non-class, non-caption attributes on img */
            if (attrs.id) { APPEND(" id=\""); APPEND(attrs.id); APPEND("\""); }
            for (int j = 0; j < attrs.kv_count; j++) {
                if (strcmp(attrs.keys[j], "caption") == 0) continue;
                APPEND(" "); APPEND(attrs.keys[j]); APPEND("=\"");
                APPEND(attrs.values[j]); APPEND("\"");
            }
            APPEND(" />");
            APPEND("<figcaption>");
            if (caption) APPEND(caption);
            APPEND("</figcaption></figure>");
        } else if (is_image) {
            APPEND("<img src=\"");
            if (url) APPEND(url);
            APPEND("\" alt=\""); APPEND(alt); APPEND("\"");
            if (attrs.class_count > 0) {
                APPEND(" class=\"");
                for (int j = 0; j < attrs.class_count; j++) {
                    if (j > 0) APPEND(" ");
                    APPEND(attrs.classes[j]);
                }
                APPEND("\"");
            }
            if (attrs.id) { APPEND(" id=\""); APPEND(attrs.id); APPEND("\""); }
            for (int j = 0; j < attrs.kv_count; j++) {
                APPEND(" "); APPEND(attrs.keys[j]); APPEND("=\"");
                APPEND(attrs.values[j]); APPEND("\"");
            }
            if (annot.property) {
                APPEND(" data-wm-property=\""); APPEND(annot.property); APPEND("\"");
            }
            for (int j = 0; j < annot.kv_count; j++) {
                APPEND(" data-wm-"); APPEND(annot.keys[j]); APPEND("=\"");
                APPEND(annot.values[j]); APPEND("\"");
            }
            APPEND(" />");
        } else if (is_link) {
            APPEND("<a href=\"");
            if (url) APPEND(url);
            APPEND("\"");
            if (attrs.class_count > 0) {
                APPEND(" class=\"");
                for (int j = 0; j < attrs.class_count; j++) {
                    if (j > 0) APPEND(" ");
                    APPEND(attrs.classes[j]);
                }
                APPEND("\"");
            }
            if (attrs.id) { APPEND(" id=\""); APPEND(attrs.id); APPEND("\""); }
            for (int j = 0; j < attrs.kv_count; j++) {
                APPEND(" "); APPEND(attrs.keys[j]); APPEND("=\"");
                APPEND(attrs.values[j]); APPEND("\"");
            }
            if (annot.property) {
                APPEND(" data-wm-property=\""); APPEND(annot.property); APPEND("\"");
            }
            for (int j = 0; j < annot.kv_count; j++) {
                APPEND(" data-wm-"); APPEND(annot.keys[j]); APPEND("=\"");
                APPEND(annot.values[j]); APPEND("\"");
            }
            APPEND(">");
            if (link_text) APPEND(link_text);
            APPEND("</a>");
        }

        html[hlen] = '\0';

        #undef APPEND

        /* Replace the element with the rendered HTML.
         * For figures: if the image is the only content in a paragraph,
         * replace the paragraph with HTML_BLOCK to avoid <p><figure>...</p>. */
        if (is_figure) {
            cmark_node *parent = cmark_node_parent(elem);
            /* Check if elem + text node are the only children */
            cmark_node *first = cmark_node_first_child(parent);
            cmark_node *second = first ? cmark_node_next(first) : NULL;
            cmark_node *third = second ? cmark_node_next(second) : NULL;
            int only_child = (parent && parent->type == CMARK_NODE_PARAGRAPH &&
                              first == elem && (!third || (second == text && !cmark_node_next(third ? third : second))));

            if (only_child) {
                /* Append newline for block-level output */
                if (hlen + 2 >= html_cap) { html_cap *= 2; html = realloc(html, html_cap); }
                html[hlen++] = '\n';
                html[hlen] = '\0';

                cmark_node *block = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
                cmark_node_set_literal(block, html);
                cmark_node_insert_before(parent, block);
                cmark_node_free(parent); /* Frees paragraph + all children */
                /* Skip text node cleanup since parent was freed */
                free(html);
                free(link_text);
                free_attrs(&attrs);
                free_annotation(&annot);
                continue;
            }
        }

        cmark_node *html_node = cmark_node_new_with_mem(CMARK_NODE_HTML_INLINE, mem);
        cmark_node_set_literal(html_node, html);
        cmark_node_insert_before(elem, html_node);
        cmark_node_free(elem);

        /* Update the text node: remove consumed modifiers, keep remainder */
        if (pos < len) {
            char *remaining = strndup(lit + pos, len - pos);
            cmark_node_set_literal(text, remaining);
            free(remaining);
        } else {
            cmark_node_free(text);
        }

        free(html);
        free(link_text);
        free_attrs(&attrs);
        free_annotation(&annot);
    }

    free(pairs);
}

/* wm_attach_annotations is now in annotation.c */
