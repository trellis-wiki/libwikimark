/**
 * Variable reference expansion (${...}).
 * Uses engine callbacks to resolve variable values.
 */

#include "variable.h"
#include <string.h>
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
    size_t j = 0;

    int in_code = 0; /* Track backtick code spans */

    for (size_t i = 0; i < len; ) {
        /* Track code spans — don't expand variables inside backticks */
        if (s[i] == '`') {
            if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
            result[j++] = s[i++];
            /* Count backticks for fenced code */
            int ticks = 1;
            while (i < len && s[i] == '`') {
                if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
                result[j++] = s[i++];
                ticks++;
            }
            /* Copy until matching backtick sequence */
            while (i < len) {
                int match = 1;
                if (s[i] == '`') {
                    int count = 0;
                    size_t check = i;
                    while (check < len && s[check] == '`') { count++; check++; }
                    if (count == ticks) {
                        /* Closing backticks — copy and break */
                        for (int t = 0; t < ticks; t++) {
                            if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
                            result[j++] = s[i++];
                        }
                        break;
                    }
                }
                if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
                result[j++] = s[i++];
            }
            continue;
        }

        /* Escaped \${ → literal ${ */
        if (i + 2 < len && s[i] == '\\' && s[i+1] == '$' && s[i+2] == '{') {
            if (j + 2 >= cap) { cap *= 2; result = realloc(result, cap); }
            result[j++] = '$';
            result[j++] = '{';
            i += 3;
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
                    while (j + vlen >= cap) { cap *= 2; result = realloc(result, cap); }
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
        if (j + 1 >= cap) { cap *= 2; result = realloc(result, cap); }
        result[j++] = s[i++];
    }

    result[j] = '\0';
    return result;
}
