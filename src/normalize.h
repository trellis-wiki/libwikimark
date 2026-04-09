/**
 * Page title normalization (WikiMark spec §13).
 */

#ifndef WIKIMARK_NORMALIZE_H
#define WIKIMARK_NORMALIZE_H

#include <cmark-gfm.h>

/**
 * Normalize a wiki page title for use as a URL.
 *
 * Rules (per spec §13):
 * - Strip leading/trailing whitespace
 * - Collapse internal whitespace to single space
 * - Replace spaces with underscores
 * - Capitalize first letter (unless case_sensitive)
 * - Unicode NFC normalization (TODO)
 *
 * Returns a newly allocated string. Caller must free with mem->free().
 */
char *wikimark_normalize_title(const char *title, int case_sensitive,
                               cmark_mem *mem);

#endif /* WIKIMARK_NORMALIZE_H */
