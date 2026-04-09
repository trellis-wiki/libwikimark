/**
 * WikiMark wiki link extension for cmark-gfm.
 *
 * Since cmark-gfm's inline parser handles `[` directly (not via extensions),
 * wiki links are detected in the postprocess phase: after cmark finishes
 * inline parsing, we walk the AST looking for text nodes containing `[[...]]`
 * and replace them with CMARK_NODE_WM_WIKILINK nodes.
 *
 * This is robust because cmark always falls back to literal text when `[[`
 * doesn't match a standard link — so `[[Main Page]]` becomes a text node
 * containing the literal string "[[Main Page]]".
 */

#include "wikilink.h"
#include "node_types.h"
#include "normalize.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <node.h>
#include <parser.h>

#include <string.h>
#include <stdlib.h>

/* Node type — assigned at runtime */
cmark_node_type CMARK_NODE_WM_WIKILINK;

/* --- Forbidden characters in wiki link targets (spec §13.6) --- */

static int is_forbidden_in_target(unsigned char c) {
    return c == '#' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '|' || c == '\n' || c == '\r';
}

/* --- Postprocess: find [[...]] in text nodes and convert to wiki links --- */

/**
 * Try to parse a wiki link at position `pos` in the text `s` of length `len`.
 * `s[pos]` and `s[pos+1]` must be '['.
 * Returns the position past the closing `]]`, or -1 if no valid wiki link.
 * On success, sets *target_start and *target_len.
 */
static int try_parse_wikilink(const char *s, int len, int pos,
                               int *target_start, int *target_len) {
    /* pos points to the first '[', pos+1 to the second '[' */
    if (pos + 1 >= len || s[pos] != '[' || s[pos + 1] != '[')
        return -1;

    int start = pos + 2; /* past the [[ */

    /* Scan for ]] */
    for (int i = start; i < len - 1; i++) {
        if (s[i] == ']' && s[i + 1] == ']') {
            int tlen = i - start;
            if (tlen <= 0)
                return -1; /* empty [[ ]] */

            /* Check for forbidden chars */
            for (int j = start; j < i; j++) {
                if (is_forbidden_in_target((unsigned char)s[j]))
                    return -1;
            }

            *target_start = start;
            *target_len = tlen;
            return i + 2; /* past the ]] */
        }
        /* Wiki links don't span lines */
        if (s[i] == '\n' || s[i] == '\r')
            return -1;
    }

    return -1; /* no closing ]] */
}

/**
 * Build the display text for a wiki link target.
 * Trims whitespace, collapses internal whitespace, replaces underscores with spaces.
 * Returns a newly allocated string.
 */
static char *make_display_text(const char *target, int target_len, cmark_mem *mem) {
    char *display = (char *)mem->calloc(1, target_len + 1);
    const char *src = target;
    const char *end = target + target_len;
    char *dst = display;

    /* Skip leading whitespace */
    while (src < end && (*src == ' ' || *src == '\t')) src++;
    /* Trim trailing whitespace */
    while (end > src && (end[-1] == ' ' || end[-1] == '\t')) end--;

    int prev_space = 0;
    while (src < end) {
        if (*src == ' ' || *src == '\t' || *src == '_') {
            if (!prev_space) {
                *dst++ = ' ';
                prev_space = 1;
            }
        } else {
            *dst++ = *src;
            prev_space = 0;
        }
        src++;
    }
    *dst = '\0';
    return display;
}

/**
 * Create a wiki link as a standard CMARK_NODE_LINK.
 *
 * We use the standard link node type so it's accepted everywhere links are
 * (including inside table cells, which have a hardcoded child type allowlist).
 * The URL is set to the normalized wiki target.
 */
static cmark_node *make_wikilink_node(cmark_syntax_extension *ext,
                                       cmark_mem *mem,
                                       const char *target, int target_len) {
    /* Copy target to null-terminated buffer */
    char *target_buf = (char *)mem->calloc(1, target_len + 1);
    memcpy(target_buf, target, target_len);
    target_buf[target_len] = '\0';

    char *normalized = wikimark_normalize_title(target_buf, 0, mem);
    if (!normalized) {
        mem->free(target_buf);
        return NULL;
    }

    cmark_node *link = cmark_node_new_with_mem(CMARK_NODE_LINK, mem);
    cmark_node_set_url(link, normalized);

    /* Create display text child */
    char *display = make_display_text(target, target_len, mem);
    cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
    cmark_node_set_literal(text, display);
    cmark_node_append_child(link, text);

    mem->free(target_buf);
    mem->free(normalized);
    mem->free(display);
    return link;
}

/**
 * Process a single text node: split it on [[...]] patterns, inserting
 * wiki link nodes as siblings.
 */
static void process_text_node(cmark_syntax_extension *ext,
                               cmark_node *text_node, cmark_mem *mem) {
    const char *literal = cmark_node_get_literal(text_node);
    if (!literal)
        return;

    int len = (int)strlen(literal);
    int pos = 0;
    int found = 0;

    /* First pass: check if there are any [[ at all */
    for (int i = 0; i < len - 1; i++) {
        if (literal[i] == '[' && literal[i + 1] == '[') {
            found = 1;
            break;
        }
    }
    if (!found)
        return;

    /* Second pass: split the text node */
    cmark_node *insert_after = text_node;
    int last_end = 0;

    pos = 0;
    while (pos < len - 1) {
        if (literal[pos] == '[' && literal[pos + 1] == '[') {
            int target_start, target_len;
            int end = try_parse_wikilink(literal, len, pos, &target_start, &target_len);
            if (end >= 0) {
                /* Text before the wiki link */
                if (pos > last_end) {
                    cmark_node *before = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
                    char *before_text = (char *)mem->calloc(1, pos - last_end + 1);
                    memcpy(before_text, literal + last_end, pos - last_end);
                    before_text[pos - last_end] = '\0';
                    cmark_node_set_literal(before, before_text);
                    mem->free(before_text);
                    cmark_node_insert_after(insert_after, before);
                    insert_after = before;
                }

                /* The wiki link node */
                cmark_node *wl = make_wikilink_node(ext, mem,
                    literal + target_start, target_len);
                cmark_node_insert_after(insert_after, wl);
                insert_after = wl;

                last_end = end;
                pos = end;
                continue;
            }
        }
        pos++;
    }

    /* Remaining text after the last wiki link */
    if (last_end > 0) {
        if (last_end < len) {
            cmark_node *after = cmark_node_new_with_mem(CMARK_NODE_TEXT, mem);
            char *after_text = (char *)mem->calloc(1, len - last_end + 1);
            memcpy(after_text, literal + last_end, len - last_end);
            after_text[len - last_end] = '\0';
            cmark_node_set_literal(after, after_text);
            mem->free(after_text);
            cmark_node_insert_after(insert_after, after);
        }

        /* Remove the original text node */
        cmark_node_free(text_node);
    }
}

/**
 * Check if a URL is a wiki target (no protocol scheme, no special prefix).
 *
 * A link target is a wiki link if it does NOT match any of:
 * - Has an RFC 3986 scheme (letters followed by ":")
 * - Starts with # (anchor)
 * - Starts with / (absolute path)
 * - Starts with ./ or ../ (relative path)
 */
static int is_wiki_url(const char *url) {
    if (!url || !*url)
        return 0;

    /* Starts with # — anchor link */
    if (url[0] == '#')
        return 0;

    /* Starts with / — absolute path */
    if (url[0] == '/')
        return 0;

    /* Starts with ./ or ../ — relative path */
    if (url[0] == '.') {
        if (url[1] == '/')
            return 0;
        if (url[1] == '.' && url[2] == '/')
            return 0;
    }

    /* Check for RFC 3986 scheme: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":" */
    const char *p = url;
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
        p++;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.') {
            p++;
        }
        if (*p == ':') {
            /* Has a scheme — not a wiki link */
            return 0;
        }
    }

    /* No scheme, no special prefix — treat as wiki page name */
    return 1;
}

/**
 * Rewrite standard Markdown links that have wiki-style targets.
 * [text](Page Name) where "Page Name" has no protocol → normalize the URL.
 */
static void rewrite_wiki_style_links(cmark_node *root, cmark_mem *mem) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;

    /* Collect link nodes to rewrite */
    cmark_node **links = NULL;
    int link_count = 0;
    int link_cap = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_LINK) {
            const char *url = cmark_node_get_url(node);
            if (is_wiki_url(url)) {
                if (link_count >= link_cap) {
                    link_cap = link_cap ? link_cap * 2 : 16;
                    links = (cmark_node **)realloc(links, link_cap * sizeof(cmark_node *));
                }
                links[link_count++] = node;
            }
        }
    }
    cmark_iter_free(iter);

    /* Normalize URLs for collected wiki-style links */
    for (int i = 0; i < link_count; i++) {
        const char *url = cmark_node_get_url(links[i]);
        char *normalized = wikimark_normalize_title(url, 0, mem);
        if (normalized) {
            cmark_node_set_url(links[i], normalized);
            mem->free(normalized);
        }
    }

    free(links);
}

static cmark_node *postprocess(cmark_syntax_extension *ext,
                                cmark_parser *parser, cmark_node *root) {
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;
    cmark_mem *mem = parser->mem;

    /* --- Pass 1: Find [[...]] in text nodes and convert to wiki links --- */

    cmark_node **nodes = NULL;
    int node_count = 0;
    int node_cap = 0;
    int inside_link = 0;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);

        /* Track whether we're inside a GFM link — wiki links are forbidden there
         * (spec §7.10: would produce nested <a> elements) */
        if (node->type == CMARK_NODE_LINK || node->type == CMARK_NODE_IMAGE) {
            if (ev_type == CMARK_EVENT_ENTER) inside_link++;
            else if (ev_type == CMARK_EVENT_EXIT) inside_link--;
        }

        if (ev_type == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT
            && !inside_link) {
            const char *lit = cmark_node_get_literal(node);
            if (lit && strstr(lit, "[[")) {
                if (node_count >= node_cap) {
                    node_cap = node_cap ? node_cap * 2 : 16;
                    nodes = (cmark_node **)realloc(nodes, node_cap * sizeof(cmark_node *));
                }
                nodes[node_count++] = node;
            }
        }
    }
    cmark_iter_free(iter);

    for (int i = 0; i < node_count; i++) {
        process_text_node(ext, nodes[i], mem);
    }
    free(nodes);

    /* --- Pass 2: Rewrite wiki-style Markdown links [text](Page) ---
     * DISABLED: Cannot reliably distinguish wiki targets from standard
     * relative URLs. All [text](url) links are standard Markdown for now.
     * Wiki links use [[...]] syntax exclusively.
     * TODO: Consider a wiki: URI scheme prefix for explicit wiki-style links.
     */

    return root;
}

/* --- Extension creation --- */

cmark_syntax_extension *create_wikilink_extension(void) {
    cmark_syntax_extension *ext = cmark_syntax_extension_new("wikilink");

    cmark_syntax_extension_set_postprocess_func(ext, postprocess);

    CMARK_NODE_WM_WIKILINK = cmark_syntax_extension_add_node(1); /* 1 = inline */

    return ext;
}
