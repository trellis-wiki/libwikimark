/**
 * Variable reference expansion (${...}).
 * Uses engine callbacks to resolve variable values.
 */

#include "variable.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

char *wm_expand_variables_str(const char *s, size_t len,
                               const wikimark_context *ctx) {
    if (!ctx || !ctx->resolve_variable) return NULL;

    /* Quick check: any ${ at all? */
    int found = 0;
    for (size_t i = 0; i + 1 < len; i++) {
        if (s[i] == '$' && s[i+1] == '{') { found = 1; break; }
    }
    if (!found) return NULL;

    size_t cap = len * 2 + 1;
    char *result = (char *)malloc(cap);
    if (!result) return NULL;
    size_t j = 0;

    int in_code = 0; /* Track backtick code spans */

    for (size_t i = 0; i < len; ) {
        /* Code spans: copied as written. A run of backticks opens a span
         * only if a run of the same length closes it; otherwise the run is
         * literal and scanning goes on after it (as in CommonMark). */
        if (s[i] == '`') {
            size_t ticks = 0;
            while (i + ticks < len && s[i + ticks] == '`') ticks++;
            size_t k = i + ticks, close = (size_t)-1;
            while (k < len) {
                if (s[k] != '`') { k++; continue; }
                size_t run = 0;
                while (k + run < len && s[k + run] == '`') run++;
                if (run == ticks) { close = k; break; }
                k += run;
            }
            size_t stop = close == (size_t)-1 ? i + ticks : close + ticks;
            while (j + (stop - i) + 1 >= cap) { cap *= 2; void *tmp = realloc(result, cap); if (!tmp) { free(result); return NULL; } result = tmp; }
            memcpy(result + j, s + i, stop - i);
            j += stop - i;
            i = stop;
            continue;
        }

        /* Escaped \${ → literal ${ */
        if (i + 2 < len && s[i] == '\\' && s[i+1] == '$' && s[i+2] == '{') {
            if (j + 2 >= cap) { cap *= 2; void *tmp = realloc(result, cap); if (!tmp) { free(result); return NULL; } result = tmp; }
            result[j++] = '$';
            result[j++] = '{';
            i += 3;
            continue;
        }

        /* Any other backslash escape is copied as a unit (so \\` doesn't open a code span) */
        if (s[i] == '\\' && i + 1 < len && ispunct((unsigned char)s[i+1])) {
            if (j + 2 >= cap) { cap *= 2; void *tmp = realloc(result, cap); if (!tmp) { free(result); return NULL; } result = tmp; }
            result[j++] = s[i++];
            result[j++] = s[i++];
            continue;
        }

        /* ${ ... } variable reference */
        if (i + 1 < len && s[i] == '$' && s[i+1] == '{') {
            size_t start = i + 2;
            size_t end = start;
            /* Find closing } — don't cross newlines */
            while (end < len && s[end] != '}' && s[end] != '\n') end++;

            if (end < len && s[end] == '}') {
                /* Extract path — use stack buffer for typical short paths */
                size_t path_len = end - start;
                char path_stack[256];
                char *path = path_stack;
                if (path_len >= sizeof(path_stack))
                    path = (char *)malloc(path_len + 1);
                memcpy(path, s + start, path_len);
                path[path_len] = '\0';

                /* Resolve via engine callback */
                const char *value = ctx->resolve_variable(path, ctx->user_data);
                if (value) {
                    size_t vlen = strlen(value);
                    while (j + vlen >= cap) { cap *= 2; void *tmp = realloc(result, cap); if (!tmp) { free(result); return NULL; } result = tmp; }
                    memcpy(result + j, value, vlen);
                    j += vlen;
                }

                if (path != path_stack) free(path);
                i = end + 1;
                continue;
            }
            /* Unclosed ${ — literal */
        }

        /* Don't expand $ alone (not followed by {) */
        if (j + 1 >= cap) { cap *= 2; void *tmp = realloc(result, cap); if (!tmp) { free(result); return NULL; } result = tmp; }
        result[j++] = s[i++];
    }

    result[j] = '\0';
    return result;
}
