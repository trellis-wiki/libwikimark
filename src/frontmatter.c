/**
 * YAML frontmatter extraction and parsing using libyaml.
 */

#include "frontmatter.h"
#include <yaml.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* --- Frontmatter tree construction --- */

static wm_fm_node *node_new(const char *key, const char *value) {
    wm_fm_node *n = (wm_fm_node *)calloc(1, sizeof(wm_fm_node));
    if (key) n->key = strdup(key);
    if (value) n->value = strdup(value);
    return n;
}

/* last_child is tracked per-parse via local variables in the parse functions.
 * This simple version is used for small trees; parse functions use direct
 * pointer tracking for O(1) appends. */
static void node_add_child(wm_fm_node *parent, wm_fm_node *child) {
    if (!parent->children) {
        parent->children = child;
    } else {
        /* Walk to end — acceptable for small trees (YAML frontmatter is typically < 50 keys) */
        wm_fm_node *last = parent->children;
        while (last->next) last = last->next;
        last->next = child;
    }
}

void wm_frontmatter_free(wm_fm_node *root) {
    if (!root) return;
    wm_frontmatter_free(root->children);
    wm_frontmatter_free(root->next);
    free(root->key);
    free(root->value);
    free(root);
}

/* --- YAML parsing into tree --- */

static wm_fm_node *parse_yaml_value(yaml_parser_t *parser);

static wm_fm_node *parse_yaml_mapping(yaml_parser_t *parser) {
    wm_fm_node *map = node_new(NULL, NULL);
    yaml_event_t event;

    while (1) {
        if (!yaml_parser_parse(parser, &event)) break;
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        if (event.type != YAML_SCALAR_EVENT) {
            yaml_event_delete(&event);
            continue;
        }

        char *key = strdup((char *)event.data.scalar.value);
        yaml_event_delete(&event);

        wm_fm_node *val = parse_yaml_value(parser);
        if (val) {
            free(val->key);
            val->key = key;
            node_add_child(map, val);
        } else {
            free(key);
        }
    }
    return map;
}

static wm_fm_node *parse_yaml_sequence(yaml_parser_t *parser) {
    wm_fm_node *list = node_new(NULL, NULL);
    list->is_list = 1;
    int index = 0;

    while (1) {
        yaml_event_t event;
        if (!yaml_parser_parse(parser, &event)) break;
        if (event.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }

        wm_fm_node *item = NULL;
        if (event.type == YAML_SCALAR_EVENT) {
            item = node_new(NULL, (char *)event.data.scalar.value);
            yaml_event_delete(&event);
        } else if (event.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&event);
            item = parse_yaml_mapping(parser);
        } else if (event.type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&event);
            item = parse_yaml_sequence(parser);
        } else {
            yaml_event_delete(&event);
            continue;
        }

        if (item) {
            char idx[16];
            snprintf(idx, sizeof(idx), "%d", index++);
            free(item->key);
            item->key = strdup(idx);
            node_add_child(list, item);
        }
    }
    return list;
}

static wm_fm_node *parse_yaml_value(yaml_parser_t *parser) {
    yaml_event_t event;
    if (!yaml_parser_parse(parser, &event)) return NULL;

    wm_fm_node *result = NULL;
    switch (event.type) {
        case YAML_SCALAR_EVENT:
            result = node_new(NULL, (char *)event.data.scalar.value);
            break;
        case YAML_MAPPING_START_EVENT:
            result = parse_yaml_mapping(parser);
            break;
        case YAML_SEQUENCE_START_EVENT:
            result = parse_yaml_sequence(parser);
            break;
        default:
            break;
    }
    yaml_event_delete(&event);
    return result;
}

/* --- Public API --- */

wm_fm_node *wm_frontmatter_parse(const char *text, size_t len,
                                  size_t *body_start) {
    *body_start = 0;

    if (len < 4) return NULL;

    /* Check for opening --- */
    if (text[0] != '-' || text[1] != '-' || text[2] != '-')
        return NULL;
    /* Must be followed by newline or be exactly --- */
    if (len > 3 && text[3] != '\n' && text[3] != '\r')
        return NULL;

    /* Find closing --- or ... */
    size_t start = (text[3] == '\r' && len > 4 && text[4] == '\n') ? 5 :
                   (text[3] == '\n' || text[3] == '\r') ? 4 : 3;
    size_t yaml_start = start;
    size_t yaml_end = 0;
    size_t i = start;

    while (i < len) {
        /* Check for --- or ... at start of line */
        if (i + 2 < len &&
            ((text[i] == '-' && text[i+1] == '-' && text[i+2] == '-') ||
             (text[i] == '.' && text[i+1] == '.' && text[i+2] == '.'))) {
            /* Check that it's followed by newline or EOF */
            size_t after = i + 3;
            if (after >= len || text[after] == '\n' || text[after] == '\r') {
                yaml_end = i;
                /* Skip past the closing fence and its newline */
                i = after;
                if (i < len && text[i] == '\r') i++;
                if (i < len && text[i] == '\n') i++;
                *body_start = i;
                break;
            }
        }
        /* Advance to next line */
        while (i < len && text[i] != '\n' && text[i] != '\r') i++;
        if (i < len && text[i] == '\r') i++;
        if (i < len && text[i] == '\n') i++;
    }

    if (yaml_end == 0)
        return NULL; /* No closing fence */

    /* Parse the YAML content */
    size_t yaml_len = yaml_end - yaml_start;
    if (yaml_len == 0) {
        return node_new(NULL, NULL); /* Empty frontmatter */
    }

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (unsigned char *)text + yaml_start, yaml_len);

    /* Skip STREAM_START and DOCUMENT_START events */
    yaml_event_t event;
    wm_fm_node *root = NULL;

    while (yaml_parser_parse(&parser, &event)) {
        if (event.type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&event);
            root = parse_yaml_mapping(&parser);
            break;
        }
        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    return root;
}

const char *wm_frontmatter_get(const wm_fm_node *root, const char *path) {
    if (!root || !path || !*path)
        return NULL;

    const wm_fm_node *current = root;
    const char *p = path;

    while (*p && current) {
        /* Find the end of this segment (next '.' or end of string) */
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t seg_len = dot - p;

        /* Search children for matching key */
        const wm_fm_node *child = current->children;
        const wm_fm_node *found = NULL;
        while (child) {
            if (child->key && strlen(child->key) == seg_len &&
                memcmp(child->key, p, seg_len) == 0) {
                found = child;
                break;
            }
            child = child->next;
        }

        if (!found)
            return NULL;

        /* Advance past the segment (and the dot if present) */
        p = *dot ? dot + 1 : dot;

        if (!*p) {
            /* Final segment — return the value */
            return found->value;
        }

        /* Descend into children */
        current = found;
    }

    return NULL;
}

void wm_frontmatter_promote_input_defaults(wm_fm_node *fm) {
    if (!fm) return;

    wm_fm_node *inputs = NULL;
    wm_fm_node *c = fm->children;
    while (c) {
        if (c->key && strcmp(c->key, "inputs") == 0) { inputs = c; break; }
        c = c->next;
    }
    if (!inputs || !inputs->children) return;

    /* Keep a tail pointer to avoid repeated O(n) traversals */
    wm_fm_node *tail = fm->children;
    while (tail->next) tail = tail->next;

    wm_fm_node *inp = inputs->children;
    while (inp) {
        if (inp->key) {
            /* Check if a top-level key already exists */
            int found = 0;
            c = fm->children;
            while (c) {
                if (c->key && strcmp(c->key, inp->key) == 0 && c != inputs) {
                    found = 1;
                    break;
                }
                c = c->next;
            }
            if (!found) {
                const char *def = wm_frontmatter_get(inp, "default");
                if (def) {
                    wm_fm_node *defnode = (wm_fm_node *)calloc(1, sizeof(wm_fm_node));
                    if (defnode) {
                        defnode->key = strdup(inp->key);
                        defnode->value = strdup(def);
                        tail->next = defnode;
                        tail = defnode;
                    }
                }
            }
        }
        inp = inp->next;
    }
}
