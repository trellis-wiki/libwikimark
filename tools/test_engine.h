/**
 * Simple file-based wiki engine for testing.
 *
 * Resolves templates from a directory of .wm files.
 * Resolves variables from frontmatter.
 * Resolves embeds by rendering page files.
 *
 * This is NOT a production engine — it's a test fixture.
 */

#ifndef WIKIMARK_TEST_ENGINE_H
#define WIKIMARK_TEST_ENGINE_H

#include "wikimark.h"

typedef struct test_engine test_engine;

/**
 * Create a test engine.
 * template_dir: directory containing template .wm files (or NULL)
 * pages_dir: directory containing page .wm files for embeds (or NULL)
 */
test_engine *test_engine_new(const char *template_dir, const char *pages_dir);

/**
 * Set the frontmatter for variable resolution.
 * The engine takes ownership of the frontmatter string (will be parsed internally).
 */
void test_engine_set_frontmatter_text(test_engine *engine, const char *yaml_text);

/**
 * Get a wikimark_context with callbacks pointing to this engine.
 */
wikimark_context test_engine_get_context(test_engine *engine);

/**
 * Free the engine.
 */
void test_engine_free(test_engine *engine);

#endif
