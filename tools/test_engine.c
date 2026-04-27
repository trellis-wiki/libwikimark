/**
 * Simple file-based wiki engine for testing.
 */

#include "test_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal headers */
#include "frontmatter.h"
#include "variable.h"

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
        void *tmp = realloc(e->resolved_strings,
            e->resolved_cap * sizeof(char *));
        if (!tmp) return;
        e->resolved_strings = tmp;
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
                /* Scan to closing quote, tracking escapes */
                size_t vi = 0;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) {
                        p++; /* skip backslash, copy next char */
                    }
                    if (vi < sizeof(val) - 1) val[vi++] = *p;
                    p++;
                }
                val[vi] = '\0';
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
                size_t vi = 0;
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) {
                        p++; /* skip backslash, copy next char */
                    }
                    if (vi < sizeof(val) - 1) val[vi++] = *p;
                    p++;
                }
                val[vi] = '\0';
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

    /* Map positional args to named inputs */
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
        int input_index = 1;
        wm_fm_node *input = inputs->children;
        while (input) {
            if (input->key) {
                char idx_str[16];
                snprintf(idx_str, sizeof(idx_str), "%d", input_index);
                wm_fm_node *c = fm->children;
                while (c) {
                    if (c->key && strcmp(c->key, idx_str) == 0) {
                        free(c->key);
                        c->key = strdup(input->key);
                        break;
                    }
                    c = c->next;
                }
                input_index++;
            }
            input = input->next;
        }
    }

    /* Promote defaults for any inputs not provided by caller */
    wm_frontmatter_promote_input_defaults(fm);
}

static const char *resolve_template(const char *name, const char *args,
                                     void *user_data) {
    test_engine *e = (test_engine *)user_data;
    if (!e->template_dir) return NULL;
    if (e->depth > 40) return NULL;

    size_t path_len = strlen(e->template_dir) + strlen(name) + 10;
    char *path = (char *)malloc(path_len);
    if (!path) return NULL;
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

    /* Check for missing required inputs */
    wm_fm_node *check_inputs = NULL;
    {
        wm_fm_node *c = tmpl_fm->children;
        while (c) {
            if (c->key && strcmp(c->key, "inputs") == 0) { check_inputs = c; break; }
            c = c->next;
        }
    }
    if (check_inputs && check_inputs->children) {
        wm_fm_node *inp = check_inputs->children;
        while (inp) {
            if (inp->key) {
                const char *req = wm_frontmatter_get(inp, "required");
                if (req && strcmp(req, "true") == 0) {
                    /* Check if value exists */
                    int found = 0;
                    wm_fm_node *c = tmpl_fm->children;
                    while (c) {
                        if (c->key && strcmp(c->key, inp->key) == 0 && c != check_inputs) {
                            found = 1;
                            break;
                        }
                        c = c->next;
                    }
                    if (!found) {
                        /* Return error message */
                        size_t err_len = strlen(name) + strlen(inp->key) + 100;
                        char *err = (char *)malloc(err_len);
                        snprintf(err, err_len,
                            "<span class=\"wm-error\">{{%s}}: missing required input \"%s\"</span>",
                            name, inp->key);
                        wm_frontmatter_free(tmpl_fm);
                        free(source);
                        cache_string(e, err);
                        return err;
                    }
                }
            }
            inp = inp->next;
        }
    }

    /* Pre-expand variables in the template body using the merged frontmatter.
     * This is needed because wikimark_render extracts its own frontmatter
     * from the text, but the template body (after frontmatter stripping) has
     * no frontmatter — the args/defaults are in tmpl_fm. */
    const char *body = source + body_start;
    size_t body_len = source_len - body_start;

    /* Build a variable resolver from the merged template frontmatter */
    wikimark_context var_ctx = wikimark_context_default();
    var_ctx.resolve_variable = resolve_variable;

    wm_fm_node *saved_fm = e->frontmatter;
    e->frontmatter = tmpl_fm;
    var_ctx.user_data = e;

    char *expanded = wm_expand_variables_str(body, body_len, &var_ctx);
    const char *render_body = expanded ? expanded : body;
    size_t render_len = expanded ? strlen(expanded) : body_len;

    /* Render the expanded template body */
    e->depth++;
    wikimark_config config = wikimark_config_default();
    wikimark_context render_ctx = test_engine_get_context(e);

    char *html = wikimark_render(render_body, render_len, CMARK_OPT_UNSAFE, &config, &render_ctx);
    e->depth--;

    e->frontmatter = saved_fm;
    free(expanded);
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

    /* Split target on # for section embeds: "Main Page#History" → page, section */
    char *page = strdup(target);
    char *section = NULL;
    char *hash = strchr(page, '#');
    if (hash) {
        *hash = '\0';
        section = strdup(hash + 1); /* Copy before freeing page */
    }

    /* Normalize spaces to underscores for filename */
    for (char *p = page; *p; p++) {
        if (*p == ' ') *p = '_';
    }

    size_t path_len = strlen(e->pages_dir) + strlen(page) + 10;
    char *path = (char *)malloc(path_len);
    if (!path) { free(page); free(section); return NULL; }
    snprintf(path, path_len, "%s/%s.wm", e->pages_dir, page);
    free(page);

    char *source = read_file(path);
    free(path);
    if (!source) return NULL;

    e->depth++;
    wikimark_config config = wikimark_config_default();
    wikimark_context ctx = test_engine_get_context(e);
    char *html = wikimark_render(source, strlen(source), CMARK_OPT_UNSAFE, &config, &ctx);
    e->depth--;
    free(source);

    /* If section targeting is requested, extract just that section */
    if (html && section) {
        /* Find <h2>Section Name</h2> and extract until next <h2> or end */
        char search[256];
        snprintf(search, sizeof(search), ">%s</h", section);
        char *found = strstr(html, search);
        if (found) {
            /* Find the start of content after the heading's closing tag */
            char *after_heading = strstr(found, "</h");
            if (after_heading) {
                after_heading = strchr(after_heading, '>');
                if (after_heading) {
                    after_heading++; /* Past > */
                    if (*after_heading == '\n') after_heading++;
                    /* Find next heading of same or higher level, or end */
                    char *end = strstr(after_heading, "<h");
                    if (!end) end = html + strlen(html);
                    /* Trim trailing whitespace */
                    while (end > after_heading && (end[-1] == '\n' || end[-1] == ' '))
                        end--;
                    size_t sec_len = end - after_heading;
                    /* Strip <p></p> wrapper if present */
                    if (sec_len > 7 &&
                        strncmp(after_heading, "<p>", 3) == 0 &&
                        strncmp(end - 4, "</p>", 4) == 0) {
                        after_heading += 3;
                        sec_len -= 7;
                    }
                    char *sec_html = (char *)malloc(sec_len + 1);
                    memcpy(sec_html, after_heading, sec_len);
                    sec_html[sec_len] = '\0';
                    free(html);
                    html = sec_html;
                }
            }
        }
    }

    free(section);
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
