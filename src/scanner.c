/**
 * Shared scanning utilities for WikiMark inline parsing.
 */

#include "scanner.h"

int wm_scan_to_closing(cmark_inline_parser *inline_parser,
                        unsigned char close1, unsigned char close2) {
    int in_quotes = 0;

    while (!cmark_inline_parser_is_eof(inline_parser)) {
        unsigned char c = cmark_inline_parser_peek_char(inline_parser);

        if (c == '\n' || c == '\r') {
            /* Wiki constructs don't span lines */
            return -1;
        }

        if (in_quotes) {
            if (c == '\\') {
                /* Skip escaped char inside quotes */
                cmark_inline_parser_advance_offset(inline_parser);
                if (!cmark_inline_parser_is_eof(inline_parser))
                    cmark_inline_parser_advance_offset(inline_parser);
                continue;
            }
            if (c == '"') {
                in_quotes = 0;
            }
            cmark_inline_parser_advance_offset(inline_parser);
            continue;
        }

        if (c == '"') {
            in_quotes = 1;
            cmark_inline_parser_advance_offset(inline_parser);
            continue;
        }

        if (c == close1) {
            if (close2 == 0) {
                /* Single-char closing delimiter */
                cmark_inline_parser_advance_offset(inline_parser);
                return cmark_inline_parser_get_offset(inline_parser);
            }
            /* Peek ahead for two-char closing delimiter */
            int saved = cmark_inline_parser_get_offset(inline_parser);
            cmark_inline_parser_advance_offset(inline_parser);
            if (!cmark_inline_parser_is_eof(inline_parser) &&
                cmark_inline_parser_peek_char(inline_parser) == close2) {
                cmark_inline_parser_advance_offset(inline_parser);
                return cmark_inline_parser_get_offset(inline_parser);
            }
            /* Not a match — the single char is part of content, continue */
            continue;
        }

        cmark_inline_parser_advance_offset(inline_parser);
    }

    return -1;
}

int wm_scan_to_delim(const char *s, int len, int start, char delim) {
    int in_quotes = 0;
    for (int i = start; i < len; i++) {
        if (in_quotes) {
            if (s[i] == '\\' && i + 1 < len) { i++; continue; }
            if (s[i] == '"') in_quotes = 0;
            continue;
        }
        if (s[i] == '"') { in_quotes = 1; continue; }
        if (s[i] == delim) return i;
        if (s[i] == '\n') return -1;
    }
    return -1;
}
