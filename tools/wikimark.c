/**
 * WikiMark CLI — convert WikiMark to HTML.
 *
 * Uses the test engine for variable/template/embed resolution.
 *
 * Usage:
 *   wikimark [options] [FILE]
 *   echo '[[Main Page]]' | wikimark
 *
 * Options:
 *   --template-dir DIR   Directory for template .wm files
 *   --pages-dir DIR      Directory for page .wm files (embeds)
 */

#include "wikimark.h"
#include "test_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    while (1) {
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
        if (len == cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
            if (!buf) return NULL;
        }
    }
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    FILE *input = stdin;
    const char *template_dir = NULL;
    const char *pages_dir = NULL;

    /* Parse arguments */
    int file_arg = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("wikimark %s\n", WIKIMARK_VERSION_STRING);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stderr,
                "Usage: wikimark [options] [FILE]\n"
                "Convert WikiMark to HTML.\n\n"
                "Options:\n"
                "  --template-dir DIR  Template .wm files directory\n"
                "  --pages-dir DIR     Page .wm files directory (for embeds)\n"
                "  --version           Show version\n"
                "  -h, --help          Show this help\n");
            return 0;
        }
        if (strcmp(argv[i], "--template-dir") == 0 && i + 1 < argc) {
            template_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--pages-dir") == 0 && i + 1 < argc) {
            pages_dir = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            file_arg = i;
        }
    }

    if (file_arg) {
        input = fopen(argv[file_arg], "r");
        if (!input) {
            fprintf(stderr, "wikimark: cannot open '%s'\n", argv[file_arg]);
            return 1;
        }
    }

    size_t len = 0;
    char *text = read_all(input, &len);
    if (input != stdin) fclose(input);
    if (!text) { fprintf(stderr, "wikimark: out of memory\n"); return 1; }

    /* Set up config */
    wikimark_interwiki interwiki[] = {
        { "wikipedia", "https://en.wikipedia.org/wiki/{page}" },
    };
    wikimark_config config = wikimark_config_default();
    config.interwiki = interwiki;
    config.interwiki_count = 1;

    /* Set up test engine — always active for variable resolution,
     * template resolution only when template_dir is set */
    test_engine *engine = test_engine_new(template_dir, pages_dir);
    wikimark_context ctx = test_engine_get_context(engine);
    wikimark_context *ctx_ptr = &ctx;

    /* Render — if no engine, built-in frontmatter resolver handles ${...} */
    char *html = wikimark_render(text, len,
        CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE, &config, ctx_ptr);
    free(text);

    if (!html) {
        fprintf(stderr, "wikimark: conversion failed\n");
        test_engine_free(engine);
        return 1;
    }

    fputs(html, stdout);
    wikimark_free(html);
    test_engine_free(engine);

    return 0;
}
