/**
 * WikiMark CLI — convert WikiMark to HTML.
 *
 * Reads from a file or stdin, writes HTML to stdout.
 * This is the main entry point for spec test validation.
 *
 * Usage:
 *   wikimark [FILE]
 *   echo '[[Main Page]]' | wikimark
 */

#include "wikimark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUF_SIZE 4096

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = INITIAL_BUF_SIZE;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    while (1) {
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
        if (len == cap) {
            cap *= 2;
            char *newbuf = (char *)realloc(buf, cap);
            if (!newbuf) { free(buf); return NULL; }
            buf = newbuf;
        }
    }

    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    FILE *input = stdin;

    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0) {
            printf("wikimark %s\n", WIKIMARK_VERSION_STRING);
            return 0;
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            fprintf(stderr, "Usage: wikimark [FILE]\n"
                            "Convert WikiMark to HTML.\n"
                            "Reads from FILE or stdin.\n");
            return 0;
        }
        input = fopen(argv[1], "r");
        if (!input) {
            fprintf(stderr, "wikimark: cannot open '%s'\n", argv[1]);
            return 1;
        }
    }

    size_t len = 0;
    char *text = read_all(input, &len);
    if (input != stdin) fclose(input);

    if (!text) {
        fprintf(stderr, "wikimark: out of memory\n");
        return 1;
    }

    char *html = wikimark_markdown_to_html(text, len, CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE);
    free(text);

    if (!html) {
        fprintf(stderr, "wikimark: conversion failed\n");
        return 1;
    }

    fputs(html, stdout);
    wikimark_free(html);

    return 0;
}
