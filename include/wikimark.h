/**
 * libwikimark — WikiMark extensions for cmark-gfm.
 *
 * WikiMark is a strict superset of GitHub Flavored Markdown with wiki links,
 * templates, semantic annotations, and structured page metadata.
 *
 * Architecture:
 *   libwikimark is a PARSER, not a wiki engine. It converts WikiMark syntax
 *   to HTML. Content resolution (variables, templates, embeds) is handled
 *   by the wiki engine via callbacks.
 *
 *   Without callbacks: ${...} and {{...}} produce error indicators.
 *   With callbacks: the engine resolves them and the parser inserts the result.
 *
 * Basic usage (no engine):
 *   char *html = wikimark_markdown_to_html(text, len, CMARK_OPT_DEFAULT);
 *   wikimark_free(html);
 *
 * With engine callbacks:
 *   wikimark_context ctx = wikimark_context_default();
 *   ctx.resolve_variable = my_var_resolver;
 *   ctx.resolve_template = my_template_resolver;
 *   ctx.user_data = my_engine;
 *   char *html = wikimark_render(text, len, CMARK_OPT_DEFAULT, &config, &ctx);
 *   wikimark_free(html);
 */

#ifndef WIKIMARK_H
#define WIKIMARK_H

#include <cmark-gfm.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Version --- */
#define WIKIMARK_VERSION_MAJOR 0
#define WIKIMARK_VERSION_MINOR 1
#define WIKIMARK_VERSION_PATCH 0
#define WIKIMARK_VERSION_STRING "0.1.0"

/* --- Registration (thread-safe, idempotent) --- */

void wikimark_extensions_ensure_registered(void);

/* --- Configuration (parser settings, no engine state) --- */

typedef struct wikimark_interwiki {
    const char *prefix;
    const char *url_format;  /**< Use {page} as placeholder */
} wikimark_interwiki;

typedef struct wikimark_config {
    const char *base_url;
    const wikimark_interwiki *interwiki;
    int interwiki_count;

    /**
     * Resource limits per §15 of the spec. Zero means "use the spec
     * default" (filled in by wikimark_config_default). Setting a
     * non-zero value overrides. The processor MUST enforce all limits;
     * engines that want tighter control can reduce them.
     */
    int max_template_expansions;     /**< §15.2: per-render (default 500) */
    size_t max_expansion_output;     /**< §15.2: per-expansion bytes (default 2 MB) */
    size_t max_render_output;        /**< §15.2: total render bytes (default 10 MB) */
} wikimark_config;

wikimark_config wikimark_config_default(void);

/* --- Engine context (callbacks for content resolution) --- */

/**
 * Callbacks the wiki engine provides to resolve content.
 * All returned strings are owned by the engine and must remain valid
 * until wikimark_render() returns.
 */
typedef struct wikimark_context {
    /**
     * Resolve a variable path (e.g., "title", "star.name", "moons.0").
     * Return the value string, or NULL if undefined (→ empty string).
     */
    const char *(*resolve_variable)(const char *path, void *user_data);

    /**
     * Resolve a template transclusion.
     * name: template name (e.g., "greeting")
     * args: raw argument string (e.g., "name=\"Alice\""), or NULL
     * Return rendered HTML, or NULL (→ error indicator).
     */
    const char *(*resolve_template)(const char *name, const char *args,
                                     void *user_data);

    /**
     * Resolve a page embed (![[target]]).
     * Return rendered HTML, or NULL (→ error indicator).
     */
    const char *(*resolve_embed)(const char *target, void *user_data);

    void *user_data;  /**< Passed to all callbacks */
} wikimark_context;

wikimark_context wikimark_context_default(void);

/* --- Rendering API --- */

/**
 * Convert WikiMark to HTML (no engine — variables/templates produce errors).
 * Caller frees with wikimark_free().
 */
char *wikimark_markdown_to_html(const char *text, size_t len, int options);

/**
 * Convert WikiMark to HTML with config (no engine).
 */
char *wikimark_markdown_to_html_with_config(
    const char *text, size_t len, int options,
    const wikimark_config *config);

/**
 * Convert WikiMark to HTML with config and engine context.
 * The engine provides callbacks for resolving variables, templates, and embeds.
 */
char *wikimark_render(const char *text, size_t len, int options,
                      const wikimark_config *config,
                      const wikimark_context *context);

void wikimark_free(void *ptr);

/* --- Node type accessors --- */

cmark_node_type wikimark_node_type_wikilink(void);
int wikimark_node_is_wikilink(cmark_node *node);
const char *wikimark_node_get_wiki_target(cmark_node *node);

/* --- Frontmatter inspection ---
 *
 * Engines that embed libwikimark often need to read page
 * frontmatter to drive variable and template resolution. Rather
 * than reparsing YAML, callers can use these helpers to access
 * the same tree libwikimark parses internally.
 */

/**
 * Opaque handle to a parsed frontmatter tree. Scalar values are
 * accessed via wikimark_frontmatter_get().
 */
typedef struct wikimark_frontmatter wikimark_frontmatter;

/**
 * Parse YAML frontmatter from the beginning of a document.
 *
 * A frontmatter block is:
 *   ---
 *   <yaml>
 *   ---
 *
 * at the start of `text`, optionally preceded by a UTF-8 BOM.
 *
 * Returns NULL if no valid frontmatter is present. The returned
 * handle must be freed with wikimark_frontmatter_free().
 */
wikimark_frontmatter *wikimark_frontmatter_parse(
    const char *text, size_t len);

/**
 * Look up a scalar value by dot-notation path.
 *
 * Examples:
 *   "title"         → top-level key
 *   "star.name"     → nested map key
 *   "moons.0"       → list index
 *   "inputs.name.default" → nested further
 *
 * Returns a string owned by the frontmatter tree, valid until
 * wikimark_frontmatter_free() is called. Returns NULL if the
 * path does not resolve to a scalar.
 */
const char *wikimark_frontmatter_get(
    const wikimark_frontmatter *fm, const char *path);

/**
 * Release the frontmatter tree.
 */
void wikimark_frontmatter_free(wikimark_frontmatter *fm);

#ifdef __cplusplus
}
#endif

#endif /* WIKIMARK_H */
