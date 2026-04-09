/**
 * Shared scanning utilities for WikiMark inline parsing.
 */

#ifndef WIKIMARK_SCANNER_H
#define WIKIMARK_SCANNER_H

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>

/**
 * Scan forward from the current position in the inline parser, looking for
 * a closing delimiter sequence. Respects quoted strings ("...").
 *
 * Returns the offset past the closing delimiter, or -1 if not found
 * before end of line/input.
 *
 * close1: first char of closing delimiter (e.g., ']')
 * close2: second char of closing delimiter (e.g., ']' for ']]'), or 0 for single-char
 */
int wm_scan_to_closing(cmark_inline_parser *inline_parser,
                        unsigned char close1, unsigned char close2);

#endif /* WIKIMARK_SCANNER_H */
