/**
 * Simple file-based wiki engine for testing.
 */

#include "test_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal headers for frontmatter parsing */
#include "frontmatter.h"

struct test_engine {
    char *template_dir;
    char *pages_dir;
    wm_fm_node *frontmatter;

    /* Cache for resolved strings (freed with engine) */
    char **resolved_strings;
    int resolved_count;
    int resolved_cap;

    int depth;  /* Recursion depth */
};

static void cache_string(test_engine *e, char *s) {
    if (e->resolved_count >= e->resolved_cap) {
        e->resolved_cap = e->resolved_cap ? e->resolved_cap * 2 : 32;
        e->resolved_strings = realloc(e->resolved_strings,
            e->resolved_cap * sizeof(char *));
    }
    e->resolved_strings[e->resolved_count++] = s;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (buf) {
        size_t n = fread(buf, 1, len, f);
        buf[n] = '\0';
    }
    fclose(f);
    return buf;
}

/* --- Callbacks --- */

static const char *resolve_variable(const char *path, void *user_data) {
    test_engine *e = (test_engine *)user_data;
    if (!e->frontmatter) return NULL;
    return wm_frontmatter_get(e->frontmatter, path);
}

static const char *resolve_template(const char *name, const char *args,
                                     void *user_data) {
    test_engine *e = (test_engine *)user_data;
    if (!e->template_dir) return NULL;
    if (e->depth > 40) return NULL; /* Recursion limit */

    /* Build file path: template_dir/name.wm */
    size_t path_len = strlen(e->template_dir) + strlen(name) + 10;
    char *path = (char *)malloc(path_len);
    snprintf(path, path_len, "%s/%s.wm", e->template_dir, name);

    char *source = read_file(path);
    free(path);
    if (!source) return NULL;

    /* Parse template frontmatter for inputs */
    size_t body_start = 0;
    wm_fm_node *tmpl_fm = wm_frontmatter_parse(source, strlen(source), &body_start);

    /* TODO: properly map args to inputs. For now, simple approach:
     * Parse named args and set them as frontmatter values. */

    /* Render the template body through the parser */
    e->depth++;
    wikimark_config config = wikimark_config_default();
    wikimark_context ctx = test_engine_get_context(e);

    /* Temporarily replace frontmatter with template's + args */
    wm_fm_node *saved_fm = e->frontmatter;
    e->frontmatter = tmpl_fm;

    char *html = wikimark_render(source + body_start,
        strlen(source) - body_start, 0, &config, &ctx);
    e->depth--;

    e->frontmatter = saved_fm;
    wm_frontmatter_free(tmpl_fm);
    free(source);

    if (html) {
        cache_string(e, html);
    }
    return html;
}

static const char *resolve_embed(const char *target, void *user_data) {
    test_engine *e = (test_engine *)user_data;
    if (!e->pages_dir) return NULL;
    if (e->depth > 40) return NULL;

    size_t path_len = strlen(e->pages_dir) + strlen(target) + 10;
    char *path = (char *)malloc(path_len);
    snprintf(path, path_len, "%s/%s.wm", e->pages_dir, target);

    char *source = read_file(path);
    free(path);
    if (!source) return NULL;

    e->depth++;
    wikimark_config config = wikimark_config_default();
    wikimark_context ctx = test_engine_get_context(e);
    char *html = wikimark_render(source, strlen(source), 0, &config, &ctx);
    e->depth--;
    free(source);

    if (html) {
        cache_string(e, html);
    }
    return html;
}

/* --- Public API --- */

test_engine *test_engine_new(const char *template_dir, const char *pages_dir) {
    test_engine *e = (test_engine *)calloc(1, sizeof(test_engine));
    if (template_dir) e->template_dir = strdup(template_dir);
    if (pages_dir) e->pages_dir = strdup(pages_dir);
    return e;
}

void test_engine_set_frontmatter_text(test_engine *engine, const char *yaml_text) {
    if (engine->frontmatter) {
        wm_frontmatter_free(engine->frontmatter);
        engine->frontmatter = NULL;
    }
    if (yaml_text) {
        size_t body_start = 0;
        /* Wrap in --- fences if not already */
        size_t len = strlen(yaml_text);
        engine->frontmatter = wm_frontmatter_parse(yaml_text, len, &body_start);
    }
}

wikimark_context test_engine_get_context(test_engine *engine) {
    wikimark_context ctx = wikimark_context_default();
    ctx.resolve_variable = resolve_variable;
    ctx.resolve_template = resolve_template;
    ctx.resolve_embed = resolve_embed;
    ctx.user_data = engine;
    return ctx;
}

void test_engine_free(test_engine *engine) {
    if (!engine) return;
    free(engine->template_dir);
    free(engine->pages_dir);
    wm_frontmatter_free(engine->frontmatter);
    for (int i = 0; i < engine->resolved_count; i++)
        free(engine->resolved_strings[i]);
    free(engine->resolved_strings);
    free(engine);
}
