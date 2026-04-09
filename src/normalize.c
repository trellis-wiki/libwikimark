/**
 * Page title normalization (WikiMark spec §13).
 *
 * Rules:
 * - Strip leading/trailing whitespace
 * - Collapse internal whitespace to single underscore
 * - Replace spaces with underscores
 * - Preserve case (no first-letter capitalization)
 */

#include "normalize.h"
#include <string.h>

char *wikimark_normalize_title(const char *title, int case_sensitive,
                               cmark_mem *mem) {
    (void)case_sensitive; /* No longer used — case always preserved */

    if (!title || !*title)
        return NULL;

    size_t len = strlen(title);
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

    int prev_space = 0;

    while (src < end) {
        if (*src == ' ' || *src == '\t' || *src == '_') {
            if (!prev_space) {
                *dst++ = '_';
                prev_space = 1;
            }
            src++;
        } else {
            *dst++ = *src;
            prev_space = 0;
            src++;
        }
    }

    *dst = '\0';
    return result;
}
