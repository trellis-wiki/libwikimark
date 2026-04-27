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
 *
 * Unicode NFC normalization: NOT implemented. Spec §13.4 makes NFC
 * a SHOULD (not MUST), with explicit permission to skip. We skip
 * and document via README / PLAN. Engines that need NFC must do it
 * at the engine layer — libwikimark handles titles byte-for-byte
 * past the ASCII rules above. The consequence: two visually
 * identical titles differing only in NFC vs. NFD representation
 * become distinct URLs. This is extremely rare in practice and not
 * worth a libunicode dependency.
 *
 * Returns a newly allocated string. Caller must free with mem->free().
 */
char *wikimark_normalize_title(const char *title, int case_sensitive,
                               cmark_mem *mem);

#endif /* WIKIMARK_NORMALIZE_H */
