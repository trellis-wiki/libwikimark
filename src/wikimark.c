/**
 * libwikimark — main registration and convenience API.
 */

#include "wikimark.h"
#include "wikilink.h"
#include "node_types.h"
#include "wm_private.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
#include <parser.h>
#include <node.h>
#include <registry.h>
#include <plugin.h>

#include <string.h>
#include <pthread.h>

/* --- Registration --- */

static int wikimark_registration(cmark_plugin *plugin) {
    cmark_plugin_register_syntax_extension(plugin, create_wikilink_extension());
    return 1;
}

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void do_register(void) {
    /* Register GFM core extensions first (tables, strikethrough, etc.) */
    cmark_gfm_core_extensions_ensure_registered();
    /* Then register WikiMark extensions */
    cmark_register_plugin(wikimark_registration);
}

void wikimark_extensions_ensure_registered(void) {
    pthread_once(&once_control, do_register);
}

/* --- Configuration --- */

wikimark_config wikimark_config_default(void) {
    wikimark_config config;
    config.base_url = "";
    config.template_dir = NULL;
    config.max_expansion_depth = 40;
    config.max_expansions = 500;
    config.max_output_size = 2 * 1024 * 1024;
    config.case_sensitive = 0;
    config.bare_bracket_links = 1;
    return config;
}

/* --- Convenience API --- */

static char *do_convert(const char *text, size_t len, int options,
                        const wikimark_config *config) {
    wikimark_extensions_ensure_registered();

    cmark_parser *parser = cmark_parser_new(options);

    /* Attach GFM extensions */
    static const char *gfm_exts[] = {
        "table", "strikethrough", "autolink", "tagfilter", "tasklist", NULL
    };
    for (int i = 0; gfm_exts[i]; i++) {
        cmark_syntax_extension *ext = cmark_find_syntax_extension(gfm_exts[i]);
        if (ext)
            cmark_parser_attach_syntax_extension(parser, ext);
    }

    /* Attach WikiMark extensions */
    cmark_syntax_extension *wikilink_ext = cmark_find_syntax_extension("wikilink");
    if (wikilink_ext)
        cmark_parser_attach_syntax_extension(parser, wikilink_ext);

    /* Set per-parse config on the wikilink extension */
    wm_parse_state state;
    state.config = config ? *config : wikimark_config_default();
    if (wikilink_ext)
        cmark_syntax_extension_set_private(wikilink_ext, &state, NULL);

    /* Pre-process: protect \[\[ from being parsed as wiki links.
     *
     * Problem: cmark converts \[ to literal [ and merges text nodes, so
     * by postprocess time \[\[foo]] is indistinguishable from [[foo]].
     *
     * Solution: replace each \[ with U+E000 (Private Use Area char) before
     * feeding to cmark. Our postprocessor converts U+E000 back to [ in all
     * text nodes. Since U+E000 is not [, cmark won't create bracket
     * structures from it, and our [[ scanner won't match it.
     *
     * U+E000 in UTF-8: 0xEE 0x80 0x80 (3 bytes replaces 2 bytes \[ )
     */
    char *preprocessed = NULL;
    int needs_preprocess = 0;
    for (size_t i = 0; i + 3 < len; i++) {
        if (text[i] == '\\' && text[i+1] == '[' &&
            text[i+2] == '\\' && text[i+3] == '[') {
            needs_preprocess = 1;
            break;
        }
    }

    if (needs_preprocess) {
        /* Only replace \[\[ (escaped wiki link opener), not bare \[ */
        preprocessed = (char *)malloc(len * 2 + 1);
        size_t j = 0;
        for (size_t i = 0; i < len; i++) {
            if (i + 3 < len &&
                text[i] == '\\' && text[i+1] == '[' &&
                text[i+2] == '\\' && text[i+3] == '[') {
                /* Replace \[\[ with U+E000 U+E000 */
                preprocessed[j++] = (char)0xEE;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0xEE;
                preprocessed[j++] = (char)0x80;
                preprocessed[j++] = (char)0x80;
                i += 3; /* skip \[\[ (4 chars, loop adds 1) */
            } else {
                preprocessed[j++] = text[i];
            }
        }
        text = preprocessed;
        len = j;
    }

    /* Parse */
    cmark_parser_feed(parser, text, len);
    cmark_node *doc = cmark_parser_finish(parser);

    if (preprocessed)
        free(preprocessed);

    /* Clear per-parse state */
    if (wikilink_ext)
        cmark_syntax_extension_set_private(wikilink_ext, NULL, NULL);

    /* Render to HTML */
    char *html = cmark_render_html(doc, options, cmark_parser_get_syntax_extensions(parser));

    cmark_node_free(doc);
    cmark_parser_free(parser);

    return html;
}

char *wikimark_markdown_to_html(const char *text, size_t len, int options) {
    return do_convert(text, len, options, NULL);
}

char *wikimark_markdown_to_html_with_config(
    const char *text, size_t len, int options,
    const wikimark_config *config) {
    return do_convert(text, len, options, config);
}

void wikimark_free(void *ptr) {
    cmark_mem *mem = cmark_get_default_mem_allocator();
    mem->free(ptr);
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
