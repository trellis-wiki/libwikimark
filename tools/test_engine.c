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

/**
 * Parse template arguments string into the frontmatter tree.
 * Handles: positional ("value"), named (key=value, key="value").
 * Positional args get keys "1", "2", etc.
 * Also promotes inputs defaults for missing args.
 */
static void merge_template_args(wm_fm_node *fm, const char *args) {
    if (!fm) return;

    const char *p = args;
    int pos_index = 1;

    while (p && *p) {
        while (*p == ' ') p++;
        if (!*p) break;

        char key[64] = "";
        char val[1024] = "";

        /* Check for key=value */
        const char *eq = NULL;
        const char *scan = p;
        if (*scan != '"') {
            while (*scan && *scan != '=' && *scan != ' ') scan++;
            if (*scan == '=') eq = scan;
        }

        if (eq) {
            /* Named argument: key=value or key="value" */
            size_t klen = eq - p;
            if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, p, klen);
            key[klen] = '\0';
            p = eq + 1;

            if (*p == '"') {
                p++;
                const char *vs = p;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) p++;
                    p++;
                }
                size_t vlen = p - vs;
                if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                memcpy(val, vs, vlen);
                val[vlen] = '\0';
                if (*p == '"') p++;
            } else {
                const char *vs = p;
                while (*p && *p != ' ') p++;
                size_t vlen = p - vs;
                if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                memcpy(val, vs, vlen);
                val[vlen] = '\0';
            }
        } else {
            /* Positional argument */
            snprintf(key, sizeof(key), "%d", pos_index++);

            if (*p == '"') {
                p++;
                const char *vs = p;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) p++;
                    p++;
                }
                size_t vlen = p - vs;
                if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                memcpy(val, vs, vlen);
                val[vlen] = '\0';
                if (*p == '"') p++;
            } else {
                const char *vs = p;
                while (*p && *p != ' ') p++;
                size_t vlen = p - vs;
                if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
                memcpy(val, vs, vlen);
                val[vlen] = '\0';
            }
        }

        /* Add to frontmatter as a top-level key */
        wm_fm_node *node = (wm_fm_node *)calloc(1, sizeof(wm_fm_node));
        node->key = strdup(key);
        node->value = strdup(val);

        /* Append as child of fm root */
        if (!fm->children) {
            fm->children = node;
        } else {
            /* Check if key already exists — override */
            wm_fm_node *existing = fm->children;
            wm_fm_node *prev = NULL;
            int replaced = 0;
            while (existing) {
                if (existing->key && strcmp(existing->key, key) == 0) {
                    free(existing->value);
                    existing->value = strdup(val);
                    free(node->key);
                    free(node->value);
                    free(node);
                    replaced = 1;
                    break;
                }
                prev = existing;
                existing = existing->next;
            }
            if (!replaced && prev) {
                prev->next = node;
            }
        }
    }

    /* Map positional args to named inputs and promote defaults */
    wm_fm_node *inputs = NULL;
    wm_fm_node *child = fm->children;
    while (child) {
        if (child->key && strcmp(child->key, "inputs") == 0) {
            inputs = child;
            break;
        }
        child = child->next;
    }

    if (inputs && inputs->children) {
        /* Map positional args (key "1", "2", ...) to input names */
        int input_index = 1;
        wm_fm_node *input = inputs->children;
        while (input) {
            if (input->key) {
                char idx_str[16];
                snprintf(idx_str, sizeof(idx_str), "%d", input_index);

                /* If a positional arg exists with this index, remap to the input's name */
                wm_fm_node *c = fm->children;
                while (c) {
                    if (c->key && strcmp(c->key, idx_str) == 0) {
                        /* Rename the key from "1" to the input name */
                        free(c->key);
                        c->key = strdup(input->key);
                        break;
                    }
                    c = c->next;
                }

                /* If no arg (positional or named) matches, promote default */
                int found = 0;
                c = fm->children;
                while (c) {
                    if (c->key && strcmp(c->key, input->key) == 0 && c != inputs) {
                        found = 1;
                        break;
                    }
                    c = c->next;
                }
                if (!found) {
                    const char *def = wm_frontmatter_get(input, "default");
                    if (def) {
                        wm_fm_node *defnode = (wm_fm_node *)calloc(1, sizeof(wm_fm_node));
                        defnode->key = strdup(input->key);
                        defnode->value = strdup(def);
                        wm_fm_node *last = fm->children;
                        while (last->next) last = last->next;
                        last->next = defnode;
                    }
                }
                input_index++;
            }
            input = input->next;
        }
    }
}

static const char *resolve_template(const char *name, const char *args,
                                     void *user_data) {
    test_engine *e = (test_engine *)user_data;
    if (!e->template_dir) return NULL;
    if (e->depth > 40) return NULL;

    size_t path_len = strlen(e->template_dir) + strlen(name) + 10;
    char *path = (char *)malloc(path_len);
    snprintf(path, path_len, "%s/%s.wm", e->template_dir, name);

    char *source = read_file(path);
    free(path);
    if (!source) return NULL;

    size_t source_len = strlen(source);

    /* Parse template frontmatter */
    size_t body_start = 0;
    wm_fm_node *tmpl_fm = wm_frontmatter_parse(source, source_len, &body_start);
    if (!tmpl_fm) tmpl_fm = (wm_fm_node *)calloc(1, sizeof(wm_fm_node));

    /* Merge caller's arguments into template frontmatter */
    merge_template_args(tmpl_fm, args);

    /* Render the template body */
    e->depth++;
    wikimark_config config = wikimark_config_default();
    wikimark_context ctx = test_engine_get_context(e);

    wm_fm_node *saved_fm = e->frontmatter;
    e->frontmatter = tmpl_fm;

    char *html = wikimark_render(source + body_start,
        source_len - body_start, 0, &config, &ctx);
    e->depth--;

    e->frontmatter = saved_fm;
    wm_frontmatter_free(tmpl_fm);
    free(source);

    if (html) {
        /* Strip trailing newline from template output for inline insertion */
        size_t hlen = strlen(html);
        while (hlen > 0 && (html[hlen-1] == '\n' || html[hlen-1] == '\r'))
            html[--hlen] = '\0';
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
    /* Note: single strlen call here — source is only used once */
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
