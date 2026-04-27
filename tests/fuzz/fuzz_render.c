/**
 * libFuzzer target for wikimark_render().
 *
 * Exercises the full pipeline: frontmatter parsing, variable
 * expansion, template/embed callback dispatch, wiki link
 * postprocessing, callout conversion, attribute attachment,
 * annotation parsing, and HTML serialization.
 *
 * Build (from the libwikimark build/ directory):
 *   cmake -DWIKIMARK_FUZZ=ON -DCMAKE_C_FLAGS="-fsanitize=fuzzer,address -g" ..
 *   make fuzz_render
 *
 * Run:
 *   ./fuzz_render ../tests/fuzz/corpus/
 *
 * Seed the corpus directory with the spec test inputs:
 *   python3 ../tests/fuzz/seed_corpus.py ../tests/fuzz/corpus/
 */

#include "wikimark.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Dummy template resolver: returns a fixed string so template
 * expansion actually runs without needing a filesystem. */
static const char *fuzz_resolve_template(const char *name,
                                          const char *args,
                                          void *user_data) {
    (void)args;
    (void)user_data;
    /* Return a small HTML string so the expansion pipeline runs
     * its strip-<p>, insert-inline-HTML, and cache paths. */
    if (name && name[0]) {
        return "<span>fuzz</span>";
    }
    return NULL;
}

/* Dummy embed resolver */
static const char *fuzz_resolve_embed(const char *target,
                                       void *user_data) {
    (void)target;
    (void)user_data;
    return "<p>fuzz-embed</p>";
}

/* Dummy variable resolver */
static const char *fuzz_resolve_variable(const char *path,
                                          void *user_data) {
    (void)path;
    (void)user_data;
    return "fuzz-var";
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Cap input size to avoid OOM on absurdly large inputs;
     * libFuzzer can generate up to ~1 MB by default. We limit to
     * 256 KB here since that's far larger than any realistic wiki
     * page and keeps the iteration rate up. */
    if (size > 256 * 1024) return 0;

    wikimark_extensions_ensure_registered();

    wikimark_config config = wikimark_config_default();

    wikimark_context ctx = wikimark_context_default();
    ctx.resolve_template = fuzz_resolve_template;
    ctx.resolve_embed = fuzz_resolve_embed;
    ctx.resolve_variable = fuzz_resolve_variable;

    char *html = wikimark_render((const char *)data, size,
                                  CMARK_OPT_UNSAFE, &config, &ctx);
    if (html) {
        wikimark_free(html);
    }

    return 0;
}
