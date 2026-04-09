/**
 * libwikimark — WikiMark extensions for cmark-gfm.
 *
 * WikiMark is a strict superset of GitHub Flavored Markdown with wiki links,
 * templates, semantic annotations, and structured page metadata.
 *
 * Usage:
 *   wikimark_extensions_ensure_registered();
 *   char *html = wikimark_markdown_to_html(text, len, CMARK_OPT_DEFAULT);
 *   // ... use html ...
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

/**
 * Register all WikiMark extensions with cmark-gfm.
 * Must be called before using any WikiMark functionality.
 * Safe to call from multiple threads; uses once-init semantics.
 */
void wikimark_extensions_ensure_registered(void);

/* --- Configuration --- */

typedef struct wikimark_config {
    const char *base_url;       /**< Prepended to wiki link hrefs (default: "") */
    const char *template_dir;   /**< Template directory; NULL = disabled (default: NULL) */
    int max_expansion_depth;    /**< Max template recursion depth (default: 40) */
    int max_expansions;         /**< Max total expansions per page (default: 500) */
    size_t max_output_size;     /**< Max expanded output bytes (default: 2MB) */
    int case_sensitive;         /**< 1 = disable first-letter capitalization (default: 0) */
    int bare_bracket_links;     /**< 1 = bare [PageName] becomes wiki link (default: 1) */
} wikimark_config;

/** Return a config with all default values. */
wikimark_config wikimark_config_default(void);

/* --- Convenience API --- */

/**
 * Convert WikiMark to HTML using default config.
 * Caller must free the result with wikimark_free().
 */
char *wikimark_markdown_to_html(const char *text, size_t len, int options);

/**
 * Convert WikiMark to HTML using the provided config.
 * Caller must free the result with wikimark_free().
 */
char *wikimark_markdown_to_html_with_config(
    const char *text, size_t len, int options,
    const wikimark_config *config);

/** Free memory returned by wikimark_markdown_to_html*. */
void wikimark_free(void *ptr);

/* --- Node type accessors (ABI-safe) --- */

cmark_node_type wikimark_node_type_wikilink(void);

/* --- Node predicates --- */

int wikimark_node_is_wikilink(cmark_node *node);

/* --- Node data accessors (strings valid for node lifetime) --- */

const char *wikimark_node_get_wiki_target(cmark_node *node);

#ifdef __cplusplus
}
#endif

#endif /* WIKIMARK_H */
