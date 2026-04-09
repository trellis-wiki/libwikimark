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
    config.case_sensitive = 0;
    config.bare_bracket_links = 1;
    config.interwiki = NULL;
    config.interwiki_count = 0;
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
                redirect_target, config ? config->case_sensitive : 0, mem);
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

    /* === Determine resolution context === */

    /* If no engine context provided, use a default that resolves ${...}
     * from the page's own frontmatter. If an engine is provided, it's
     * responsible for all resolution (including frontmatter variables). */
    default_resolver_data default_data;
    wikimark_context default_ctx;
    const wikimark_context *ctx;

    if (context && context->resolve_variable) {
        ctx = context;
    } else {
        default_data.frontmatter = frontmatter;
        default_ctx = wikimark_context_default();
        default_ctx.resolve_variable = default_resolve_variable;
        default_ctx.user_data = &default_data;
        ctx = &default_ctx;
    }

    /* Expand ${...} variables in the body text before parsing.
     * This is a text-level substitution so the parser sees the resolved values. */
    char *expanded_body = wm_expand_variables_str(body, body_len, ctx);
    if (expanded_body) {
        body = expanded_body;
        body_len = strlen(expanded_body);
    }

    /* === Pre-process: protect \[\[ from wiki link parsing === */
    char *preprocessed = NULL;
    int needs_preprocess = 0;
    for (size_t i = 0; i + 3 < body_len; i++) {
        if (body[i] == '\\' && body[i+1] == '[' &&
            body[i+2] == '\\' && body[i+3] == '[') {
            needs_preprocess = 1;
            break;
        }
    }
    if (needs_preprocess) {
        preprocessed = (char *)malloc(body_len * 2 + 1);
        size_t j = 0;
        for (size_t i = 0; i < body_len; i++) {
            if (i + 3 < body_len &&
                body[i] == '\\' && body[i+1] == '[' &&
                body[i+2] == '\\' && body[i+3] == '[') {
                preprocessed[j++] = (char)0xEE;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0xEE;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0x80;
                i += 3;
            } else {
                preprocessed[j++] = body[i];
            }
        }
        body = preprocessed;
        body_len = j;
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

    /* Set per-parse state */
    wm_parse_state state;
    state.config = config ? *config : wikimark_config_default();
    state.context = ctx;
    state.frontmatter = frontmatter;
    if (wikilink_ext)
        cmark_syntax_extension_set_private(wikilink_ext, &state, NULL);

    cmark_parser_feed(parser, body, body_len);
    cmark_node *doc = cmark_parser_finish(parser);

    if (preprocessed) free(preprocessed);

    /* === Phase 4: Post-parse processing === */
    /* The wikilink postprocess handles [[...]], ![[...]], wiki-style links,
     * and escaped bracket restoration. */

    /* Convert callouts */
    wm_convert_callouts(doc, mem);

    /* Process templates — resolve via engine callback or produce errors */
    wm_process_templates_with_context(doc, mem, ctx);

    /* Attach presentation attributes and semantic annotations */
    wm_attach_attributes(doc, mem);
    wm_attach_annotations(doc, mem);

    /* Clear per-parse state */
    if (wikilink_ext)
        cmark_syntax_extension_set_private(wikilink_ext, NULL, NULL);

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
