/**
 * Page title normalization (WikiMark spec §13).
 */

#include "normalize.h"
#include <ctype.h>
#include <string.h>

char *wikimark_normalize_title(const char *title, int case_sensitive,
                               cmark_mem *mem) {
    if (!title || !*title)
        return NULL;

    size_t len = strlen(title);

    /* Allocate worst case (same length + null) */
    char *result = (char *)mem->calloc(1, len + 1);
    if (!result)
        return NULL;

    const char *src = title;
    char *dst = result;

    /* Skip leading whitespace */
    while (*src && (*src == ' ' || *src == '\t'))
        src++;

    /* Find end, skip trailing whitespace */
    const char *end = title + len;
    while (end > src && (end[-1] == ' ' || end[-1] == '\t'))
        end--;

    int first_letter = 1;  /* Track first letter for capitalization */
    int prev_space = 0;    /* Collapse whitespace */

    while (src < end) {
        if (*src == ' ' || *src == '\t') {
            if (!prev_space) {
                *dst++ = '_';
                prev_space = 1;
            }
            src++;
        } else if (*src == '_') {
            /* Underscore treated as space */
            if (!prev_space) {
                *dst++ = '_';
                prev_space = 1;
            }
            src++;
        } else {
            if (first_letter && !case_sensitive && *src >= 'a' && *src <= 'z') {
                *dst++ = *src - 32;  /* Capitalize */
            } else {
                *dst++ = *src;
            }
            first_letter = 0;
            prev_space = 0;
            src++;
        }
    }

    *dst = '\0';
    return result;
}
