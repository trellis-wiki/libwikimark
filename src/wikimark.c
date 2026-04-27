/**
 * libwikimark — main registration and convenience API.
 */

#include "wikimark.h"
#include "wikilink.h"
#include "node_types.h"
#include "wm_private.h"
#include "frontmatter.h"
#include "normalize.h"
#include "variable.h"
#include "callout.h"
#include "redirect.h"
#include "template.h"
#include "attributes.h"
#include "annotation.h"
#include "spans.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
#include <parser.h>
#include <node.h>
#include <registry.h>
#include <plugin.h>

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* Thread-local per-parse state — see wm_private.h */
WM_THREAD_LOCAL wm_parse_state *wm_current_parse_state = NULL;

/* --- Registration --- */

static int wikimark_registration(cmark_plugin *plugin) {
    cmark_plugin_register_syntax_extension(plugin, create_wikilink_extension());
    return 1;
}

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void do_register(void) {
    cmark_gfm_core_extensions_ensure_registered();
    cmark_register_plugin(wikimark_registration);
}

void wikimark_extensions_ensure_registered(void) {
    pthread_once(&once_control, do_register);
}

/* --- Configuration --- */

wikimark_config wikimark_config_default(void) {
    wikimark_config config;
    memset(&config, 0, sizeof(config));
    config.base_url = "";
    config.interwiki = NULL;
    config.interwiki_count = 0;
    /* §15.2 defaults */
    config.max_template_expansions = 500;
    config.max_expansion_output = 2 * 1024 * 1024;   /* 2 MB */
    config.max_render_output = 10 * 1024 * 1024;      /* 10 MB */
    return config;
}

wikimark_context wikimark_context_default(void) {
    wikimark_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    return ctx;
}

/* --- Default variable resolver (frontmatter only) --- */

typedef struct {
    const wm_fm_node *frontmatter;
} default_resolver_data;

static const char *default_resolve_variable(const char *path, void *user_data) {
    default_resolver_data *d = (default_resolver_data *)user_data;
    if (!d || !d->frontmatter) return NULL;
    return wm_frontmatter_get(d->frontmatter, path);
}

/* --- Core render function --- */

static char *do_convert(const char *text, size_t len, int options,
                        const wikimark_config *config,
                        const wikimark_context *context) {
    wikimark_extensions_ensure_registered();
    cmark_mem *mem = cmark_get_default_mem_allocator();

    /* === Phase 1: Frontmatter extraction === */
    size_t body_start = 0;
    wm_fm_node *frontmatter = wm_frontmatter_parse(text, len, &body_start);
    const char *body = text + body_start;
    size_t body_len = len - body_start;

    /* Check for redirect (frontmatter-only) — skip rendering if redirect */
    if (frontmatter) {
        const char *redirect_target = wm_frontmatter_get(frontmatter, "redirect");
        if (redirect_target) {
            char *normalized = wikimark_normalize_title(
                redirect_target, 0, mem);
            if (normalized) {
                size_t html_len = strlen(redirect_target) + strlen(normalized) + 100;
                char *html = (char *)mem->calloc(1, html_len);
                snprintf(html, html_len,
                    "<p>Redirecting to <a href=\"%s\">%s</a>.</p>\n",
                    normalized, redirect_target);
                mem->free(normalized);
                wm_frontmatter_free(frontmatter);
                return html;
            }
        }
    }

    /* Promote inputs defaults to top-level keys so ${name} resolves
     * when rendering the page standalone (not via a template call). */
    wm_frontmatter_promote_input_defaults(frontmatter);

    /* === Variable expansion (pre-parse, from page frontmatter) === */
    /* Variables always resolve from the page's own frontmatter.
     * This is a text substitution before cmark sees the document. */
    default_resolver_data var_data;
    var_data.frontmatter = frontmatter;
    wikimark_context var_ctx = wikimark_context_default();
    var_ctx.resolve_variable = default_resolve_variable;
    var_ctx.user_data = &var_data;

    /* Expand ${...} variables in the body text before parsing.
     * This is a text-level substitution so the parser sees the resolved values.
     * Always uses the page's own frontmatter, not the engine context. */
    char *expanded_body = wm_expand_variables_str(body, body_len, &var_ctx);
    if (expanded_body) {
        body = expanded_body;
        body_len = strlen(expanded_body);
    }

    /* === Pre-process: protect escapes and template interiors from parsing ===
     *
     * Three concerns handled by a single scan:
     *
     * 1. Escape sequences \[\[ and \{\{ become U+E000 / U+E001 sentinels so
     *    cmark-gfm doesn't see them as literal brackets. Restored after the
     *    template / span postprocessors have run.
     *
     * 2. Unescaped `{{...}}` template calls are detected with balanced-
     *    lookahead. Only when a matching `}}` is found (respecting nesting)
     *    do we rewrite the interior. Unbalanced `{{` passes through as
     *    literal text.
     *
     *    Inside a balanced template region:
     *      `[` → U+E004 sentinel  (so the wikilink postprocessor can't
     *                              consume [[link]] inside an arg value —
     *                              fixes T9.1)
     *      `]` → U+E005 sentinel  (same)
     *      `\n` → space           (lets a template call span source lines
     *                              without being broken into multiple
     *                              paragraphs by cmark-gfm)
     *
     *    The template postprocessor restores U+E004 / U+E005 before
     *    handing the args string to the engine callback. Newlines don't
     *    need to be restored — they were inter-argument whitespace, and
     *    the arg parser already treats any whitespace as a separator.
     */
    char *preprocessed = NULL;
    {
        char *buf = (char *)malloc(body_len * 3 / 2 + 4);
        size_t j = 0;
        int changed = 0;
        if (buf) {
            for (size_t i = 0; i < body_len; ) {
                /* Escape handling */
                if (i + 3 < body_len && body[i] == '\\' && body[i+2] == '\\') {
                    if (body[i+1] == '[' && body[i+3] == '[') {
                        buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x80;
                        buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x80;
                        i += 4; changed = 1; continue;
                    }
                    if (body[i+1] == '{' && body[i+3] == '{') {
                        buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x81;
                        buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x81;
                        i += 4; changed = 1; continue;
                    }
                }
                /* Balanced-template detection: when we see `{{`, scan
                 * forward for the matching `}}` respecting nesting. Only
                 * rewrite the interior if balanced. */
                if (i + 1 < body_len && body[i] == '{' && body[i+1] == '{') {
                    /* Find matching `}}` */
                    size_t depth = 1;
                    size_t k = i + 2;
                    size_t close = (size_t)-1;
                    while (k + 1 < body_len) {
                        if (body[k] == '{' && body[k+1] == '{') {
                            depth++; k += 2; continue;
                        }
                        if (body[k] == '}' && body[k+1] == '}') {
                            depth--;
                            if (depth == 0) { close = k; break; }
                            k += 2; continue;
                        }
                        k++;
                    }
                    if (close != (size_t)-1) {
                        /* Emit `{{` verbatim */
                        buf[j++] = body[i];
                        buf[j++] = body[i+1];
                        /* Interior with sentinel rewrites */
                        for (size_t p = i + 2; p < close; p++) {
                            char c = body[p];
                            if (c == '[') {
                                buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x84;
                                changed = 1;
                            } else if (c == ']') {
                                buf[j++] = (char)0xEE; buf[j++] = (char)0x80; buf[j++] = (char)0x85;
                                changed = 1;
                            } else if (c == '\n' || c == '\r') {
                                buf[j++] = ' ';
                                changed = 1;
                            } else {
                                buf[j++] = c;
                            }
                        }
                        /* Emit `}}` verbatim */
                        buf[j++] = body[close];
                        buf[j++] = body[close+1];
                        i = close + 2;
                        continue;
                    }
                    /* Unbalanced {{: fall through, emit literal. */
                }
                buf[j++] = body[i++];
            }
            if (changed) {
                preprocessed = buf;
                body = preprocessed;
                body_len = j;
            } else {
                free(buf);
            }
        }
    }

    /* === Phase 2+3: Parse with cmark-gfm === */
    cmark_parser *parser = cmark_parser_new(options);

    static const char *gfm_exts[] = {
        "table", "strikethrough", "autolink", "tagfilter", "tasklist", NULL
    };
    for (int i = 0; gfm_exts[i]; i++) {
        cmark_syntax_extension *ext = cmark_find_syntax_extension(gfm_exts[i]);
        if (ext)
            cmark_parser_attach_syntax_extension(parser, ext);
    }

    cmark_syntax_extension *wikilink_ext = cmark_find_syntax_extension("wikilink");
    if (wikilink_ext)
        cmark_parser_attach_syntax_extension(parser, wikilink_ext);

    /* Set per-parse state via TLS (thread-safe) */
    wm_parse_state state;
    state.config = config ? *config : wikimark_config_default();
    /* Apply spec §15 defaults for any limit the caller left at zero */
    if (state.config.max_template_expansions == 0)
        state.config.max_template_expansions = 500;
    if (state.config.max_expansion_output == 0)
        state.config.max_expansion_output = 2 * 1024 * 1024;
    if (state.config.max_render_output == 0)
        state.config.max_render_output = 10 * 1024 * 1024;
    state.context = context;
    state.frontmatter = frontmatter;
    state.expansion_count = 0;
    wm_current_parse_state = &state;

    cmark_parser_feed(parser, body, body_len);
    cmark_node *doc = cmark_parser_finish(parser);

    if (preprocessed) free(preprocessed);

    /* === Phase 4: Post-parse processing === */
    /* The wikilink postprocess handles [[...]], ![[...]], wiki-style links,
     * and escaped bracket restoration. */

    /* Convert callouts */
    wm_convert_callouts(doc, mem);

    /* Process templates — resolve via engine callback or produce errors */
    wm_process_templates_with_context(doc, mem, context);

    /* Restore bracket / brace sentinels back to their ASCII
     * originals now that template processing is done. Three
     * sentinel pairs are currently in use:
     *   U+E001 (0xEE 0x80 0x81) → '{' (escaped \{\{)
     *   U+E004 (0xEE 0x80 0x84) → '[' (inside template args)
     *   U+E005 (0xEE 0x80 0x85) → ']' (inside template args)
     * This pass must run AFTER template processing so that
     * \{\{ is not mistaken for a template call, and so that
     * `[[link]]` inside template args has already been restored
     * and handed to the engine callback by the template
     * postprocessor. Any sentinels still present at this point
     * came from unmatched or partial constructs and should render
     * as literal text. */
    {
        cmark_iter *biter = cmark_iter_new(doc);
        cmark_event_type bev;
        while ((bev = cmark_iter_next(biter)) != CMARK_EVENT_DONE) {
            cmark_node *node = cmark_iter_get_node(biter);
            if (bev == CMARK_EVENT_ENTER && node->type == CMARK_NODE_TEXT) {
                const char *lit = cmark_node_get_literal(node);
                if (!lit) continue;
                /* Scan for any sentinel in the U+E001..U+E005 range. */
                int has_sentinel = 0;
                size_t len = strlen(lit);
                for (size_t bi = 0; bi + 2 < len; bi++) {
                    if ((unsigned char)lit[bi] == 0xEE &&
                        (unsigned char)lit[bi+1] == 0x80) {
                        unsigned char b = (unsigned char)lit[bi+2];
                        if (b == 0x81 || b == 0x84 || b == 0x85) {
                            has_sentinel = 1;
                            break;
                        }
                    }
                }
                if (has_sentinel) {
                    char *restored = (char *)mem->calloc(1, len + 1);
                    size_t rj = 0;
                    for (size_t bi = 0; bi < len; ) {
                        if (bi + 2 < len &&
                            (unsigned char)lit[bi] == 0xEE &&
                            (unsigned char)lit[bi+1] == 0x80) {
                            unsigned char b = (unsigned char)lit[bi+2];
                            if (b == 0x81) { restored[rj++] = '{'; bi += 3; continue; }
                            if (b == 0x84) { restored[rj++] = '['; bi += 3; continue; }
                            if (b == 0x85) { restored[rj++] = ']'; bi += 3; continue; }
                        }
                        restored[rj++] = lit[bi++];
                    }
                    restored[rj] = '\0';
                    cmark_node_set_literal(node, restored);
                    mem->free(restored);
                }
            }
        }
        cmark_iter_free(biter);
    }

    /* Process [text]{...} and [text]|...| spans in text nodes */
    wm_process_spans(doc, mem);

    /* Attach presentation attributes and semantic annotations to links/images */
    wm_attach_attributes(doc, mem);
    wm_attach_annotations(doc, mem);

    /* Single AST walk to collect both standalone images (for figure wrapping)
     * and block-level HTML_INLINE nodes (for promotion out of paragraphs). */
    {
        cmark_iter *postiter = cmark_iter_new(doc);
        cmark_event_type postev;
        cmark_node **fig_images = NULL;
        int fig_count = 0, fig_cap = 0;
        cmark_node **to_promote = NULL;
        int promo_count = 0, promo_cap = 0;

        while ((postev = cmark_iter_next(postiter)) != CMARK_EVENT_DONE) {
            cmark_node *node = cmark_iter_get_node(postiter);
            if (postev != CMARK_EVENT_ENTER) continue;

            if (node->type == CMARK_NODE_IMAGE) {
                cmark_node *parent = cmark_node_parent(node);
                if (parent && parent->type == CMARK_NODE_PARAGRAPH) {
                    cmark_node *first = cmark_node_first_child(parent);
                    cmark_node *next = cmark_node_next(node);
                    if (first == node && (!next || (next->type == CMARK_NODE_SOFTBREAK && !cmark_node_next(next)))) {
                        if (fig_count >= fig_cap) {
                            fig_cap = fig_cap ? fig_cap * 2 : 8;
                            void *tmp = realloc(fig_images, fig_cap * sizeof(cmark_node *));
                            if (!tmp) continue;
                            fig_images = tmp;
                        }
                        fig_images[fig_count++] = node;
                    }
                }
            } else if (node->type == CMARK_NODE_HTML_INLINE) {
                const char *lit = cmark_node_get_literal(node);
                if (lit && (strncmp(lit, "<div class=\"wm-embed\"", 20) == 0 ||
                            strncmp(lit, "<div class=\"wm-callout\"", 22) == 0 ||
                            strncmp(lit, "<nav", 4) == 0 ||
                            strncmp(lit, "<table", 6) == 0)) {
                    cmark_node *parent = cmark_node_parent(node);
                    if (parent && parent->type == CMARK_NODE_PARAGRAPH) {
                        if (promo_count >= promo_cap) {
                            promo_cap = promo_cap ? promo_cap * 2 : 8;
                            void *tmp = realloc(to_promote, promo_cap * sizeof(cmark_node*));
                            if (!tmp) continue;
                            to_promote = tmp;
                        }
                        to_promote[promo_count++] = node;
                    }
                }
            }
        }
        cmark_iter_free(postiter);

        /* Process standalone images → <figure> */
        for (int fi = 0; fi < fig_count; fi++) {
            cmark_node *img = fig_images[fi];
            cmark_node *parent = cmark_node_parent(img);
            if (!parent) continue;

            const char *src = cmark_node_get_url(img);
            const char *alt = "";
            cmark_node *alt_node = cmark_node_first_child(img);
            if (alt_node) alt = cmark_node_get_literal(alt_node);
            if (!alt) alt = "";

            size_t html_cap = strlen(src ? src : "") + strlen(alt) + 200;
            char *html = (char *)malloc(html_cap);
            if (!html) continue;
            snprintf(html, html_cap,
                "<figure><img src=\"%s\" alt=\"%s\" />"
                "<figcaption>%s</figcaption></figure>\n",
                src ? src : "", alt, alt);

            cmark_node *block = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
            cmark_node_set_literal(block, html);
            cmark_node_insert_before(parent, block);
            cmark_node_free(parent);
            free(html);
        }
        free(fig_images);

        /* Process block-level HTML_INLINE promotion */
        for (int pi = 0; pi < promo_count; pi++) {
            cmark_node *node = to_promote[pi];
            cmark_node *parent = cmark_node_parent(node);
            if (!parent) continue;

            const char *lit = cmark_node_get_literal(node);
            size_t lit_len = lit ? strlen(lit) : 0;

            /* Build block HTML with newline */
            char *block_html = (char *)mem->calloc(1, lit_len + 2);
            memcpy(block_html, lit, lit_len);
            if (lit_len == 0 || block_html[lit_len-1] != '\n')
                block_html[lit_len++] = '\n';
            block_html[lit_len] = '\0';

            cmark_node *block = cmark_node_new_with_mem(CMARK_NODE_HTML_BLOCK, mem);
            cmark_node_set_literal(block, block_html);
            mem->free(block_html);

            cmark_node_insert_before(parent, block);
            cmark_node_free(parent);
        }
        free(to_promote);
    }

    /* Clear per-parse state */
    wm_current_parse_state = NULL;

    /* Render to HTML */
    char *html = cmark_render_html(doc, options,
                                    cmark_parser_get_syntax_extensions(parser));

    cmark_node_free(doc);
    cmark_parser_free(parser);
    wm_frontmatter_free(frontmatter);
    free(expanded_body);

    return html;
}

/* --- Public API --- */

char *wikimark_markdown_to_html(const char *text, size_t len, int options) {
    return do_convert(text, len, options, NULL, NULL);
}

char *wikimark_markdown_to_html_with_config(
    const char *text, size_t len, int options,
    const wikimark_config *config) {
    return do_convert(text, len, options, config, NULL);
}

char *wikimark_render(const char *text, size_t len, int options,
                      const wikimark_config *config,
                      const wikimark_context *context) {
    return do_convert(text, len, options, config, context);
}

void wikimark_free(void *ptr) {
    cmark_get_default_mem_allocator()->free(ptr);
}

/* --- Node type accessors --- */

cmark_node_type wikimark_node_type_wikilink(void) {
    return CMARK_NODE_WM_WIKILINK;
}

int wikimark_node_is_wikilink(cmark_node *node) {
    return node && node->type == CMARK_NODE_WM_WIKILINK;
}

const char *wikimark_node_get_wiki_target(cmark_node *node) {
    if (!node || node->type != CMARK_NODE_WM_WIKILINK)
        return NULL;
    wikilink_data *data = (wikilink_data *)node->as.opaque;
    return data ? data->target : NULL;
}

/* --- Public frontmatter API ---
 *
 * The opaque `wikimark_frontmatter` is the internal `wm_fm_node`
 * root. We cast at the boundary; callers never dereference.
 */

wikimark_frontmatter *wikimark_frontmatter_parse(
    const char *text, size_t len) {
    size_t body_start = 0;
    wm_fm_node *root = wm_frontmatter_parse(text, len, &body_start);
    return (wikimark_frontmatter *)root;
}

const char *wikimark_frontmatter_get(
    const wikimark_frontmatter *fm, const char *path) {
    return wm_frontmatter_get((const wm_fm_node *)fm, path);
}

void wikimark_frontmatter_free(wikimark_frontmatter *fm) {
    wm_frontmatter_free((wm_fm_node *)fm);
}
