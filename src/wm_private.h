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

/*
 * Escaped modifier characters.
 *
 * `\[ \] \{ \} \| \!` become sentinels before cmark-gfm parses the body,
 * so the wiki link, embed, template, span, attribute and annotation passes
 * (which work on text after cmark has resolved escapes) can't mistake an
 * escaped character for syntax. They are restored afterwards: to the bare
 * character in text, URLs and generated HTML; to backslash + character in
 * code and raw HTML, where a backslash escape never applied.
 *
 * The sentinels are U+1CC0..U+1CC5 (Sundanese punctuation): cmark counts
 * them as punctuation, as it does the characters they stand for, so
 * emphasis next to an escape opens and closes as it should. Real
 * U+1CC0..U+1CC7 in a page are swapped for U+E020..U+E027 first and
 * swapped back at the end.
 */
static const char WM_ESC_CHARS[] = "[]{}|!";
#define WM_ESC_COUNT 6
#define WM_ESC_LEAD0 0xE1
#define WM_ESC_LEAD1 0xB3
#define WM_ESC_FIRST 0x80   /* U+1CC0 */
#define WM_REAL_LEAD0 0xEE
#define WM_REAL_LEAD1 0x80
#define WM_REAL_FIRST 0xA0  /* U+E020 */

/* Index in WM_ESC_CHARS if s[0..2] is an escape sentinel, else -1. */
static inline int wm_esc_sentinel_at(const unsigned char *s, size_t avail) {
    if (avail < 3 || s[0] != WM_ESC_LEAD0 || s[1] != WM_ESC_LEAD1) return -1;
    int k = (int)s[2] - WM_ESC_FIRST;
    return (k >= 0 && k < WM_ESC_COUNT) ? k : -1;
}

/* 0..7 if s[0..2] is a swapped-out real U+1CC0..U+1CC7, else -1. */
static inline int wm_real_at(const unsigned char *s, size_t avail) {
    if (avail < 3 || s[0] != WM_REAL_LEAD0 || s[1] != WM_REAL_LEAD1) return -1;
    int k = (int)s[2] - WM_REAL_FIRST;
    return (k >= 0 && k < 8) ? k : -1;
}

/* Copy `len` bytes of `in` to `out` replacing sentinels with their
 * character (keep_backslash: with a backslash before it). `out` may be
 * `in`; it needs room for
 * `len` + 1 bytes. Returns the output length; out is NUL-terminated. */
static inline size_t wm_esc_restore(const char *in, size_t len, char *out, int keep_backslash) {
    size_t j = 0;
    for (size_t i = 0; i < len; ) {
        const unsigned char *u = (const unsigned char *)in + i;
        int k = wm_esc_sentinel_at(u, len - i);
        if (k >= 0) {
            if (keep_backslash) out[j++] = '\\';
            out[j++] = WM_ESC_CHARS[k];
            i += 3;
            continue;
        }
        out[j++] = in[i++];
    }
    out[j] = '\0';
    return j;
}

/* Swap U+E020..U+E027 back to the real U+1CC0..U+1CC7 (in place). Only on
 * the final HTML, after every sentinel is gone. */
static inline void wm_esc_unswap_real(char *s, size_t len) {
    for (size_t i = 0; i + 2 < len; i++) {
        int k = wm_real_at((const unsigned char *)s + i, len - i);
        if (k >= 0) {
            s[i] = (char)WM_ESC_LEAD0;
            s[i+1] = (char)WM_ESC_LEAD1;
            s[i+2] = (char)(WM_ESC_FIRST + k);
            i += 2;
        }
    }
}

static inline int wm_esc_has_sentinel(const char *s) {
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (wm_esc_sentinel_at(p, 3) >= 0) return 1;
    return 0;
}

/* Swap real U+1CC0..U+1CC7 in s (in place, same length) for U+E020..U+E027. */
static inline void wm_esc_swap_real(char *s, size_t len) {
    for (size_t i = 0; i + 2 < len; i++) {
        unsigned char *u = (unsigned char *)s + i;
        if (u[0] == WM_ESC_LEAD0 && u[1] == WM_ESC_LEAD1 && u[2] >= WM_ESC_FIRST && u[2] < WM_ESC_FIRST + 8) {
            u[2] = (unsigned char)(WM_REAL_FIRST + (u[2] - WM_ESC_FIRST));
            u[0] = WM_REAL_LEAD0;
            u[1] = WM_REAL_LEAD1;
            i += 2;
        }
    }
}

#endif
