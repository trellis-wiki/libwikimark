#ifndef WIKIMARK_PRIVATE_H
#define WIKIMARK_PRIVATE_H

#include "wikimark.h"
#include "frontmatter.h"
#include <stdlib.h>

typedef struct {
    wikimark_config config;
    const wikimark_context *context;  /* Engine callbacks (may be NULL) */
    wm_fm_node *frontmatter;          /* Parsed YAML frontmatter (owned by do_convert) */
    int expansion_count;               /* §15 enforcement: template expansions so far */
} wm_parse_state;

/* Thread-local per-parse state.
 * cmark_syntax_extension_set_private() stores on a global singleton,
 * which is unsafe for concurrent parses. We use TLS instead. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
  #define WM_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
  #define WM_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
  #define WM_THREAD_LOCAL __declspec(thread)
#else
  #define WM_THREAD_LOCAL /* fallback: not thread-safe */
#endif

extern WM_THREAD_LOCAL wm_parse_state *wm_current_parse_state;

/**
 * Safe realloc: preserves the original pointer on failure.
 * Returns the new pointer, or NULL on failure (original ptr unchanged).
 */
static inline void *wm_realloc(void **ptr, size_t size) {
    void *tmp = realloc(*ptr, size);
    if (tmp) *ptr = tmp;
    return tmp;
}

#endif
